/* $Id: RTSignTool.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Signing Tool.
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
#include <iprt/assert.h>
#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/getopt.h>
#include <iprt/file.h>
#include <iprt/initterm.h>
#include <iprt/ldr.h>
#include <iprt/message.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#ifdef RT_OS_WINDOWS
# include <iprt/utf16.h>
#endif
#include <iprt/uuid.h>
#include <iprt/zero.h>
#include <iprt/formats/asn1.h>
#include <iprt/formats/mach-o.h>
#ifndef RT_OS_WINDOWS
# include <iprt/formats/pecoff.h>
#else
# define WIN_CERTIFICATE_ALIGNMENT      UINT32_C(8) /* from pecoff.h */
#endif
#include <iprt/crypto/applecodesign.h>
#include <iprt/crypto/digest.h>
#include <iprt/crypto/key.h>
#include <iprt/crypto/x509.h>
#include <iprt/crypto/pkcs7.h>
#include <iprt/crypto/store.h>
#include <iprt/crypto/spc.h>
#include <iprt/crypto/tsp.h>
#include <iprt/cpp/ministring.h>
#ifdef VBOX
# include <VBox/sup.h> /* Certificates */
#endif
#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
# include <iprt/win/imagehlp.h>
# include <wincrypt.h>
# include <ncrypt.h>
#endif
#include "internal/ldr.h" /* for IMAGE_XX_SIGNATURE defines */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define OPT_OFF_CERT_FILE                   0       /**< signtool /f file */
#define OPT_OFF_CERT_SHA1                   1       /**< signtool /sha1 thumbprint */
#define OPT_OFF_CERT_SUBJECT                2       /**< signtool /n name */
#define OPT_OFF_CERT_STORE                  3       /**< signtool /s store */
#define OPT_OFF_CERT_STORE_MACHINE          4       /**< signtool /sm */
#define OPT_OFF_KEY_FILE                    5       /**< no signtool equivalent, other than maybe /f. */
#define OPT_OFF_KEY_PASSWORD                6       /**< signtool /p pass */
#define OPT_OFF_KEY_PASSWORD_FILE           7       /**< no signtool equivalent. */
#define OPT_OFF_KEY_NAME                    8       /**< signtool /kc  name */
#define OPT_OFF_KEY_PROVIDER                9       /**< signtool /csp name (CSP = cryptographic service provider) */

#define OPT_CERT_KEY_SWITCH_CASES(a_Instance, a_uBase, a_chOpt, a_ValueUnion, a_rcExit) \
        case (a_uBase) + OPT_OFF_CERT_FILE: \
        case (a_uBase) + OPT_OFF_CERT_SHA1: \
        case (a_uBase) + OPT_OFF_CERT_SUBJECT: \
        case (a_uBase) + OPT_OFF_CERT_STORE: \
        case (a_uBase) + OPT_OFF_CERT_STORE_MACHINE: \
        case (a_uBase) + OPT_OFF_KEY_FILE: \
        case (a_uBase) + OPT_OFF_KEY_PASSWORD: \
        case (a_uBase) + OPT_OFF_KEY_PASSWORD_FILE: \
        case (a_uBase) + OPT_OFF_KEY_NAME: \
        case (a_uBase) + OPT_OFF_KEY_PROVIDER: \
            a_rcExit = a_Instance.handleOption((a_chOpt) - (a_uBase), &(a_ValueUnion)); \
            break

#define OPT_CERT_KEY_GETOPTDEF_ENTRIES(a_szPrefix, a_szSuffix, a_uBase) \
    { a_szPrefix "cert-file" a_szSuffix,          (a_uBase) + OPT_OFF_CERT_FILE,          RTGETOPT_REQ_STRING  }, \
    { a_szPrefix "cert-sha1" a_szSuffix,          (a_uBase) + OPT_OFF_CERT_SHA1,          RTGETOPT_REQ_STRING  }, \
    { a_szPrefix "cert-subject" a_szSuffix,       (a_uBase) + OPT_OFF_CERT_SUBJECT,       RTGETOPT_REQ_STRING  }, \
    { a_szPrefix "cert-store" a_szSuffix,         (a_uBase) + OPT_OFF_CERT_STORE,         RTGETOPT_REQ_STRING  }, \
    { a_szPrefix "cert-machine-store" a_szSuffix, (a_uBase) + OPT_OFF_CERT_STORE_MACHINE, RTGETOPT_REQ_NOTHING }, \
    { a_szPrefix "key-file" a_szSuffix,           (a_uBase) + OPT_OFF_KEY_FILE,           RTGETOPT_REQ_STRING  }, \
    { a_szPrefix "key-password" a_szSuffix,       (a_uBase) + OPT_OFF_KEY_PASSWORD,       RTGETOPT_REQ_STRING  }, \
    { a_szPrefix "key-password-file" a_szSuffix,  (a_uBase) + OPT_OFF_KEY_PASSWORD_FILE,  RTGETOPT_REQ_STRING  }, \
    { a_szPrefix "key-name" a_szSuffix,           (a_uBase) + OPT_OFF_KEY_NAME,           RTGETOPT_REQ_STRING  }, \
    { a_szPrefix "key-provider" a_szSuffix,       (a_uBase) + OPT_OFF_KEY_PROVIDER,       RTGETOPT_REQ_STRING  }

#define OPT_CERT_KEY_GETOPTDEF_COMPAT_ENTRIES(a_uBase) \
    { "/f",                                       (a_uBase) + OPT_OFF_CERT_FILE,          RTGETOPT_REQ_STRING }, \
    { "/sha1",                                    (a_uBase) + OPT_OFF_CERT_SHA1,          RTGETOPT_REQ_STRING }, \
    { "/n",                                       (a_uBase) + OPT_OFF_CERT_SUBJECT,       RTGETOPT_REQ_STRING }, \
    { "/s",                                       (a_uBase) + OPT_OFF_CERT_STORE,         RTGETOPT_REQ_STRING }, \
    { "/sm",                                      (a_uBase) + OPT_OFF_CERT_STORE_MACHINE, RTGETOPT_REQ_NOTHING }, \
    { "/p",                                       (a_uBase) + OPT_OFF_KEY_PASSWORD,       RTGETOPT_REQ_STRING }, \
    { "/kc",                                      (a_uBase) + OPT_OFF_KEY_NAME,           RTGETOPT_REQ_STRING }, \
    { "/csp",                                     (a_uBase) + OPT_OFF_KEY_PROVIDER,       RTGETOPT_REQ_STRING }

#define OPT_CERT_KEY_SYNOPSIS(a_szPrefix, a_szSuffix) \
    "[" a_szPrefix "cert-file" a_szSuffix " <file.pem|file.crt>] " \
    "[" a_szPrefix "cert-sha1" a_szSuffix " <fingerprint>] " \
    "[" a_szPrefix "cert-subject" a_szSuffix " <part-name>] " \
    "[" a_szPrefix "cert-store" a_szSuffix " <store>] " \
    "[" a_szPrefix "cert-machine-store" a_szSuffix "] " \
    "[" a_szPrefix "key-file" a_szSuffix " <file.pem|file.p12>] " \
    "[" a_szPrefix "key-password" a_szSuffix " <password>] " \
    "[" a_szPrefix "key-password-file" a_szSuffix " <file>|stdin] " \
    "[" a_szPrefix "key-name" a_szSuffix " <name>] " \
    "[" a_szPrefix "key-provider" a_szSuffix " <csp>] "

#define OPT_HASH_PAGES                      1200
#define OPT_NO_HASH_PAGES                   1201
#define OPT_ADD_CERT                        1202
#define OPT_TIMESTAMP_TYPE                  1203
#define OPT_TIMESTAMP_TYPE_2                1204
#define OPT_TIMESTAMP_OVERRIDE              1205
#define OPT_NO_SIGNING_TIME                 1206
#define OPT_FILE_TYPE                       1207
#define OPT_IGNORED                         1208


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Help detail levels. */
typedef enum RTSIGNTOOLHELP
{
    RTSIGNTOOLHELP_USAGE,
    RTSIGNTOOLHELP_FULL
} RTSIGNTOOLHELP;


/** Filetypes. */
typedef enum RTSIGNTOOLFILETYPE
{
    RTSIGNTOOLFILETYPE_INVALID = 0,
    RTSIGNTOOLFILETYPE_DETECT,
    RTSIGNTOOLFILETYPE_EXE,
    RTSIGNTOOLFILETYPE_CAT,
    RTSIGNTOOLFILETYPE_UNKNOWN,
    RTSIGNTOOLFILETYPE_END
} RTSIGNTOOLFILETYPE;


/**
 * PKCS\#7 signature data.
 */
typedef struct SIGNTOOLPKCS7
{
    /** The file type. */
    RTSIGNTOOLFILETYPE          enmType;
    /** The raw signature. */
    uint8_t                    *pbBuf;
    /** Size of the raw signature. */
    size_t                      cbBuf;
    /** The filename.   */
    const char                 *pszFilename;
    /** The outer content info wrapper. */
    RTCRPKCS7CONTENTINFO        ContentInfo;
    /** Pointer to the decoded SignedData inside the ContentInfo member. */
    PRTCRPKCS7SIGNEDDATA        pSignedData;

    /** Newly encoded raw signature.
     * @sa SignToolPkcs7_Encode()  */
    uint8_t                    *pbNewBuf;
    /** Size of newly encoded raw signature. */
    size_t                      cbNewBuf;

} SIGNTOOLPKCS7;
typedef SIGNTOOLPKCS7 *PSIGNTOOLPKCS7;


/**
 * PKCS\#7 signature data for executable.
 */
typedef struct SIGNTOOLPKCS7EXE : public SIGNTOOLPKCS7
{
    /** The module handle. */
    RTLDRMOD                    hLdrMod;
} SIGNTOOLPKCS7EXE;
typedef SIGNTOOLPKCS7EXE *PSIGNTOOLPKCS7EXE;


/**
 * Data for the show exe (signature) command.
 */
typedef struct SHOWEXEPKCS7 : public SIGNTOOLPKCS7EXE
{
    /** The verbosity. */
    unsigned                    cVerbosity;
    /** The prefix buffer. */
    char                        szPrefix[256];
    /** Temporary buffer. */
    char                        szTmp[4096];
} SHOWEXEPKCS7;
typedef SHOWEXEPKCS7 *PSHOWEXEPKCS7;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static RTEXITCODE HandleHelp(int cArgs, char **papszArgs);
static RTEXITCODE HelpHelp(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel);
static RTEXITCODE HandleVersion(int cArgs, char **papszArgs);
static int HandleShowExeWorkerPkcs7DisplaySignerInfo(PSHOWEXEPKCS7 pThis, size_t offPrefix, PCRTCRPKCS7SIGNERINFO pSignerInfo);
static int HandleShowExeWorkerPkcs7Display(PSHOWEXEPKCS7 pThis, PRTCRPKCS7SIGNEDDATA pSignedData, size_t offPrefix,
                                           PCRTCRPKCS7CONTENTINFO pContentInfo);


/*********************************************************************************************************************************
*   Certificate and Private Key Handling (options, ++).                                                                          *
*********************************************************************************************************************************/
#ifdef RT_OS_WINDOWS

/** @todo create a better fake certificate. */
const unsigned char g_abFakeCertificate[] =
{
    0x30, 0x82, 0x03, 0xb2, 0x30, 0x82, 0x02, 0x9a, 0xa0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x10, 0x31, /* 0x00000000: 0...0..........1 */
    0xba, 0xd6, 0xbc, 0x5d, 0x9a, 0xe0, 0xb0, 0x4e, 0xd4, 0xfa, 0xcc, 0xfb, 0x47, 0x00, 0x5c, 0x30, /* 0x00000010: ...]...N....G.\0 */
    0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x30, 0x71, /* 0x00000020: ...*.H........0q */
    0x31, 0x1c, 0x30, 0x1a, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x13, 0x54, 0x69, 0x6d, 0x65, 0x73, /* 0x00000030: 1.0...U....Times */
    0x74, 0x61, 0x6d, 0x70, 0x20, 0x53, 0x69, 0x67, 0x6e, 0x69, 0x6e, 0x67, 0x20, 0x32, 0x31, 0x0c, /* 0x00000040: tamp Signing 21. */
    0x30, 0x0a, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x0c, 0x03, 0x44, 0x65, 0x76, 0x31, 0x15, 0x30, 0x13, /* 0x00000050: 0...U....Dev1.0. */
    0x06, 0x03, 0x55, 0x04, 0x0a, 0x0c, 0x0c, 0x54, 0x65, 0x73, 0x74, 0x20, 0x43, 0x6f, 0x6d, 0x70, /* 0x00000060: ..U....Test Comp */
    0x61, 0x6e, 0x79, 0x31, 0x12, 0x30, 0x10, 0x06, 0x03, 0x55, 0x04, 0x07, 0x0c, 0x09, 0x53, 0x74, /* 0x00000070: any1.0...U....St */
    0x75, 0x74, 0x74, 0x67, 0x61, 0x72, 0x74, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x08, /* 0x00000080: uttgart1.0...U.. */
    0x0c, 0x02, 0x42, 0x42, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x44, /* 0x00000090: ..BB1.0...U....D */
    0x45, 0x30, 0x1e, 0x17, 0x0d, 0x30, 0x30, 0x30, 0x31, 0x30, 0x31, 0x30, 0x30, 0x30, 0x31, 0x30, /* 0x000000a0: E0...00010100010 */
    0x31, 0x5a, 0x17, 0x0d, 0x33, 0x36, 0x31, 0x32, 0x33, 0x31, 0x32, 0x32, 0x35, 0x39, 0x35, 0x39, /* 0x000000b0: 1Z..361231225959 */
    0x5a, 0x30, 0x71, 0x31, 0x1c, 0x30, 0x1a, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x13, 0x54, 0x69, /* 0x000000c0: Z0q1.0...U....Ti */
    0x6d, 0x65, 0x73, 0x74, 0x61, 0x6d, 0x70, 0x20, 0x53, 0x69, 0x67, 0x6e, 0x69, 0x6e, 0x67, 0x20, /* 0x000000d0: mestamp Signing  */
    0x32, 0x31, 0x0c, 0x30, 0x0a, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x0c, 0x03, 0x44, 0x65, 0x76, 0x31, /* 0x000000e0: 21.0...U....Dev1 */
    0x15, 0x30, 0x13, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x0c, 0x0c, 0x54, 0x65, 0x73, 0x74, 0x20, 0x43, /* 0x000000f0: .0...U....Test C */
    0x6f, 0x6d, 0x70, 0x61, 0x6e, 0x79, 0x31, 0x12, 0x30, 0x10, 0x06, 0x03, 0x55, 0x04, 0x07, 0x0c, /* 0x00000100: ompany1.0...U... */
    0x09, 0x53, 0x74, 0x75, 0x74, 0x74, 0x67, 0x61, 0x72, 0x74, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, /* 0x00000110: .Stuttgart1.0... */
    0x55, 0x04, 0x08, 0x0c, 0x02, 0x42, 0x42, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, /* 0x00000120: U....BB1.0...U.. */
    0x13, 0x02, 0x44, 0x45, 0x30, 0x82, 0x01, 0x22, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, /* 0x00000130: ..DE0.."0...*.H. */
    0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x82, 0x01, 0x0f, 0x00, 0x30, 0x82, 0x01, 0x0a, /* 0x00000140: ............0... */
    0x02, 0x82, 0x01, 0x01, 0x00, 0xdb, 0x18, 0x63, 0x33, 0xf2, 0x08, 0x90, 0x5a, 0xab, 0xda, 0x88, /* 0x00000150: .......c3...Z... */
    0x73, 0x86, 0x49, 0xea, 0x8b, 0xaf, 0xcf, 0x67, 0x15, 0xa5, 0x39, 0xe6, 0xa2, 0x94, 0x0c, 0x3f, /* 0x00000160: s.I....g..9....? */
    0xa1, 0x2e, 0x6c, 0xd2, 0xdf, 0x01, 0x65, 0x6d, 0xed, 0x6c, 0x4c, 0xac, 0xe7, 0x77, 0x7a, 0x45, /* 0x00000170: ..l...em.lL..wzE */
    0x05, 0x6b, 0x24, 0xf3, 0xaf, 0x45, 0x35, 0x6e, 0x64, 0x0a, 0xac, 0x1d, 0x37, 0xe1, 0x33, 0xa4, /* 0x00000180: .k$..E5nd...7.3. */
    0x92, 0xec, 0x45, 0xe8, 0x99, 0xc1, 0xde, 0x6f, 0xab, 0x7c, 0xf0, 0xdc, 0xe2, 0xc5, 0x42, 0xa3, /* 0x00000190: ..E....o.|....B. */
    0xea, 0xf5, 0x8a, 0xf9, 0x0e, 0xe7, 0xb3, 0x35, 0xa2, 0x75, 0x5e, 0x87, 0xd2, 0x2a, 0xd1, 0x27, /* 0x000001a0: .......5.u^..*.' */
    0xa6, 0x79, 0x9e, 0xfe, 0x90, 0xbf, 0x97, 0xa4, 0xa1, 0xd8, 0xf7, 0xd7, 0x05, 0x59, 0x44, 0x27, /* 0x000001b0: .y...........YD' */
    0x39, 0x6e, 0x33, 0x01, 0x2e, 0x46, 0x92, 0x47, 0xbe, 0x50, 0x91, 0x26, 0x27, 0xe5, 0x4b, 0x3a, /* 0x000001c0: 9n3..F.G.P.&'.K: */
    0x76, 0x26, 0x64, 0x92, 0x0c, 0xa0, 0x54, 0x43, 0x6f, 0x56, 0xcc, 0x7b, 0xd0, 0xe3, 0xd8, 0x39, /* 0x000001d0: v&d...TCoV.{...9 */
    0x5f, 0xb9, 0x41, 0xda, 0x1c, 0x62, 0x88, 0x0c, 0x45, 0x03, 0x63, 0xf8, 0xff, 0xe5, 0x3e, 0x87, /* 0x000001e0: _.A..b..E.c...>. */
    0x0c, 0x75, 0xc9, 0xdd, 0xa2, 0xc0, 0x1b, 0x63, 0x19, 0xeb, 0x09, 0x9d, 0xa1, 0xbb, 0x0f, 0x63, /* 0x000001f0: .u.....c.......c */
    0x67, 0x1c, 0xa3, 0xfd, 0x2f, 0xd1, 0x2a, 0xda, 0xd8, 0x93, 0x66, 0x45, 0x54, 0xef, 0x8b, 0x6d, /* 0x00000200: g.....*...fET..m */
    0x12, 0x15, 0x0f, 0xd4, 0xb5, 0x04, 0x17, 0x30, 0x5b, 0xfa, 0x12, 0x96, 0x48, 0x5b, 0x38, 0x65, /* 0x00000210: .......0[...H[8e */
    0xfd, 0x8f, 0x0c, 0xa3, 0x11, 0x46, 0x49, 0xe0, 0x62, 0xc3, 0xcc, 0x34, 0xe6, 0xfb, 0xab, 0x51, /* 0x00000220: .....FI.b..4...Q */
    0xc3, 0xd4, 0x0b, 0xdc, 0x39, 0x93, 0x87, 0x90, 0x10, 0x9f, 0xce, 0x43, 0x27, 0x31, 0xd5, 0x4e, /* 0x00000230: ....9......C'1.N */
    0x52, 0x60, 0xf1, 0x93, 0xd5, 0x06, 0xc4, 0x4e, 0x65, 0xb6, 0x35, 0x4a, 0x64, 0x15, 0xf8, 0xaf, /* 0x00000240: R`.....Ne.5Jd... */
    0x71, 0xb2, 0x42, 0x50, 0x89, 0x02, 0x03, 0x01, 0x00, 0x01, 0xa3, 0x46, 0x30, 0x44, 0x30, 0x0e, /* 0x00000250: q.BP.......F0D0. */
    0x06, 0x03, 0x55, 0x1d, 0x0f, 0x01, 0x01, 0xff, 0x04, 0x04, 0x03, 0x02, 0x07, 0x80, 0x30, 0x13, /* 0x00000260: ..U...........0. */
    0x06, 0x03, 0x55, 0x1d, 0x25, 0x04, 0x0c, 0x30, 0x0a, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, /* 0x00000270: ..U.%..0...+.... */
    0x07, 0x03, 0x08, 0x30, 0x1d, 0x06, 0x03, 0x55, 0x1d, 0x0e, 0x04, 0x16, 0x04, 0x14, 0x52, 0x9d, /* 0x00000280: ...0...U......R. */
    0x4d, 0xcd, 0x41, 0xe1, 0xd2, 0x68, 0x22, 0xd3, 0x10, 0x33, 0x01, 0xca, 0xff, 0x00, 0x1d, 0x27, /* 0x00000290: M.A..h"..3.....' */
    0xa4, 0x01, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, /* 0x000002a0: ..0...*.H....... */
    0x00, 0x03, 0x82, 0x01, 0x01, 0x00, 0xc5, 0x5a, 0x51, 0x83, 0x68, 0x3f, 0x06, 0x39, 0x79, 0x13, /* 0x000002b0: .......ZQ.h?.9y. */
    0xa6, 0xf0, 0x1a, 0xf9, 0x29, 0x16, 0x2d, 0xa2, 0x07, 0xaa, 0x9b, 0xc3, 0x13, 0x88, 0x39, 0x69, /* 0x000002c0: ....).-.......9i */
    0xba, 0xf7, 0x0d, 0xfb, 0xc0, 0x6e, 0x3a, 0x0b, 0x49, 0x10, 0xd1, 0xbe, 0x36, 0x91, 0x3f, 0x9d, /* 0x000002d0: .....n:.I...6.?. */
    0xa1, 0xe8, 0xc4, 0x91, 0xf9, 0x02, 0xe1, 0xf1, 0x01, 0x15, 0x09, 0xb7, 0xa1, 0xf1, 0xec, 0x43, /* 0x000002e0: ...............C */
    0x0d, 0x73, 0xd1, 0x31, 0x02, 0x4a, 0xce, 0x21, 0xf2, 0xa7, 0x99, 0x7c, 0xee, 0x85, 0x54, 0xc0, /* 0x000002f0: .s.1.J.!...|..T. */
    0x55, 0x9b, 0x19, 0x37, 0xe8, 0xcf, 0x94, 0x41, 0x10, 0x6e, 0x67, 0xdd, 0x86, 0xaf, 0xb7, 0xfe, /* 0x00000300: U..7...A.ng..... */
    0x50, 0x05, 0xf6, 0xfb, 0x0a, 0xdf, 0x88, 0xb5, 0x59, 0x69, 0x98, 0x27, 0xf8, 0x81, 0x6a, 0x4a, /* 0x00000310: P.......Yi.'..jJ */
    0x7c, 0xf3, 0x63, 0xa9, 0x41, 0x78, 0x76, 0x12, 0xdb, 0x0e, 0x94, 0x0a, 0xdb, 0x1d, 0x3c, 0x87, /* 0x00000320: |.c.Axv.......<. */
    0x35, 0xca, 0x28, 0xeb, 0xb0, 0x62, 0x27, 0x69, 0xe2, 0xf3, 0x84, 0x48, 0xa2, 0x2d, 0xd7, 0x0e, /* 0x00000330: 5.(..b'i...H.-.. */
    0x4b, 0x6d, 0x39, 0xa7, 0x3e, 0x04, 0x94, 0x8e, 0xb6, 0x4b, 0x91, 0x01, 0x68, 0xf9, 0xd2, 0x75, /* 0x00000340: Km9.>....K..h..u */
    0x1b, 0xac, 0x42, 0x3b, 0x85, 0xfc, 0x5b, 0x48, 0x3a, 0x13, 0xe7, 0x1c, 0x17, 0xcd, 0x84, 0x89, /* 0x00000350: ..B;..[H:....... */
    0x9e, 0x5f, 0xe3, 0x77, 0xc0, 0xae, 0x34, 0xc3, 0x87, 0x76, 0x4a, 0x23, 0x30, 0xa0, 0xe1, 0x45, /* 0x00000360: ._.w..4..vJ#0..E */
    0x94, 0x2a, 0x5b, 0x6b, 0x5a, 0xf0, 0x1a, 0x7e, 0xa6, 0xc4, 0xed, 0xe4, 0xac, 0x5d, 0xdf, 0x87, /* 0x00000370: .*[kZ..~.....].. */
    0x8f, 0xc5, 0xb4, 0x8c, 0xbc, 0x70, 0xc1, 0xf7, 0xb2, 0x72, 0xbd, 0x73, 0xc9, 0x4e, 0xed, 0x8d, /* 0x00000380: .....p...r.s.N.. */
    0x29, 0x33, 0xe9, 0x14, 0xc1, 0x5e, 0xff, 0x39, 0xa8, 0xe7, 0x9a, 0x3b, 0x7a, 0x3c, 0xce, 0x5d, /* 0x00000390: )3...^.9...;z<.] */
    0x0f, 0x3c, 0x82, 0x90, 0xff, 0x81, 0x82, 0x00, 0x82, 0x5f, 0xba, 0x08, 0x79, 0xb1, 0x97, 0xc3, /* 0x000003a0: .<......._..y... */
    0x09, 0x75, 0xc0, 0x04, 0x9b, 0x67,                                                             /* 0x000003b0: .u...g           */
};

const unsigned char g_abFakeRsaKey[] =
{
    0x30, 0x82, 0x04, 0xa4, 0x02, 0x01, 0x00, 0x02, 0x82, 0x01, 0x01, 0x00, 0xdb, 0x18, 0x63, 0x33, /* 0x00000000: 0.............c3 */
    0xf2, 0x08, 0x90, 0x5a, 0xab, 0xda, 0x88, 0x73, 0x86, 0x49, 0xea, 0x8b, 0xaf, 0xcf, 0x67, 0x15, /* 0x00000010: ...Z...s.I....g. */
    0xa5, 0x39, 0xe6, 0xa2, 0x94, 0x0c, 0x3f, 0xa1, 0x2e, 0x6c, 0xd2, 0xdf, 0x01, 0x65, 0x6d, 0xed, /* 0x00000020: .9....?..l...em. */
    0x6c, 0x4c, 0xac, 0xe7, 0x77, 0x7a, 0x45, 0x05, 0x6b, 0x24, 0xf3, 0xaf, 0x45, 0x35, 0x6e, 0x64, /* 0x00000030: lL..wzE.k$..E5nd */
    0x0a, 0xac, 0x1d, 0x37, 0xe1, 0x33, 0xa4, 0x92, 0xec, 0x45, 0xe8, 0x99, 0xc1, 0xde, 0x6f, 0xab, /* 0x00000040: ...7.3...E....o. */
    0x7c, 0xf0, 0xdc, 0xe2, 0xc5, 0x42, 0xa3, 0xea, 0xf5, 0x8a, 0xf9, 0x0e, 0xe7, 0xb3, 0x35, 0xa2, /* 0x00000050: |....B........5. */
    0x75, 0x5e, 0x87, 0xd2, 0x2a, 0xd1, 0x27, 0xa6, 0x79, 0x9e, 0xfe, 0x90, 0xbf, 0x97, 0xa4, 0xa1, /* 0x00000060: u^..*.'.y....... */
    0xd8, 0xf7, 0xd7, 0x05, 0x59, 0x44, 0x27, 0x39, 0x6e, 0x33, 0x01, 0x2e, 0x46, 0x92, 0x47, 0xbe, /* 0x00000070: ....YD'9n3..F.G. */
    0x50, 0x91, 0x26, 0x27, 0xe5, 0x4b, 0x3a, 0x76, 0x26, 0x64, 0x92, 0x0c, 0xa0, 0x54, 0x43, 0x6f, /* 0x00000080: P.&'.K:v&d...TCo */
    0x56, 0xcc, 0x7b, 0xd0, 0xe3, 0xd8, 0x39, 0x5f, 0xb9, 0x41, 0xda, 0x1c, 0x62, 0x88, 0x0c, 0x45, /* 0x00000090: V.{...9_.A..b..E */
    0x03, 0x63, 0xf8, 0xff, 0xe5, 0x3e, 0x87, 0x0c, 0x75, 0xc9, 0xdd, 0xa2, 0xc0, 0x1b, 0x63, 0x19, /* 0x000000a0: .c...>..u.....c. */
    0xeb, 0x09, 0x9d, 0xa1, 0xbb, 0x0f, 0x63, 0x67, 0x1c, 0xa3, 0xfd, 0x2f, 0xd1, 0x2a, 0xda, 0xd8, /* 0x000000b0: ......cg.....*.. */
    0x93, 0x66, 0x45, 0x54, 0xef, 0x8b, 0x6d, 0x12, 0x15, 0x0f, 0xd4, 0xb5, 0x04, 0x17, 0x30, 0x5b, /* 0x000000c0: .fET..m.......0[ */
    0xfa, 0x12, 0x96, 0x48, 0x5b, 0x38, 0x65, 0xfd, 0x8f, 0x0c, 0xa3, 0x11, 0x46, 0x49, 0xe0, 0x62, /* 0x000000d0: ...H[8e.....FI.b */
    0xc3, 0xcc, 0x34, 0xe6, 0xfb, 0xab, 0x51, 0xc3, 0xd4, 0x0b, 0xdc, 0x39, 0x93, 0x87, 0x90, 0x10, /* 0x000000e0: ..4...Q....9.... */
    0x9f, 0xce, 0x43, 0x27, 0x31, 0xd5, 0x4e, 0x52, 0x60, 0xf1, 0x93, 0xd5, 0x06, 0xc4, 0x4e, 0x65, /* 0x000000f0: ..C'1.NR`.....Ne */
    0xb6, 0x35, 0x4a, 0x64, 0x15, 0xf8, 0xaf, 0x71, 0xb2, 0x42, 0x50, 0x89, 0x02, 0x03, 0x01, 0x00, /* 0x00000100: .5Jd...q.BP..... */
    0x01, 0x02, 0x82, 0x01, 0x01, 0x00, 0xd0, 0x5e, 0x09, 0x3a, 0xc5, 0xdc, 0xcf, 0x2c, 0xec, 0x74, /* 0x00000110: .......^.:...,.t */
    0x11, 0x81, 0x8d, 0x1d, 0x8f, 0x2a, 0xfa, 0x31, 0x4d, 0xe0, 0x90, 0x1a, 0xd8, 0xf5, 0x95, 0xc7, /* 0x00000120: .....*.1M....... */
    0x70, 0x5c, 0x62, 0x42, 0xac, 0xe9, 0xd9, 0xf2, 0x14, 0xf1, 0xd0, 0x25, 0xbb, 0xeb, 0x06, 0xfe, /* 0x00000130: p\bB.......%.... */
    0x09, 0xd6, 0x75, 0x67, 0xd7, 0x39, 0xc1, 0xa0, 0x67, 0x34, 0x4d, 0xd2, 0x12, 0x97, 0xaa, 0x5d, /* 0x00000140: ..ug.9..g4M....] */
    0xeb, 0x0e, 0xb0, 0x16, 0x6c, 0x78, 0x8e, 0xa0, 0x75, 0xa3, 0xaa, 0x57, 0x88, 0x3b, 0x43, 0x4f, /* 0x00000150: ....lx..u..W.;CO */
    0x75, 0x85, 0x67, 0xb0, 0x9b, 0xdd, 0x49, 0x0e, 0x6e, 0xdb, 0xea, 0xb3, 0xd4, 0x88, 0x54, 0xa0, /* 0x00000160: u.g...I.n.....T. */
    0x46, 0x0d, 0x55, 0x6d, 0x98, 0xbd, 0x20, 0xf9, 0x9f, 0x61, 0x2d, 0x6f, 0xc7, 0xd7, 0x16, 0x66, /* 0x00000170: F.Um.. ..a-o...f */
    0x72, 0xc7, 0x73, 0xbe, 0x9e, 0x48, 0xdc, 0x65, 0x12, 0x46, 0x35, 0x69, 0x55, 0xd8, 0x6b, 0x81, /* 0x00000180: r.s..H.e.F5iU.k. */
    0x78, 0x40, 0x15, 0x93, 0x60, 0x31, 0x4e, 0x87, 0x15, 0x2a, 0x74, 0x74, 0x7b, 0xa0, 0x1f, 0x59, /* 0x00000190: x@..`1N..*tt{..Y */
    0x8d, 0xc8, 0x3f, 0xdd, 0xf0, 0x13, 0x88, 0x2a, 0x4a, 0xf2, 0xf5, 0xf1, 0x9e, 0xf3, 0x2d, 0x9c, /* 0x000001a0: ..?....*J.....-. */
    0x8e, 0xbc, 0xb1, 0x21, 0x45, 0xc7, 0x44, 0x0c, 0x6a, 0xfe, 0x4c, 0x20, 0xdc, 0x73, 0xda, 0x62, /* 0x000001b0: ...!E.D.j.L .s.b */
    0x21, 0xcb, 0xdf, 0x06, 0xfc, 0x90, 0xc2, 0xbd, 0xd6, 0xde, 0xfb, 0xf6, 0x08, 0x69, 0x5d, 0xea, /* 0x000001c0: !............i]. */
    0xb3, 0x7f, 0x93, 0x61, 0xf2, 0xc1, 0xd0, 0x61, 0x4f, 0xd5, 0x5b, 0x63, 0xba, 0xb0, 0x3b, 0x07, /* 0x000001d0: ...a...aO.[c..;. */
    0x7a, 0x55, 0xcd, 0xa1, 0xae, 0x8a, 0x92, 0x21, 0xcc, 0x2f, 0x5b, 0xf8, 0x40, 0x6a, 0xcd, 0xd5, /* 0x000001e0: zU.....!..[.@j.. */
    0x5f, 0x15, 0xf4, 0xb6, 0xbd, 0xe5, 0x91, 0xb9, 0xa8, 0xcc, 0x2a, 0xa8, 0xa6, 0x67, 0x57, 0x2b, /* 0x000001f0: _.........*..gW+ */
    0x4b, 0xe9, 0x88, 0xe0, 0xbb, 0x58, 0xac, 0x69, 0x5f, 0x3c, 0x76, 0x28, 0xa6, 0x9d, 0xbc, 0x71, /* 0x00000200: K....X.i_<v(...q */
    0x7f, 0xcb, 0x0c, 0xc0, 0xbd, 0x61, 0x02, 0x81, 0x81, 0x00, 0xfc, 0x62, 0x79, 0x5b, 0xac, 0xf6, /* 0x00000210: .....a.....by[.. */
    0x9b, 0x8c, 0xaa, 0x76, 0x2a, 0x30, 0x0e, 0xcf, 0x6b, 0x88, 0x72, 0x54, 0x8c, 0xdf, 0xf3, 0x9d, /* 0x00000220: ...v*0..k.rT.... */
    0x84, 0xbb, 0xe7, 0x9d, 0xd4, 0x04, 0x29, 0x3c, 0xb5, 0x9d, 0x60, 0x9a, 0xcc, 0x12, 0xf3, 0xfa, /* 0x00000230: ......)<..`..... */
    0x64, 0x30, 0x23, 0x47, 0xc6, 0xa4, 0x8b, 0x6c, 0x73, 0x6c, 0x6b, 0x78, 0x82, 0xec, 0x05, 0x19, /* 0x00000240: d0#G...lslkx.... */
    0xde, 0xdd, 0xde, 0x52, 0xc5, 0x20, 0xd1, 0x11, 0x58, 0x19, 0x07, 0x5a, 0x90, 0xdd, 0x22, 0x91, /* 0x00000250: ...R. ..X..Z..". */
    0x89, 0x22, 0x3f, 0x12, 0x54, 0x1a, 0xb8, 0x79, 0xd8, 0x6c, 0xbc, 0xf5, 0x0d, 0xc7, 0x73, 0x5c, /* 0x00000260: ."?.T..y.l....s\ */
    0xed, 0xba, 0x40, 0x2b, 0x72, 0x34, 0x34, 0x97, 0xfa, 0x49, 0xf6, 0x43, 0x7c, 0xbc, 0x61, 0x30, /* 0x00000270: ..@+r44..I.C|.a0 */
    0x54, 0x22, 0x21, 0x5f, 0x77, 0x68, 0x6b, 0x83, 0x95, 0xc6, 0x8d, 0xb8, 0x25, 0x3a, 0xd3, 0xb2, /* 0x00000280: T"!_whk.....%:.. */
    0xbe, 0x29, 0x94, 0x01, 0x15, 0xf0, 0x36, 0x9d, 0x3e, 0xff, 0x02, 0x81, 0x81, 0x00, 0xde, 0x3b, /* 0x00000290: .)....6.>......; */
    0xd6, 0x4b, 0x38, 0x69, 0x9b, 0x71, 0x29, 0x89, 0xd4, 0x6d, 0x8c, 0x41, 0xee, 0xe2, 0x4d, 0xfc, /* 0x000002a0: .K8i.q)..m.A..M. */
    0xf0, 0x9a, 0x73, 0xf1, 0x15, 0x94, 0xac, 0x1b, 0x68, 0x5f, 0x79, 0x15, 0x3a, 0x41, 0x55, 0x09, /* 0x000002b0: ..s.....h_y.:AU. */
    0xc7, 0x1e, 0xec, 0x27, 0x67, 0xe2, 0xdc, 0x54, 0xa8, 0x09, 0xe6, 0x46, 0x92, 0x92, 0x03, 0x8d, /* 0x000002c0: ...'g..T...F.... */
    0xe5, 0x96, 0xfb, 0x1a, 0xdd, 0x59, 0x6f, 0x92, 0xf1, 0xf6, 0x8f, 0x76, 0xb0, 0xc5, 0xe6, 0xd7, /* 0x000002d0: .....Yo....v.... */
    0x1b, 0x25, 0xaf, 0x04, 0x9f, 0xd8, 0x71, 0x27, 0x97, 0x99, 0x23, 0x09, 0x7d, 0xef, 0x06, 0x13, /* 0x000002e0: .%....q'..#.}... */
    0xab, 0xdc, 0xa2, 0xd8, 0x5f, 0xc5, 0xec, 0xf3, 0x62, 0x20, 0x72, 0x7b, 0xa8, 0xc7, 0x09, 0x24, /* 0x000002f0: ...._...b r{...$ */
    0xaf, 0x72, 0xc9, 0xea, 0xb8, 0x2d, 0xda, 0x00, 0xc8, 0xfe, 0xb4, 0x9f, 0x9f, 0xc7, 0xa9, 0xf7, /* 0x00000300: .r...-.......... */
    0x1d, 0xce, 0xb1, 0xdb, 0xc5, 0x8a, 0x4e, 0xe8, 0x88, 0x77, 0x68, 0xdd, 0xf8, 0x77, 0x02, 0x81, /* 0x00000310: ......N..wh..w.. */
    0x80, 0x5b, 0xa5, 0x8e, 0x98, 0x01, 0xa8, 0xd3, 0x37, 0x33, 0x37, 0x11, 0x7e, 0xbe, 0x02, 0x07, /* 0x00000320: .[......737.~... */
    0xf4, 0x56, 0x3f, 0xe9, 0x9f, 0xf1, 0x20, 0xc3, 0xf0, 0x4f, 0xdc, 0xf9, 0xfe, 0x40, 0xd3, 0x30, /* 0x00000330: .V?... ..O...@.0 */
    0xc7, 0xe3, 0x2a, 0x92, 0xec, 0x56, 0xf8, 0x17, 0xa5, 0x7b, 0x4a, 0x37, 0x11, 0xcd, 0x27, 0x26, /* 0x00000340: ..*..V...{J7..'& */
    0x8a, 0xba, 0x43, 0xda, 0x96, 0xc6, 0x0b, 0x6c, 0xe8, 0x78, 0x30, 0xea, 0x30, 0x4e, 0x7a, 0xd3, /* 0x00000350: ..C....l.x0.0Nz. */
    0xd8, 0xd2, 0xd8, 0xca, 0x3d, 0xe2, 0xad, 0xa2, 0x74, 0x73, 0x1e, 0xbe, 0xb7, 0xad, 0x41, 0x61, /* 0x00000360: ....=...ts....Aa */
    0x9b, 0xaa, 0xc9, 0xf9, 0xa4, 0xf1, 0x79, 0x4f, 0x42, 0x10, 0xc7, 0x36, 0x03, 0x4b, 0x0d, 0xdc, /* 0x00000370: ......yOB..6.K.. */
    0xef, 0x3a, 0xa3, 0xab, 0x09, 0xe4, 0xe8, 0xdd, 0xc4, 0x3f, 0x06, 0x21, 0xa0, 0x23, 0x5a, 0x76, /* 0x00000380: .:.......?.!.#Zv */
    0xea, 0xd0, 0xcf, 0x8b, 0x85, 0x5f, 0x16, 0x4b, 0x03, 0x62, 0x21, 0x3a, 0xcc, 0x2d, 0xa8, 0xd0, /* 0x00000390: ....._.K.b!:.-.. */
    0x15, 0x02, 0x81, 0x80, 0x51, 0xf6, 0x89, 0xbb, 0xa6, 0x6b, 0xb4, 0xcb, 0xd0, 0xc1, 0x27, 0xda, /* 0x000003a0: ....Q....k....'. */
    0xdb, 0x6e, 0xf9, 0xd6, 0xf7, 0x62, 0x81, 0xae, 0xc5, 0x72, 0x36, 0x3e, 0x66, 0x17, 0x99, 0xb0, /* 0x000003b0: .n...b...r6>f... */
    0x14, 0xad, 0x52, 0x96, 0x03, 0xf2, 0x1e, 0x41, 0x76, 0x61, 0xb6, 0x3c, 0x02, 0x7d, 0x2a, 0x98, /* 0x000003c0: ..R....Ava.<.}*. */
    0xb4, 0x18, 0x75, 0x38, 0x6b, 0x1d, 0x2b, 0x7f, 0x3a, 0xcf, 0x96, 0xb1, 0xc4, 0xa7, 0xd2, 0x9b, /* 0x000003d0: ..u8k.+.:....... */
    0xd8, 0x1f, 0xb3, 0x64, 0xda, 0x15, 0x9d, 0xca, 0x91, 0x39, 0x48, 0x67, 0x00, 0x9c, 0xd4, 0x99, /* 0x000003e0: ...d.....9Hg.... */
    0xc3, 0x45, 0x5d, 0xf0, 0x09, 0x32, 0xba, 0x21, 0x1e, 0xe2, 0x64, 0xb8, 0x50, 0x03, 0x17, 0xbe, /* 0x000003f0: .E]..2.!..d.P... */
    0xd5, 0xda, 0x6b, 0xce, 0x34, 0xbe, 0x16, 0x03, 0x65, 0x1b, 0x2f, 0xa0, 0xa1, 0x95, 0xc6, 0x8b, /* 0x00000400: ..k.4...e....... */
    0xc2, 0x3c, 0x59, 0x26, 0xbf, 0xb6, 0x07, 0x85, 0x53, 0x2d, 0xb6, 0x36, 0xa3, 0x91, 0xb9, 0xbb, /* 0x00000410: .<Y&....S-.6.... */
    0x28, 0xaf, 0x2d, 0x53, 0x02, 0x81, 0x81, 0x00, 0xd7, 0xbc, 0x70, 0xd8, 0x18, 0x4f, 0x65, 0x8c, /* 0x00000420: (.-S......p..Oe. */
    0x68, 0xca, 0x35, 0x77, 0x43, 0x50, 0x9b, 0xa1, 0xa3, 0x9a, 0x0e, 0x2d, 0x7b, 0x38, 0xf8, 0xba, /* 0x00000430: h.5wCP.....-{8.. */
    0x14, 0x91, 0x3b, 0xc3, 0x3b, 0x1b, 0xa0, 0x6d, 0x45, 0xe4, 0xa8, 0x28, 0x97, 0xf6, 0x89, 0x13, /* 0x00000440: ..;.;..mE..(.... */
    0xb6, 0x16, 0x6d, 0x65, 0x47, 0x8c, 0xa6, 0x21, 0xf8, 0x6a, 0xce, 0x4e, 0x44, 0x5e, 0x81, 0x47, /* 0x00000450: ..meG..!.j.ND^.G */
    0xd9, 0xad, 0x8a, 0xb9, 0xd9, 0xe9, 0x3e, 0x33, 0x1e, 0x5f, 0xe9, 0xe9, 0xa7, 0xea, 0x60, 0x75, /* 0x00000460: ......>3._....`u */
    0x02, 0x57, 0x71, 0xb5, 0xed, 0x47, 0x77, 0xda, 0x1a, 0x40, 0x38, 0xab, 0x82, 0xd2, 0x0d, 0xf5, /* 0x00000470: .Wq..Gw..@8..... */
    0x0e, 0x8e, 0xa9, 0x24, 0xdc, 0x30, 0xc9, 0x98, 0xa2, 0x05, 0xcd, 0xca, 0x01, 0xcf, 0xae, 0x1d, /* 0x00000480: ...$.0.......... */
    0xe9, 0x02, 0x47, 0x0e, 0x46, 0x1d, 0x52, 0x02, 0x9a, 0x99, 0x22, 0x23, 0x7f, 0xf8, 0x9e, 0xc2, /* 0x00000490: ..G.F.R..."#.... */
    0x16, 0x86, 0xca, 0xa0, 0xa7, 0x34, 0xfb, 0xbc,                                                 /* 0x000004a0: .....4..         */
};

#endif /* RT_OS_WINDOWS */


/**
 * Certificate w/ public key + private key pair for signing.
 */
class SignToolKeyPair
{
protected:
    /* Context: */
    const char             *m_pszWhat;
    bool                    m_fMandatory;

    /* Parameters kept till finalizing parsing: */
    const char             *m_pszCertFile;
    const char             *m_pszCertSha1;
    uint8_t                 m_abCertSha1[RTSHA1_HASH_SIZE];
    const char             *m_pszCertSubject;
    const char             *m_pszCertStore;
    bool                    m_fMachineStore; /**< false = personal store */

    const char             *m_pszKeyFile;
    const char             *m_pszKeyPassword;
    const char             *m_pszKeyName;
    const char             *m_pszKeyProvider;

    /** String buffer for m_pszKeyPassword when read from file. */
    RTCString               m_strPassword;
    /** Storage for pCertificate when it's loaded from a file. */
    RTCRX509CERTIFICATE     m_DecodedCert;
#ifdef RT_OS_WINDOWS
    /** For the fake certificate */
    RTCRX509CERTIFICATE     m_DecodedFakeCert;
    /** The certificate store. */
    HCERTSTORE              m_hStore;
    /** The windows certificate context. */
    PCCERT_CONTEXT          m_pCertCtx;
    /** Whether hNCryptPrivateKey/hLegacyPrivateKey needs freeing or not. */
    BOOL                    m_fFreePrivateHandle;
#endif

    /** Set if already finalized. */
    bool                    m_fFinalized;

    /** Store containing the intermediate certificates available to the host.
     *   */
    static RTCRSTORE        s_hStoreIntermediate;
    /** Instance counter for helping cleaning up m_hStoreIntermediate. */
    static uint32_t         s_cInstances;

public: /* used to be a struct, thus not prefix either. */
    /* Result: */
    PCRTCRX509CERTIFICATE   pCertificate;
    RTCRKEY                 hPrivateKey;
#ifdef RT_OS_WINDOWS
    PCRTCRX509CERTIFICATE   pCertificateReal;
    NCRYPT_KEY_HANDLE       hNCryptPrivateKey;
    HCRYPTPROV              hLegacyPrivateKey;
#endif

public:
    SignToolKeyPair(const char *a_pszWhat, bool a_fMandatory = false)
        : m_pszWhat(a_pszWhat)
        , m_fMandatory(a_fMandatory)
        , m_pszCertFile(NULL)
        , m_pszCertSha1(NULL)
        , m_pszCertSubject(NULL)
        , m_pszCertStore("MY")
        , m_fMachineStore(false)
        , m_pszKeyFile(NULL)
        , m_pszKeyPassword(NULL)
        , m_pszKeyName(NULL)
        , m_pszKeyProvider(NULL)
#ifdef RT_OS_WINDOWS
        , m_hStore(NULL)
        , m_pCertCtx(NULL)
        , m_fFreePrivateHandle(FALSE)
#endif
        , m_fFinalized(false)
        , pCertificate(NULL)
        , hPrivateKey(NIL_RTCRKEY)
#ifdef RT_OS_WINDOWS
        , pCertificateReal(NULL)
        , hNCryptPrivateKey(0)
        , hLegacyPrivateKey(0)
#endif
    {
        RT_ZERO(m_DecodedCert);
#ifdef RT_OS_WINDOWS
        RT_ZERO(m_DecodedFakeCert);
#endif
        s_cInstances++;
    }

    virtual ~SignToolKeyPair()
    {
        if (hPrivateKey != NIL_RTCRKEY)
        {
            RTCrKeyRelease(hPrivateKey);
            hPrivateKey = NIL_RTCRKEY;
        }
        if (pCertificate == &m_DecodedCert)
        {
            RTCrX509Certificate_Delete(&m_DecodedCert);
            pCertificate = NULL;
        }
#ifdef RT_OS_WINDOWS
        if (pCertificate == &m_DecodedFakeCert)
        {
            RTCrX509Certificate_Delete(&m_DecodedFakeCert);
            RTCrX509Certificate_Delete(&m_DecodedCert);
            pCertificate = NULL;
            pCertificateReal = NULL;
        }
#endif
#ifdef RT_OS_WINDOWS
        if (m_pCertCtx != NULL)
        {
             CertFreeCertificateContext(m_pCertCtx);
             m_pCertCtx = NULL;
        }
        if (m_hStore != NULL)
        {
            CertCloseStore(m_hStore, 0);
            m_hStore = NULL;
        }
#endif
        s_cInstances--;
        if (s_cInstances == 0)
        {
            RTCrStoreRelease(s_hStoreIntermediate);
            s_hStoreIntermediate = NIL_RTCRSTORE;
        }
    }

    bool isComplete(void) const
    {
        return pCertificate && hPrivateKey != NIL_RTCRKEY;
    }

    bool isNull(void) const
    {
        return pCertificate == NULL && hPrivateKey == NIL_RTCRKEY;
    }

    RTEXITCODE handleOption(unsigned offOpt, PRTGETOPTUNION pValueUnion)
    {
        AssertReturn(!m_fFinalized, RTMsgErrorExitFailure("Cannot handle options after finalizeOptions was called!"));
        switch (offOpt)
        {
            case OPT_OFF_CERT_FILE:
                m_pszCertFile    = pValueUnion->psz;
                m_pszCertSha1    = NULL;
                m_pszCertSubject = NULL;
                break;
            case OPT_OFF_CERT_SHA1:
            {
                /* Crude normalization of input separators to colons, since it's likely
                   to use spaces and our conversion function only does colons or nothing. */
                char szDigest[RTSHA1_DIGEST_LEN * 3 + 1];
                int rc = RTStrCopy(szDigest, sizeof(szDigest), pValueUnion->psz);
                if (RT_SUCCESS(rc))
                {
                    char  *pszDigest = RTStrStrip(szDigest);
                    size_t offDst    = 0;
                    size_t offSrc    = 0;
                    char   ch;
                    while ((ch = pszDigest[offSrc++]) != '\0')
                    {
                        if (ch == ' ' || ch == '\t' || ch == ':')
                        {
                            while ((ch = pszDigest[offSrc]) == ' ' || ch == '\t' || ch == ':')
                                offSrc++;
                            ch = ch ? ':' : '\0';
                        }
                        pszDigest[offDst++] = ch;
                    }
                    pszDigest[offDst] = '\0';

                    /** @todo add a more relaxed input mode to RTStrConvertHexBytes that can deal
                     *        with spaces as well as multi-byte cluster of inputs. */
                    rc = RTStrConvertHexBytes(pszDigest, m_abCertSha1, RTSHA1_HASH_SIZE, RTSTRCONVERTHEXBYTES_F_SEP_COLON);
                    if (RT_SUCCESS(rc))
                    {
                        m_pszCertFile    = NULL;
                        m_pszCertSha1    = pValueUnion->psz;
                        m_pszCertSubject = NULL;
                        break;
                    }
                }
                return RTMsgErrorExitFailure("malformed SHA-1 certificate fingerprint (%Rrc): %s", rc, pValueUnion->psz);
            }
            case OPT_OFF_CERT_SUBJECT:
                m_pszCertFile    = NULL;
                m_pszCertSha1    = NULL;
                m_pszCertSubject = pValueUnion->psz;
                break;
            case OPT_OFF_CERT_STORE:
                m_pszCertStore   = pValueUnion->psz;
                break;
            case OPT_OFF_CERT_STORE_MACHINE:
                m_fMachineStore  = true;
                break;

            case OPT_OFF_KEY_FILE:
                m_pszKeyFile     = pValueUnion->psz;
                m_pszKeyName     = NULL;
                break;
            case OPT_OFF_KEY_NAME:
                m_pszKeyFile     = NULL;
                m_pszKeyName     = pValueUnion->psz;
                break;
            case OPT_OFF_KEY_PROVIDER:
                m_pszKeyProvider = pValueUnion->psz;
                break;
            case OPT_OFF_KEY_PASSWORD:
                m_pszKeyPassword = pValueUnion->psz;
                break;
            case OPT_OFF_KEY_PASSWORD_FILE:
            {
                m_pszKeyPassword = NULL;

                size_t const cchMax = 512;
                int rc = m_strPassword.reserveNoThrow(cchMax + 1);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExitFailure("out of memory");

                PRTSTREAM  pStrm  = g_pStdIn;
                bool const fClose = strcmp(pValueUnion->psz, "stdin") != 0;
                if (fClose)
                {
                    rc = RTStrmOpen(pValueUnion->psz, "r", &pStrm);
                    if (RT_FAILURE(rc))
                        return RTMsgErrorExitFailure("Failed to open password file '%s' for reading: %Rrc", pValueUnion->psz, rc);
                }
                rc = RTStrmGetLine(pStrm, m_strPassword.mutableRaw(), cchMax);
                if (fClose)
                    RTStrmClose(pStrm);
                if (rc == VERR_BUFFER_OVERFLOW || rc == VINF_BUFFER_OVERFLOW)
                    return RTMsgErrorExitFailure("Password from '%s' is too long (max %zu)", pValueUnion->psz, cchMax);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExitFailure("Error reading password from '%s': %Rrc", pValueUnion->psz, rc);

                m_strPassword.jolt();
                m_strPassword.stripRight();
                m_pszKeyPassword = m_strPassword.c_str();
                break;
            }
            default:
                AssertFailedReturn(RTMsgErrorExitFailure("Invalid offOpt=%u!\n", offOpt));
        }
        return RTEXITCODE_SUCCESS;
    }

    RTEXITCODE finalizeOptions(unsigned cVerbosity)
    {
        RT_NOREF(cVerbosity);

        /* Only do this once. */
        if (m_fFinalized)
            return RTEXITCODE_SUCCESS;
        m_fFinalized = true;

        /*
         * Got a cert? Is it required?
         */
        bool const fHasKey  = (   m_pszKeyFile     != NULL
                               || m_pszKeyName     != NULL);
        bool const fHasCert = (   m_pszCertFile    != NULL
                               || m_pszCertSha1    != NULL
                               || m_pszCertSubject != NULL);
        if (!fHasCert)
        {
            if (m_fMandatory)
                return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Specifying a %s certificiate is required.", m_pszWhat);
            return RTEXITCODE_SUCCESS;
        }

        /*
         * Get the certificate.
         */
        RTERRINFOSTATIC ErrInfo;
        /* From file: */
        if (m_pszCertFile)
        {
            int rc = RTCrX509Certificate_ReadFromFile(&m_DecodedCert, m_pszCertFile, 0, &g_RTAsn1DefaultAllocator,
                                                      RTErrInfoInitStatic(&ErrInfo));
            if (RT_FAILURE(rc))
                return RTMsgErrorExitFailure("Error reading %s certificate from '%s': %Rrc%#RTeim",
                                             m_pszWhat, m_pszCertFile, rc, &ErrInfo.Core);
            pCertificate = &m_DecodedCert;
        }
        /* From certificate store by name (substring) or fingerprint: */
        else
        {
#ifdef RT_OS_WINDOWS
            m_hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM_A, X509_ASN_ENCODING, NULL,
                                     CERT_STORE_DEFER_CLOSE_UNTIL_LAST_FREE_FLAG | CERT_STORE_READONLY_FLAG
                                     | CERT_STORE_OPEN_EXISTING_FLAG | CERT_STORE_ENUM_ARCHIVED_FLAG
                                     | (m_fMachineStore ? CERT_SYSTEM_STORE_LOCAL_MACHINE : CERT_SYSTEM_STORE_CURRENT_USER),
                                     m_pszCertStore);
            if (m_hStore == NULL)
                return RTMsgErrorExitFailure("Failed to open %s store '%s': %Rwc (%u)", m_fMachineStore ? "machine" : "user",
                                             m_pszCertStore, GetLastError(), GetLastError());

            CRYPT_HASH_BLOB Thumbprint  = { RTSHA1_HASH_SIZE, m_abCertSha1 };
            PRTUTF16        pwszSubject = NULL;
            void const     *pvFindParam = &Thumbprint;
            DWORD           fFind       = CERT_FIND_SHA1_HASH;
            if (!m_pszCertSha1)
            {
                int rc = RTStrToUtf16(m_pszCertSubject, &pwszSubject);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExitFailure("RTStrToUtf16 failed: %Rrc, input %.*Rhxs",
                                                 rc, strlen(m_pszCertSubject), m_pszCertSubject);
                pvFindParam = pwszSubject;
                fFind       = CERT_FIND_SUBJECT_STR;
            }

            while ((m_pCertCtx = CertFindCertificateInStore(m_hStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0 /*fFlags*/,
                                                           fFind, pvFindParam, m_pCertCtx)) != NULL)
            {
                if (m_pCertCtx->dwCertEncodingType & X509_ASN_ENCODING)
                {
                    RTASN1CURSORPRIMARY PrimaryCursor;
                    RTAsn1CursorInitPrimary(&PrimaryCursor, m_pCertCtx->pbCertEncoded, m_pCertCtx->cbCertEncoded,
                                            RTErrInfoInitStatic(&ErrInfo),
                                            &g_RTAsn1DefaultAllocator, RTASN1CURSOR_FLAGS_DER, "CurCtx");
                    int rc = RTCrX509Certificate_DecodeAsn1(&PrimaryCursor.Cursor, 0, &m_DecodedCert, "Cert");
                    if (RT_SUCCESS(rc))
                    {
                        pCertificate = &m_DecodedCert;
                        break;
                    }
                    RTMsgError("failed to decode certificate %p: %Rrc%#RTeim", m_pCertCtx, rc, &ErrInfo.Core);
                }
            }

            RTUtf16Free(pwszSubject);
            if (!m_pCertCtx)
                return RTMsgErrorExitFailure("No certificate found matching %s '%s' (%Rwc / %u)",
                                             m_pszCertSha1 ? "thumbprint" : "subject substring",
                                             m_pszCertSha1 ? m_pszCertSha1 : m_pszCertSubject, GetLastError(), GetLastError());

            /* Use this for private key too? */
            if (!fHasKey)
            {
                HCRYPTPROV_OR_NCRYPT_KEY_HANDLE hTmpPrivateKey = 0;
                DWORD                           dwKeySpec      = 0;
                if (CryptAcquireCertificatePrivateKey(m_pCertCtx,
                                                      CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_COMPARE_KEY_FLAG
                                                      | CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG
                                                      | CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG,
                                                      NULL, &hTmpPrivateKey, &dwKeySpec, &m_fFreePrivateHandle))
                {
                    if (cVerbosity > 1)
                        RTMsgInfo("hTmpPrivateKey=%p m_fFreePrivateHandle=%d dwKeySpec=%#x",
                                  hTmpPrivateKey, m_fFreePrivateHandle, dwKeySpec);
                    Assert(dwKeySpec == CERT_NCRYPT_KEY_SPEC);
                    if (dwKeySpec == CERT_NCRYPT_KEY_SPEC)
                        hNCryptPrivateKey = hTmpPrivateKey;
                    else
                        hLegacyPrivateKey = hTmpPrivateKey;   /** @todo remove or drop CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG */
                    return loadFakePrivateKeyAndCert();
                }
                return RTMsgErrorExitFailure("CryptAcquireCertificatePrivateKey failed: %Rwc (%d)", GetLastError(), GetLastError());
            }
#else
            return RTMsgErrorExitFailure("Certificate store support is missing on this host");
#endif
        }

        /*
         * Get hold of the private key (if someone above already did, they'd returned already).
         */
        Assert(hPrivateKey == NIL_RTCRKEY);
        /* Use cert file if nothing else specified. */
        if (!fHasKey && m_pszCertFile)
            m_pszKeyFile = m_pszCertFile;

        /* Load from file:*/
        if (m_pszKeyFile)
        {
            int rc = RTCrKeyCreateFromFile(&hPrivateKey, 0 /*fFlags*/, m_pszKeyFile, m_pszKeyPassword,
                                           RTErrInfoInitStatic(&ErrInfo));
            if (RT_FAILURE(rc))
                return RTMsgErrorExitFailure("Error reading the %s private key from '%s': %Rrc%#RTeim",
                                             m_pszWhat, m_pszKeyFile, rc, &ErrInfo.Core);
        }
        /* From key store: */
        else
        {
            return RTMsgErrorExitFailure("Key store support is missing on this host");
        }

        return RTEXITCODE_SUCCESS;
    }

    /** Returns the real certificate. */
    PCRTCRX509CERTIFICATE getRealCertificate() const
    {
#ifdef RT_OS_WINDOWS
        if (pCertificateReal)
            return pCertificateReal;
#endif
        return pCertificate;
    }

#ifdef RT_OS_WINDOWS
    RTEXITCODE loadFakePrivateKeyAndCert()
    {
        int rc = RTCrX509Certificate_ReadFromBuffer(&m_DecodedFakeCert, g_abFakeCertificate, sizeof(g_abFakeCertificate),
                                                    0 /*fFlags*/, &g_RTAsn1DefaultAllocator, NULL, NULL);
        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("RTCrX509Certificate_ReadFromBuffer/g_abFakeCertificate failed: %Rrc", rc);
        pCertificateReal = pCertificate;
        pCertificate = &m_DecodedFakeCert;

        rc = RTCrKeyCreateFromBuffer(&hPrivateKey, 0 /*fFlags*/, g_abFakeRsaKey, sizeof(g_abFakeRsaKey), NULL, NULL, NULL);
        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("RTCrKeyCreateFromBuffer/g_abFakeRsaKey failed: %Rrc", rc);
        return RTEXITCODE_SUCCESS;
    }

#endif

    /**
     * Search for intermediate CA.
     *
     * Currently this only do a single certificate path, so this may go south if
     * there are multiple paths available.  It may work fine for a cross signing
     * path, as long as the cross over is at the level immediately below the root.
     */
    PCRTCRCERTCTX findNextIntermediateCert(PCRTCRCERTCTX pPrev)
    {
        /*
         * Make sure the store is loaded before we start.
         */
        if (s_hStoreIntermediate == NIL_RTCRSTORE)
        {
            Assert(!pPrev);
            RTERRINFOSTATIC ErrInfo;
            int rc = RTCrStoreCreateSnapshotById(&s_hStoreIntermediate,
                                                 !m_fMachineStore
                                                 ? RTCRSTOREID_USER_INTERMEDIATE_CAS : RTCRSTOREID_SYSTEM_INTERMEDIATE_CAS,
                                                 RTErrInfoInitStatic(&ErrInfo));
            if (RT_FAILURE(rc))
            {
                RTMsgError("RTCrStoreCreateSnapshotById/%s-intermediate-CAs failed: %Rrc%#RTeim",
                           m_fMachineStore ? "user" : "machine", rc, &ErrInfo.Core);
                return NULL;
            }
        }

        /*
         * Open the search handle for the parent of the previous/end certificate.
         *
         * We don't need to consider RTCRCERTCTX::pTaInfo here as we're not
         * after trust anchors, only intermediate certificates.
         */
#ifdef RT_OS_WINDOWS
        PCRTCRX509CERTIFICATE pChildCert = pPrev ? pPrev->pCert : pCertificateReal ? pCertificateReal : pCertificate;
#else
        PCRTCRX509CERTIFICATE pChildCert = pPrev ? pPrev->pCert : pCertificate;
#endif
        AssertReturnStmt(pChildCert, RTCrCertCtxRelease(pPrev), NULL);

        RTCRSTORECERTSEARCH Search;
        int rc = RTCrStoreCertFindBySubjectOrAltSubjectByRfc5280(s_hStoreIntermediate, &pChildCert->TbsCertificate.Issuer,
                                                                 &Search);
        if (RT_FAILURE(rc))
        {
            RTMsgError("RTCrStoreCertFindBySubjectOrAltSubjectByRfc5280 failed: %Rrc", rc);
            return NULL;
        }

        /*
         * We only gave the subject so, we have to check the serial number our selves.
         */
        PCRTCRCERTCTX pCertCtx;
        while ((pCertCtx = RTCrStoreCertSearchNext(s_hStoreIntermediate, &Search)) != NULL)
        {
            if (   pCertCtx->pCert
                && RTAsn1BitString_Compare(&pCertCtx->pCert->TbsCertificate.T1.IssuerUniqueId,
                                           &pChildCert->TbsCertificate.T1.IssuerUniqueId) == 0 /* compares presentness too */
                && !RTCrX509Certificate_IsSelfSigned(pCertCtx->pCert))
            {
                break; /** @todo compare valid periode too and keep a best match when outside the desired period? */
            }
            RTCrCertCtxRelease(pCertCtx);
        }

        RTCrStoreCertSearchDestroy(s_hStoreIntermediate, & Search);
        RTCrCertCtxRelease(pPrev);
        return pCertCtx;
    }

    /**
     * Merges the user specified certificates with the signing certificate and any
     * intermediate CAs we can find in the system store.
     *
     * @returns Merged store, NIL_RTCRSTORE on failure (messaged).
     * @param   hUserSpecifiedCertificates  The user certificate store.
     */
    RTCRSTORE assembleAllAdditionalCertificates(RTCRSTORE hUserSpecifiedCertificates)
    {
        RTCRSTORE hRetStore;
        int rc = RTCrStoreCreateInMemEx(&hRetStore, 0, hUserSpecifiedCertificates);
        if (RT_SUCCESS(rc))
        {
            /* Add the signing certificate: */
            RTERRINFOSTATIC ErrInfo;
            rc = RTCrStoreCertAddX509(hRetStore, RTCRCERTCTX_F_ENC_X509_DER | RTCRCERTCTX_F_ADD_IF_NOT_FOUND,
#ifdef RT_OS_WINDOWS
                                      (PRTCRX509CERTIFICATE)(pCertificateReal ? pCertificateReal : pCertificate),
#else
                                      (PRTCRX509CERTIFICATE)pCertificate,
#endif
                                      RTErrInfoInitStatic(&ErrInfo));
            if (RT_SUCCESS(rc))
            {
                /* Add all intermediate CAs certificates we can find. */
                PCRTCRCERTCTX pInterCaCert = NULL;
                while ((pInterCaCert = findNextIntermediateCert(pInterCaCert)) != NULL)
                {
                    rc = RTCrStoreCertAddEncoded(hRetStore, RTCRCERTCTX_F_ENC_X509_DER | RTCRCERTCTX_F_ADD_IF_NOT_FOUND,
                                                 pInterCaCert->pabEncoded, pInterCaCert->cbEncoded,
                                                 RTErrInfoInitStatic(&ErrInfo));
                    if (RT_FAILURE(rc))
                    {
                        RTMsgError("RTCrStoreCertAddEncoded/InterCA failed: %Rrc%#RTeim", rc, &ErrInfo.Core);
                        RTCrCertCtxRelease(pInterCaCert);
                        break;
                    }
                }
                if (RT_SUCCESS(rc))
                    return hRetStore;
            }
            else
                RTMsgError("RTCrStoreCertAddX509/signer failed: %Rrc%#RTeim", rc, &ErrInfo.Core);
            RTCrStoreRelease(hRetStore);
        }
        else
            RTMsgError("RTCrStoreCreateInMemEx failed: %Rrc", rc);
        return NIL_RTCRSTORE;
    }

};

/*static*/ RTCRSTORE SignToolKeyPair::s_hStoreIntermediate = NIL_RTCRSTORE;
/*static*/ uint32_t  SignToolKeyPair::s_cInstances         = 0;


/*********************************************************************************************************************************
*
*********************************************************************************************************************************/
/** Timestamp type. */
typedef enum
{
    /** Old timestamp style.
     * This is just a counter signature with a trustworthy SigningTime attribute.
     * Specificially it's the SignerInfo part of a detached PKCS#7 covering the
     * SignerInfo.EncryptedDigest. */
    kTimestampType_Old = 1,
    /** This is a whole PKCS#7 signature of an TSTInfo from RFC-3161 (see page 7).
     * Currently not supported.  */
    kTimestampType_New
} TIMESTAMPTYPE;

/**
 * Timestamping options.
 *
 * Certificate w/ public key + private key pair for signing and signature type.
 */
class SignToolTimestampOpts : public SignToolKeyPair
{
public:
    /** Type timestamp type. */
    TIMESTAMPTYPE   m_enmType;

    SignToolTimestampOpts(const char *a_pszWhat, TIMESTAMPTYPE a_enmType = kTimestampType_Old)
        : SignToolKeyPair(a_pszWhat)
        , m_enmType(a_enmType)
    {
    }

    bool isOldType() const { return m_enmType == kTimestampType_Old; }
    bool isNewType() const { return m_enmType == kTimestampType_New; }
};



/*********************************************************************************************************************************
*   Crypto Store Auto Cleanup Wrapper.                                                                                           *
*********************************************************************************************************************************/
class CryptoStore
{
public:
    RTCRSTORE m_hStore;

    CryptoStore()
        : m_hStore(NIL_RTCRSTORE)
    {
    }

    ~CryptoStore()
    {
        if (m_hStore != NIL_RTCRSTORE)
        {
            uint32_t cRefs = RTCrStoreRelease(m_hStore);
            Assert(cRefs == 0); RT_NOREF(cRefs);
            m_hStore = NIL_RTCRSTORE;
        }
    }

    /**
     * Adds one or more certificates from the given file.
     *
     * @returns boolean success indicator.
     */
    bool addFromFile(const char *pszFilename, PRTERRINFOSTATIC pStaticErrInfo)
    {
        int rc = RTCrStoreCertAddFromFile(this->m_hStore, RTCRCERTCTX_F_ADD_IF_NOT_FOUND | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR,
                                          pszFilename, RTErrInfoInitStatic(pStaticErrInfo));
        if (RT_SUCCESS(rc))
        {
            if (RTErrInfoIsSet(&pStaticErrInfo->Core))
                RTMsgWarning("Warnings loading certificate '%s': %s", pszFilename, pStaticErrInfo->Core.pszMsg);
            return true;
        }
        RTMsgError("Error loading certificate '%s': %Rrc%#RTeim", pszFilename, rc, &pStaticErrInfo->Core);
        return false;
    }

    /**
     * Adds trusted self-signed certificates from the system.
     *
     * @returns boolean success indicator.
     * @note The selection is self-signed rather than CAs here so that test signing
     *       certificates will be included.
     */
    bool addSelfSignedRootsFromSystem(PRTERRINFOSTATIC pStaticErrInfo)
    {
        CryptoStore Tmp;
        int rc = RTCrStoreCreateSnapshotOfUserAndSystemTrustedCAsAndCerts(&Tmp.m_hStore, RTErrInfoInitStatic(pStaticErrInfo));
        if (RT_SUCCESS(rc))
        {
            RTCRSTORECERTSEARCH Search;
            rc = RTCrStoreCertFindAll(Tmp.m_hStore, &Search);
            if (RT_SUCCESS(rc))
            {
                PCRTCRCERTCTX pCertCtx;
                while ((pCertCtx = RTCrStoreCertSearchNext(Tmp.m_hStore, &Search)) != NULL)
                {
                    /* Add it if it's a full fledged self-signed certificate, otherwise just skip: */
                    if (   pCertCtx->pCert
                        && RTCrX509Certificate_IsSelfSigned(pCertCtx->pCert))
                    {
                        int rc2 = RTCrStoreCertAddEncoded(this->m_hStore,
                                                          pCertCtx->fFlags | RTCRCERTCTX_F_ADD_IF_NOT_FOUND,
                                                          pCertCtx->pabEncoded, pCertCtx->cbEncoded, NULL);
                        if (RT_FAILURE(rc2))
                            RTMsgWarning("RTCrStoreCertAddEncoded failed for a certificate: %Rrc", rc2);
                    }
                    RTCrCertCtxRelease(pCertCtx);
                }

                int rc2 = RTCrStoreCertSearchDestroy(Tmp.m_hStore, &Search);
                AssertRC(rc2);
                return true;
            }
            RTMsgError("RTCrStoreCertFindAll failed: %Rrc", rc);
        }
        else
            RTMsgError("RTCrStoreCreateSnapshotOfUserAndSystemTrustedCAsAndCerts failed: %Rrc%#RTeim", rc, &pStaticErrInfo->Core);
        return false;
    }

    /**
     * Adds trusted self-signed certificates from the system.
     *
     * @returns boolean success indicator.
     */
    bool addIntermediateCertsFromSystem(PRTERRINFOSTATIC pStaticErrInfo)
    {
        bool fRc = true;
        RTCRSTOREID const s_aenmStoreIds[] = { RTCRSTOREID_SYSTEM_INTERMEDIATE_CAS, RTCRSTOREID_USER_INTERMEDIATE_CAS };
        for (size_t i = 0; i < RT_ELEMENTS(s_aenmStoreIds); i++)
        {
            CryptoStore Tmp;
            int rc = RTCrStoreCreateSnapshotById(&Tmp.m_hStore, s_aenmStoreIds[i], RTErrInfoInitStatic(pStaticErrInfo));
            if (RT_SUCCESS(rc))
            {
                RTCRSTORECERTSEARCH Search;
                rc = RTCrStoreCertFindAll(Tmp.m_hStore, &Search);
                if (RT_SUCCESS(rc))
                {
                    PCRTCRCERTCTX pCertCtx;
                    while ((pCertCtx = RTCrStoreCertSearchNext(Tmp.m_hStore, &Search)) != NULL)
                    {
                        /* Skip selfsigned certs as they're useless as intermediate certs (IIRC). */
                        if (   pCertCtx->pCert
                            && !RTCrX509Certificate_IsSelfSigned(pCertCtx->pCert))
                        {
                            int rc2 = RTCrStoreCertAddEncoded(this->m_hStore,
                                                              pCertCtx->fFlags | RTCRCERTCTX_F_ADD_IF_NOT_FOUND,
                                                              pCertCtx->pabEncoded, pCertCtx->cbEncoded, NULL);
                            if (RT_FAILURE(rc2))
                                RTMsgWarning("RTCrStoreCertAddEncoded failed for a certificate: %Rrc", rc2);
                        }
                        RTCrCertCtxRelease(pCertCtx);
                    }

                    int rc2 = RTCrStoreCertSearchDestroy(Tmp.m_hStore, &Search);
                    AssertRC(rc2);
                }
                else
                {
                    RTMsgError("RTCrStoreCertFindAll/%d failed: %Rrc", s_aenmStoreIds[i], rc);
                    fRc = false;
                }
            }
            else
            {
                RTMsgError("RTCrStoreCreateSnapshotById/%d failed: %Rrc%#RTeim", s_aenmStoreIds[i], rc, &pStaticErrInfo->Core);
                fRc = false;
            }
        }
        return fRc;
    }

};



/*********************************************************************************************************************************
*   Workers.                                                                                                                     *
*********************************************************************************************************************************/


/**
 * Deletes the structure.
 *
 * @param   pThis               The structure to initialize.
 */
static void SignToolPkcs7_Delete(PSIGNTOOLPKCS7 pThis)
{
    RTCrPkcs7ContentInfo_Delete(&pThis->ContentInfo);
    pThis->pSignedData = NULL;
    RTMemFree(pThis->pbBuf);
    pThis->pbBuf       = NULL;
    pThis->cbBuf       = 0;
    RTMemFree(pThis->pbNewBuf);
    pThis->pbNewBuf    = NULL;
    pThis->cbNewBuf    = 0;
}


/**
 * Deletes the structure.
 *
 * @param   pThis               The structure to initialize.
 */
static void SignToolPkcs7Exe_Delete(PSIGNTOOLPKCS7EXE pThis)
{
    if (pThis->hLdrMod != NIL_RTLDRMOD)
    {
        int rc2 = RTLdrClose(pThis->hLdrMod);
        if (RT_FAILURE(rc2))
            RTMsgError("RTLdrClose failed: %Rrc\n", rc2);
        pThis->hLdrMod = NIL_RTLDRMOD;
    }
    SignToolPkcs7_Delete(pThis);
}


/**
 * Decodes the PKCS #7 blob pointed to by pThis->pbBuf.
 *
 * @returns IPRT status code (error message already shown on failure).
 * @param   pThis               The PKCS\#7 signature to decode.
 * @param   fCatalog            Set if catalog file, clear if executable.
 */
static int SignToolPkcs7_Decode(PSIGNTOOLPKCS7 pThis, bool fCatalog)
{
    RTERRINFOSTATIC     ErrInfo;
    RTASN1CURSORPRIMARY PrimaryCursor;
    RTAsn1CursorInitPrimary(&PrimaryCursor, pThis->pbBuf, (uint32_t)pThis->cbBuf, RTErrInfoInitStatic(&ErrInfo),
                            &g_RTAsn1DefaultAllocator, 0, "WinCert");

    int rc = RTCrPkcs7ContentInfo_DecodeAsn1(&PrimaryCursor.Cursor, 0, &pThis->ContentInfo, "CI");
    if (RT_SUCCESS(rc))
    {
        if (RTCrPkcs7ContentInfo_IsSignedData(&pThis->ContentInfo))
        {
            pThis->pSignedData = pThis->ContentInfo.u.pSignedData;

            /*
             * Decode the authenticode bits.
             */
            if (!strcmp(pThis->pSignedData->ContentInfo.ContentType.szObjId, RTCRSPCINDIRECTDATACONTENT_OID))
            {
                PRTCRSPCINDIRECTDATACONTENT pIndData = pThis->pSignedData->ContentInfo.u.pIndirectDataContent;
                Assert(pIndData);

                /*
                 * Check that things add up.
                 */
                rc = RTCrPkcs7SignedData_CheckSanity(pThis->pSignedData,
                                                     RTCRPKCS7SIGNEDDATA_SANITY_F_AUTHENTICODE
                                                     | RTCRPKCS7SIGNEDDATA_SANITY_F_ONLY_KNOWN_HASH
                                                     | RTCRPKCS7SIGNEDDATA_SANITY_F_SIGNING_CERT_PRESENT,
                                                     RTErrInfoInitStatic(&ErrInfo), "SD");
                if (RT_SUCCESS(rc))
                {
                    rc = RTCrSpcIndirectDataContent_CheckSanityEx(pIndData,
                                                                  pThis->pSignedData,
                                                                  RTCRSPCINDIRECTDATACONTENT_SANITY_F_ONLY_KNOWN_HASH,
                                                                  RTErrInfoInitStatic(&ErrInfo));
                    if (RT_FAILURE(rc))
                        RTMsgError("SPC indirect data content sanity check failed for '%s': %Rrc - %s\n",
                                   pThis->pszFilename, rc, ErrInfo.szMsg);
                }
                else
                    RTMsgError("PKCS#7 sanity check failed for '%s': %Rrc - %s\n", pThis->pszFilename, rc, ErrInfo.szMsg);
            }
            else if (!strcmp(pThis->pSignedData->ContentInfo.ContentType.szObjId, RTCR_PKCS7_DATA_OID))
            { /* apple code signing */ }
            else if (!fCatalog)
                RTMsgError("Unexpected the signed content in '%s': %s (expected %s)", pThis->pszFilename,
                           pThis->pSignedData->ContentInfo.ContentType.szObjId, RTCRSPCINDIRECTDATACONTENT_OID);
        }
        else
            rc = RTMsgErrorRc(VERR_CR_PKCS7_NOT_SIGNED_DATA,
                              "PKCS#7 content is inside '%s' is not 'signedData': %s\n",
                              pThis->pszFilename, pThis->ContentInfo.ContentType.szObjId);
    }
    else
        RTMsgError("RTCrPkcs7ContentInfo_DecodeAsn1 failed on '%s': %Rrc - %s\n", pThis->pszFilename, rc, ErrInfo.szMsg);
    return rc;
}


/**
 * Reads and decodes PKCS\#7 signature from the given cat file.
 *
 * @returns RTEXITCODE_SUCCESS on success, RTEXITCODE_FAILURE with error message
 *          on failure.
 * @param   pThis               The structure to initialize.
 * @param   pszFilename         The catalog (or any other DER PKCS\#7) filename.
 * @param   cVerbosity          The verbosity.
 */
static RTEXITCODE SignToolPkcs7_InitFromFile(PSIGNTOOLPKCS7 pThis, const char *pszFilename, unsigned cVerbosity)
{
    /*
     * Init the return structure.
     */
    RT_ZERO(*pThis);
    pThis->pszFilename = pszFilename;
    pThis->enmType     = RTSIGNTOOLFILETYPE_CAT;

    /*
     * Lazy bird uses RTFileReadAll and duplicates the allocation.
     */
    void *pvFile;
    int rc = RTFileReadAll(pszFilename, &pvFile, &pThis->cbBuf);
    if (RT_SUCCESS(rc))
    {
        pThis->pbBuf = (uint8_t *)RTMemDup(pvFile, pThis->cbBuf);
        RTFileReadAllFree(pvFile, pThis->cbBuf);
        if (pThis->pbBuf)
        {
            if (cVerbosity > 2)
                RTPrintf("PKCS#7 signature: %u bytes\n", pThis->cbBuf);

            /*
             * Decode it.
             */
            rc = SignToolPkcs7_Decode(pThis, true /*fCatalog*/);
            if (RT_SUCCESS(rc))
                return RTEXITCODE_SUCCESS;
        }
        else
            RTMsgError("Out of memory!");
    }
    else
        RTMsgError("Error reading '%s' into memory: %Rrc", pszFilename, rc);

    SignToolPkcs7_Delete(pThis);
    return RTEXITCODE_FAILURE;
}


/**
 * Encodes the signature into the SIGNTOOLPKCS7::pbNewBuf and
 * SIGNTOOLPKCS7::cbNewBuf members.
 *
 * @returns RTEXITCODE_SUCCESS on success, RTEXITCODE_FAILURE with error message
 *          on failure.
 * @param   pThis               The signature to encode.
 * @param   cVerbosity          The verbosity.
 */
static RTEXITCODE SignToolPkcs7_Encode(PSIGNTOOLPKCS7 pThis, unsigned cVerbosity)
{
    RTERRINFOSTATIC StaticErrInfo;
    PRTASN1CORE pRoot = RTCrPkcs7ContentInfo_GetAsn1Core(&pThis->ContentInfo);
    uint32_t cbEncoded;
    int rc = RTAsn1EncodePrepare(pRoot, RTASN1ENCODE_F_DER, &cbEncoded, RTErrInfoInitStatic(&StaticErrInfo));
    if (RT_SUCCESS(rc))
    {
        if (cVerbosity >= 4)
            RTAsn1Dump(pRoot, 0, 0, RTStrmDumpPrintfV, g_pStdOut);

        RTMemFree(pThis->pbNewBuf);
        pThis->cbNewBuf = cbEncoded;
        pThis->pbNewBuf = (uint8_t *)RTMemAllocZ(cbEncoded);
        if (pThis->pbNewBuf)
        {
            rc = RTAsn1EncodeToBuffer(pRoot, RTASN1ENCODE_F_DER, pThis->pbNewBuf, pThis->cbNewBuf,
                                      RTErrInfoInitStatic(&StaticErrInfo));
            if (RT_SUCCESS(rc))
            {
                if (cVerbosity > 1)
                    RTMsgInfo("Encoded signature to %u bytes", cbEncoded);
                return RTEXITCODE_SUCCESS;
            }
            RTMsgError("RTAsn1EncodeToBuffer failed: %Rrc", rc);

            RTMemFree(pThis->pbNewBuf);
            pThis->pbNewBuf = NULL;
        }
        else
            RTMsgError("Failed to allocate %u bytes!", cbEncoded);
    }
    else
        RTMsgError("RTAsn1EncodePrepare failed: %Rrc - %s", rc, StaticErrInfo.szMsg);
    return RTEXITCODE_FAILURE;
}


/**
 * Helper that makes sure the UnauthenticatedAttributes are present in the given
 * SignerInfo structure.
 *
 * Call this before trying to modify the array.
 *
 * @returns RTEXITCODE_SUCCESS on success, RTEXITCODE_FAILURE with error already
 *          displayed on failure.
 * @param   pSignerInfo         The SignerInfo structure in question.
 */
static RTEXITCODE SignToolPkcs7_EnsureUnauthenticatedAttributesPresent(PRTCRPKCS7SIGNERINFO pSignerInfo)
{
   if (pSignerInfo->UnauthenticatedAttributes.cItems == 0)
   {
       /* HACK ALERT! Invent ASN.1 setters/whatever for members to replace this mess. */

       if (pSignerInfo->AuthenticatedAttributes.cItems == 0)
           return RTMsgErrorExit(RTEXITCODE_FAILURE, "No authenticated or unauthenticated attributes! Sorry, no can do.");

       Assert(pSignerInfo->UnauthenticatedAttributes.SetCore.Asn1Core.uTag == 0);
       int rc = RTAsn1SetCore_Init(&pSignerInfo->UnauthenticatedAttributes.SetCore,
                                   pSignerInfo->AuthenticatedAttributes.SetCore.Asn1Core.pOps);
       if (RT_FAILURE(rc))
           return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTAsn1SetCore_Init failed: %Rrc", rc);
       pSignerInfo->UnauthenticatedAttributes.SetCore.Asn1Core.uTag   = 1;
       pSignerInfo->UnauthenticatedAttributes.SetCore.Asn1Core.fClass = ASN1_TAGCLASS_CONTEXT | ASN1_TAGFLAG_CONSTRUCTED;
       RTAsn1MemInitArrayAllocation(&pSignerInfo->UnauthenticatedAttributes.Allocation,
                                    pSignerInfo->AuthenticatedAttributes.Allocation.pAllocator,
                                    sizeof(**pSignerInfo->UnauthenticatedAttributes.papItems));
   }
   return RTEXITCODE_SUCCESS;
}


/**
 * Adds the @a pSrc signature as a nested signature.
 *
 * @returns RTEXITCODE_SUCCESS on success, RTEXITCODE_FAILURE with error message
 *          on failure.
 * @param   pThis               The signature to modify.
 * @param   pSrc                The signature to add as nested.
 * @param   cVerbosity          The verbosity.
 * @param   fPrepend            Whether to prepend (true) or append (false) the
 *                              source signature to the nested attribute.
 */
static RTEXITCODE SignToolPkcs7_AddNestedSignature(PSIGNTOOLPKCS7 pThis, PSIGNTOOLPKCS7 pSrc,
                                                   unsigned cVerbosity, bool fPrepend)
{
    PRTCRPKCS7SIGNERINFO pSignerInfo = pThis->pSignedData->SignerInfos.papItems[0];

    /*
     * Deal with UnauthenticatedAttributes being absent before trying to append to the array.
     */
    RTEXITCODE rcExit = SignToolPkcs7_EnsureUnauthenticatedAttributesPresent(pSignerInfo);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Find or add an unauthenticated attribute for nested signatures.
     */
    int rc = VERR_NOT_FOUND;
    PRTCRPKCS7ATTRIBUTE pAttr = NULL;
    int32_t iPos = pSignerInfo->UnauthenticatedAttributes.cItems;
    while (iPos-- > 0)
        if (pSignerInfo->UnauthenticatedAttributes.papItems[iPos]->enmType == RTCRPKCS7ATTRIBUTETYPE_MS_NESTED_SIGNATURE)
        {
            pAttr = pSignerInfo->UnauthenticatedAttributes.papItems[iPos];
            rc = VINF_SUCCESS;
            break;
        }
    if (iPos < 0)
    {
        iPos = RTCrPkcs7Attributes_Append(&pSignerInfo->UnauthenticatedAttributes);
        if (iPos >= 0)
        {
            if (cVerbosity >= 3)
                RTMsgInfo("Adding UnauthenticatedAttribute #%u...", iPos);
            Assert((uint32_t)iPos < pSignerInfo->UnauthenticatedAttributes.cItems);

            pAttr = pSignerInfo->UnauthenticatedAttributes.papItems[iPos];
            rc = RTAsn1ObjId_InitFromString(&pAttr->Type, RTCR_PKCS9_ID_MS_NESTED_SIGNATURE, pAttr->Allocation.pAllocator);
            if (RT_SUCCESS(rc))
            {
                /** @todo Generalize the Type + enmType DYN stuff and generate setters. */
                Assert(pAttr->enmType == RTCRPKCS7ATTRIBUTETYPE_NOT_PRESENT);
                Assert(pAttr->uValues.pContentInfos == NULL);
                pAttr->enmType = RTCRPKCS7ATTRIBUTETYPE_MS_NESTED_SIGNATURE;
                rc = RTAsn1MemAllocZ(&pAttr->Allocation, (void **)&pAttr->uValues.pContentInfos,
                                     sizeof(*pAttr->uValues.pContentInfos));
                if (RT_SUCCESS(rc))
                {
                    rc = RTCrPkcs7SetOfContentInfos_Init(pAttr->uValues.pContentInfos, pAttr->Allocation.pAllocator);
                    if (!RT_SUCCESS(rc))
                        RTMsgError("RTCrPkcs7ContentInfos_Init failed: %Rrc", rc);
                }
                else
                    RTMsgError("RTAsn1MemAllocZ failed: %Rrc", rc);
            }
            else
                RTMsgError("RTAsn1ObjId_InitFromString failed: %Rrc", rc);
        }
        else
            RTMsgError("RTCrPkcs7Attributes_Append failed: %Rrc", iPos);
    }
    else if (cVerbosity >= 2)
        RTMsgInfo("Found UnauthenticatedAttribute #%u...", iPos);
    if (RT_SUCCESS(rc))
    {
        /*
         * Append/prepend the signature.
         */
        uint32_t iActualPos = UINT32_MAX;
        iPos = fPrepend ? 0 : pAttr->uValues.pContentInfos->cItems;
        rc = RTCrPkcs7SetOfContentInfos_InsertEx(pAttr->uValues.pContentInfos, iPos, &pSrc->ContentInfo,
                                                 pAttr->Allocation.pAllocator, &iActualPos);
        if (RT_SUCCESS(rc))
        {
            if (cVerbosity > 0)
                RTMsgInfo("Added nested signature (#%u)", iActualPos);
            if (cVerbosity >= 3)
            {
                RTMsgInfo("SingerInfo dump after change:");
                RTAsn1Dump(RTCrPkcs7SignerInfo_GetAsn1Core(pSignerInfo), 0, 2, RTStrmDumpPrintfV, g_pStdOut);
            }
            return RTEXITCODE_SUCCESS;
        }

        RTMsgError("RTCrPkcs7ContentInfos_InsertEx failed: %Rrc", rc);
    }
    return RTEXITCODE_FAILURE;
}


/**
 * Writes the signature to the file.
 *
 * Caller must have called SignToolPkcs7_Encode() prior to this function.
 *
 * @returns RTEXITCODE_SUCCESS on success, RTEXITCODE_FAILURE with error
 *          message on failure.
 * @param   pThis               The file which to write.
 * @param   cVerbosity          The verbosity.
 */
static RTEXITCODE SignToolPkcs7_WriteSignatureToFile(PSIGNTOOLPKCS7 pThis, const char *pszFilename, unsigned cVerbosity)
{
    AssertReturn(pThis->cbNewBuf && pThis->pbNewBuf, RTEXITCODE_FAILURE);

    /*
     * Open+truncate file, write new signature, close.  Simple.
     */
    RTFILE hFile;
    int rc = RTFileOpen(&hFile, pszFilename, RTFILE_O_WRITE | RTFILE_O_OPEN_CREATE | RTFILE_O_TRUNCATE | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(rc))
    {
        rc = RTFileWrite(hFile, pThis->pbNewBuf, pThis->cbNewBuf, NULL);
        if (RT_SUCCESS(rc))
        {
            rc = RTFileClose(hFile);
            if (RT_SUCCESS(rc))
            {
                if (cVerbosity > 0)
                    RTMsgInfo("Wrote %u bytes to %s", pThis->cbNewBuf, pszFilename);
                return RTEXITCODE_SUCCESS;
            }

            RTMsgError("RTFileClose failed on %s: %Rrc", pszFilename, rc);
        }
        else
            RTMsgError("Write error on %s: %Rrc", pszFilename, rc);
    }
    else
        RTMsgError("Failed to open %s for writing: %Rrc", pszFilename, rc);
    return RTEXITCODE_FAILURE;
}



/**
 * Worker for recursively searching for MS nested signatures and signer infos.
 *
 * @returns Pointer to the signer info corresponding to @a iReqSignature.  NULL
 *          if not found.
 * @param   pSignedData     The signature to search.
 * @param   piNextSignature Pointer to the variable keeping track of the next
 *                          signature number.
 * @param   iReqSignature   The request signature number.
 * @param   ppSignedData    Where to return the signature data structure.
 *                          Optional.
 */
static PRTCRPKCS7SIGNERINFO SignToolPkcs7_FindNestedSignatureByIndexWorker(PRTCRPKCS7SIGNEDDATA pSignedData,
                                                                           uint32_t *piNextSignature,
                                                                           uint32_t iReqSignature,
                                                                           PRTCRPKCS7SIGNEDDATA *ppSignedData)
{
    for (uint32_t iSignerInfo = 0; iSignerInfo < pSignedData->SignerInfos.cItems; iSignerInfo++)
    {
        /* Match?*/
        PRTCRPKCS7SIGNERINFO pSignerInfo = pSignedData->SignerInfos.papItems[iSignerInfo];
        if (*piNextSignature == iReqSignature)
        {
            if (ppSignedData)
                *ppSignedData = pSignedData;
            return pSignerInfo;
        }
        *piNextSignature += 1;

        /* Look for nested signatures. */
        for (uint32_t iAttrib = 0; iAttrib < pSignerInfo->UnauthenticatedAttributes.cItems; iAttrib++)
            if (pSignerInfo->UnauthenticatedAttributes.papItems[iAttrib]->enmType == RTCRPKCS7ATTRIBUTETYPE_MS_NESTED_SIGNATURE)
            {
                PRTCRPKCS7SETOFCONTENTINFOS pCntInfos;
                pCntInfos = pSignerInfo->UnauthenticatedAttributes.papItems[iAttrib]->uValues.pContentInfos;
                for (uint32_t iCntInfo = 0; iCntInfo < pCntInfos->cItems; iCntInfo++)
                {
                    PRTCRPKCS7CONTENTINFO pCntInfo = pCntInfos->papItems[iCntInfo];
                    if (RTCrPkcs7ContentInfo_IsSignedData(pCntInfo))
                    {
                        PRTCRPKCS7SIGNERINFO pRet;
                        pRet = SignToolPkcs7_FindNestedSignatureByIndexWorker(pCntInfo->u.pSignedData, piNextSignature,
                                                                              iReqSignature, ppSignedData);
                        if (pRet)
                            return pRet;
                    }
                }
            }
    }
    return NULL;
}


/**
 * Locates the given nested signature.
 *
 * @returns Pointer to the signer info corresponding to @a iReqSignature.  NULL
 *          if not found.
 * @param   pThis           The PKCS\#7 structure to search.
 * @param   iReqSignature   The requested signature number.
 * @param   ppSignedData    Where to return the pointer to the signed data that
 *                          the returned signer info belongs to.
 *
 * @todo    Move into SPC or PKCS\#7.
 */
static PRTCRPKCS7SIGNERINFO SignToolPkcs7_FindNestedSignatureByIndex(PSIGNTOOLPKCS7 pThis, uint32_t iReqSignature,
                                                                     PRTCRPKCS7SIGNEDDATA *ppSignedData)
{
    uint32_t iNextSignature = 0;
    return SignToolPkcs7_FindNestedSignatureByIndexWorker(pThis->pSignedData, &iNextSignature, iReqSignature, ppSignedData);
}


/**
 * Count the number of signatures, nested or otherwise.
 *
 * @returns Number of signatures.
 * @param   pThis           The PKCS\#7 structure to search.
 *
 * @todo    Move into SPC or PKCS\#7.
 */
static uint32_t SignToolPkcs7_CountSignatures(PSIGNTOOLPKCS7 pThis)
{
    uint32_t             iNextSignature = 0;
    PRTCRPKCS7SIGNEDDATA pSignedData    = NULL;
    SignToolPkcs7_FindNestedSignatureByIndexWorker(pThis->pSignedData, &iNextSignature, UINT32_MAX / 2, &pSignedData);
    return iNextSignature;
}


/**
 * Reads and decodes PKCS\#7 signature from the given executable, if it has one.
 *
 * @returns RTEXITCODE_SUCCESS on success, RTEXITCODE_FAILURE with error message
 *          on failure.
 * @param   pThis               The structure to initialize.
 * @param   pszFilename         The executable filename.
 * @param   cVerbosity          The verbosity.
 * @param   enmLdrArch          For FAT binaries.
 * @param   fAllowUnsigned      Whether to allow unsigned binaries.
 */
static RTEXITCODE SignToolPkcs7Exe_InitFromFile(PSIGNTOOLPKCS7EXE pThis, const char *pszFilename, unsigned cVerbosity,
                                                RTLDRARCH enmLdrArch = RTLDRARCH_WHATEVER, bool fAllowUnsigned = false)
{
    /*
     * Init the return structure.
     */
    RT_ZERO(*pThis);
    pThis->hLdrMod     = NIL_RTLDRMOD;
    pThis->pszFilename = pszFilename;
    pThis->enmType     = RTSIGNTOOLFILETYPE_EXE;

    /*
     * Open the image and check if it's signed.
     */
    int rc = RTLdrOpen(pszFilename, RTLDR_O_FOR_VALIDATION, enmLdrArch, &pThis->hLdrMod);
    if (RT_SUCCESS(rc))
    {
        bool fIsSigned = false;
        rc = RTLdrQueryProp(pThis->hLdrMod, RTLDRPROP_IS_SIGNED, &fIsSigned, sizeof(fIsSigned));
        if (RT_SUCCESS(rc) && fIsSigned)
        {
            /*
             * Query the PKCS#7 data (assuming M$ style signing) and hand it to a worker.
             */
            size_t cbActual = 0;
#ifdef DEBUG
            size_t cbBuf    = 64;
#else
            size_t cbBuf    = _512K;
#endif
            void  *pvBuf    = RTMemAllocZ(cbBuf);
            if (pvBuf)
            {
                rc = RTLdrQueryPropEx(pThis->hLdrMod, RTLDRPROP_PKCS7_SIGNED_DATA, NULL /*pvBits*/, pvBuf, cbBuf, &cbActual);
                if (rc == VERR_BUFFER_OVERFLOW)
                {
                    RTMemFree(pvBuf);
                    cbBuf = cbActual;
                    pvBuf = RTMemAllocZ(cbActual);
                    if (pvBuf)
                        rc = RTLdrQueryPropEx(pThis->hLdrMod, RTLDRPROP_PKCS7_SIGNED_DATA, NULL /*pvBits*/,
                                              pvBuf, cbBuf, &cbActual);
                    else
                        rc = VERR_NO_MEMORY;
                }
            }
            else
                rc = VERR_NO_MEMORY;

            pThis->pbBuf = (uint8_t *)pvBuf;
            pThis->cbBuf = cbActual;
            if (RT_SUCCESS(rc))
            {
                if (cVerbosity > 2)
                    RTPrintf("PKCS#7 signature: %u bytes\n", cbActual);
                if (cVerbosity > 3)
                    RTPrintf("%.*Rhxd\n", cbActual, pvBuf);

                /*
                 * Decode it.
                 */
                rc = SignToolPkcs7_Decode(pThis, false /*fCatalog*/);
                if (RT_SUCCESS(rc))
                    return RTEXITCODE_SUCCESS;
            }
            else
                RTMsgError("RTLdrQueryPropEx/RTLDRPROP_PKCS7_SIGNED_DATA failed on '%s': %Rrc\n", pszFilename, rc);
        }
        else if (RT_SUCCESS(rc))
        {
            if (!fAllowUnsigned || cVerbosity >= 2)
                RTMsgInfo("'%s': not signed\n", pszFilename);
            if (fAllowUnsigned)
                return RTEXITCODE_SUCCESS;
        }
        else
            RTMsgError("RTLdrQueryProp/RTLDRPROP_IS_SIGNED failed on '%s': %Rrc\n", pszFilename, rc);
    }
    else
        RTMsgError("Error opening executable image '%s': %Rrc", pszFilename, rc);

    SignToolPkcs7Exe_Delete(pThis);
    return RTEXITCODE_FAILURE;
}


/**
 * Calculates the checksum of an executable.
 *
 * @returns Success indicator (errors are reported)
 * @param   pThis               The exe file to checksum.
 * @param   hFile               The file handle.
 * @param   puCheckSum          Where to return the checksum.
 */
static bool SignToolPkcs7Exe_CalcPeCheckSum(PSIGNTOOLPKCS7EXE pThis, RTFILE hFile, uint32_t *puCheckSum)
{
#ifdef RT_OS_WINDOWS
    /*
     * Try use IMAGEHLP!MapFileAndCheckSumW first.
     */
    PRTUTF16 pwszPath;
    int rc = RTStrToUtf16(pThis->pszFilename, &pwszPath);
    if (RT_SUCCESS(rc))
    {
        decltype(MapFileAndCheckSumW) *pfnMapFileAndCheckSumW;
        pfnMapFileAndCheckSumW = (decltype(MapFileAndCheckSumW) *)RTLdrGetSystemSymbol("IMAGEHLP.DLL", "MapFileAndCheckSumW");
        if (pfnMapFileAndCheckSumW)
        {
            DWORD uOldSum   = UINT32_MAX;
            DWORD uCheckSum = UINT32_MAX;
            DWORD dwRc = pfnMapFileAndCheckSumW(pwszPath, &uOldSum, &uCheckSum);
            if (dwRc == CHECKSUM_SUCCESS)
            {
                *puCheckSum = uCheckSum;
                return true;
            }
        }
    }
#endif

    RT_NOREF(pThis, hFile, puCheckSum);
    RTMsgError("Implement check sum calcuation fallback!");
    return false;
}


/**
 * Writes the signature to the file.
 *
 * This has the side-effect of closing the hLdrMod member.  So, it can only be
 * called once!
 *
 * Caller must have called SignToolPkcs7_Encode() prior to this function.
 *
 * @returns RTEXITCODE_SUCCESS on success, RTEXITCODE_FAILURE with error
 *          message on failure.
 * @param   pThis               The file which to write.
 * @param   cVerbosity          The verbosity.
 */
static RTEXITCODE SignToolPkcs7Exe_WriteSignatureToFile(PSIGNTOOLPKCS7EXE pThis, unsigned cVerbosity)
{
    AssertReturn(pThis->cbNewBuf && pThis->pbNewBuf, RTEXITCODE_FAILURE);

    /*
     * Get the file header offset and arch before closing the destination handle.
     */
    uint32_t offNtHdrs;
    int rc = RTLdrQueryProp(pThis->hLdrMod, RTLDRPROP_FILE_OFF_HEADER, &offNtHdrs, sizeof(offNtHdrs));
    if (RT_SUCCESS(rc))
    {
        RTLDRARCH enmLdrArch = RTLdrGetArch(pThis->hLdrMod);
        if (enmLdrArch != RTLDRARCH_INVALID)
        {
            RTLdrClose(pThis->hLdrMod);
            pThis->hLdrMod = NIL_RTLDRMOD;
            unsigned cbNtHdrs = 0;
            switch (enmLdrArch)
            {
                case RTLDRARCH_AMD64:
                    cbNtHdrs = sizeof(IMAGE_NT_HEADERS64);
                    break;
                case RTLDRARCH_X86_32:
                    cbNtHdrs = sizeof(IMAGE_NT_HEADERS32);
                    break;
                default:
                    RTMsgError("Unknown image arch: %d", enmLdrArch);
            }
            if (cbNtHdrs > 0)
            {
                if (cVerbosity > 0)
                    RTMsgInfo("offNtHdrs=%#x cbNtHdrs=%u\n", offNtHdrs, cbNtHdrs);

                /*
                 * Open the executable file for writing.
                 */
                RTFILE hFile;
                rc = RTFileOpen(&hFile, pThis->pszFilename, RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
                if (RT_SUCCESS(rc))
                {
                    /* Read the file header and locate the security directory entry. */
                    union
                    {
                        IMAGE_NT_HEADERS32 NtHdrs32;
                        IMAGE_NT_HEADERS64 NtHdrs64;
                    } uBuf;
                    PIMAGE_DATA_DIRECTORY pSecDir = cbNtHdrs == sizeof(IMAGE_NT_HEADERS64)
                                                  ? &uBuf.NtHdrs64.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY]
                                                  : &uBuf.NtHdrs32.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY];

                    rc = RTFileReadAt(hFile, offNtHdrs, &uBuf, cbNtHdrs, NULL);
                    if (   RT_SUCCESS(rc)
                        && uBuf.NtHdrs32.Signature == IMAGE_NT_SIGNATURE)
                    {
                        /*
                         * Drop any old signature by truncating the file.
                         */
                        if (   pSecDir->Size > 8
                            && pSecDir->VirtualAddress > offNtHdrs + sizeof(IMAGE_NT_HEADERS32))
                        {
                            rc = RTFileSetSize(hFile, pSecDir->VirtualAddress);
                            if (RT_FAILURE(rc))
                                RTMsgError("Error truncating file to %#x bytes: %Rrc", pSecDir->VirtualAddress, rc);
                        }
                        else if (pSecDir->Size != 0 && pSecDir->VirtualAddress == 0)
                            rc = RTMsgErrorRc(VERR_BAD_EXE_FORMAT, "Bad security directory entry: VA=%#x Size=%#x",
                                              pSecDir->VirtualAddress, pSecDir->Size);
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * Pad the file with zero up to a WIN_CERTIFICATE_ALIGNMENT boundary.
                             *
                             * Since the hash algorithm hashes everything up to the signature data,
                             * zero padding included, the alignment we do here must match the alignment
                             * padding that done while calculating the hash.
                             */
                            uint32_t const  cbWinCert = RT_UOFFSETOF(WIN_CERTIFICATE, bCertificate);
                            uint64_t        offCur    = 0;
                            rc = RTFileQuerySize(hFile, &offCur);
                            if (   RT_SUCCESS(rc)
                                && offCur < _2G)
                            {
                                if (offCur != RT_ALIGN_64(offCur, WIN_CERTIFICATE_ALIGNMENT))
                                {
                                    uint32_t const cbNeeded = (uint32_t)(RT_ALIGN_64(offCur, WIN_CERTIFICATE_ALIGNMENT) - offCur);
                                    rc = RTFileWriteAt(hFile, offCur, g_abRTZero4K, cbNeeded, NULL);
                                    if (RT_SUCCESS(rc))
                                        offCur += cbNeeded;
                                }
                                if (RT_SUCCESS(rc))
                                {
                                    /*
                                     * Write the header followed by the signature data.
                                     */
                                    uint32_t const cbZeroPad = (uint32_t)(RT_ALIGN_Z(pThis->cbNewBuf, 8) - pThis->cbNewBuf);
                                    pSecDir->VirtualAddress  = (uint32_t)offCur;
                                    pSecDir->Size            = cbWinCert + (uint32_t)pThis->cbNewBuf + cbZeroPad;
                                    if (cVerbosity >= 2)
                                        RTMsgInfo("Writing %u (%#x) bytes of signature at %#x (%u).\n",
                                                  pSecDir->Size, pSecDir->Size, pSecDir->VirtualAddress, pSecDir->VirtualAddress);

                                    WIN_CERTIFICATE WinCert;
                                    WinCert.dwLength         = pSecDir->Size;
                                    WinCert.wRevision        = WIN_CERT_REVISION_2_0;
                                    WinCert.wCertificateType = WIN_CERT_TYPE_PKCS_SIGNED_DATA;

                                    rc = RTFileWriteAt(hFile, offCur, &WinCert, cbWinCert, NULL);
                                    if (RT_SUCCESS(rc))
                                    {
                                        offCur += cbWinCert;
                                        rc = RTFileWriteAt(hFile, offCur, pThis->pbNewBuf, pThis->cbNewBuf, NULL);
                                    }
                                    if (RT_SUCCESS(rc) && cbZeroPad)
                                    {
                                        offCur += pThis->cbNewBuf;
                                        rc = RTFileWriteAt(hFile, offCur, g_abRTZero4K, cbZeroPad, NULL);
                                    }
                                    if (RT_SUCCESS(rc))
                                    {
                                        /*
                                         * Reset the checksum (sec dir updated already) and rewrite the header.
                                         */
                                        uBuf.NtHdrs32.OptionalHeader.CheckSum = 0;
                                        offCur = offNtHdrs;
                                        rc = RTFileWriteAt(hFile, offNtHdrs, &uBuf, cbNtHdrs, NULL);
                                        if (RT_SUCCESS(rc))
                                            rc = RTFileFlush(hFile);
                                        if (RT_SUCCESS(rc))
                                        {
                                            /*
                                             * Calc checksum and write out the header again.
                                             */
                                            uint32_t uCheckSum = UINT32_MAX;
                                            if (SignToolPkcs7Exe_CalcPeCheckSum(pThis, hFile, &uCheckSum))
                                            {
                                                uBuf.NtHdrs32.OptionalHeader.CheckSum = uCheckSum;
                                                rc = RTFileWriteAt(hFile, offNtHdrs, &uBuf, cbNtHdrs, NULL);
                                                if (RT_SUCCESS(rc))
                                                    rc = RTFileFlush(hFile);
                                                if (RT_SUCCESS(rc))
                                                {
                                                    rc = RTFileClose(hFile);
                                                    if (RT_SUCCESS(rc))
                                                        return RTEXITCODE_SUCCESS;
                                                    RTMsgError("RTFileClose failed: %Rrc\n", rc);
                                                    return RTEXITCODE_FAILURE;
                                                }
                                            }
                                        }
                                    }
                                }
                                if (RT_FAILURE(rc))
                                    RTMsgError("Write error at %#RX64: %Rrc", offCur, rc);
                            }
                            else if (RT_SUCCESS(rc))
                                RTMsgError("File to big: %'RU64 bytes", offCur);
                            else
                                RTMsgError("RTFileQuerySize failed: %Rrc", rc);
                        }
                    }
                    else if (RT_SUCCESS(rc))
                        RTMsgError("Not NT executable header!");
                    else
                        RTMsgError("Error reading NT headers (%#x bytes) at %#x: %Rrc", cbNtHdrs, offNtHdrs, rc);
                    RTFileClose(hFile);
                }
                else
                    RTMsgError("Failed to open '%s' for writing: %Rrc", pThis->pszFilename, rc);
            }
        }
        else
            RTMsgError("RTLdrGetArch failed!");
    }
    else
        RTMsgError("RTLdrQueryProp/RTLDRPROP_FILE_OFF_HEADER failed: %Rrc", rc);
    return RTEXITCODE_FAILURE;
}

#ifndef IPRT_SIGNTOOL_NO_SIGNING

static PRTCRPKCS7ATTRIBUTE SignToolPkcs7_AuthAttribAppend(PRTCRPKCS7ATTRIBUTES pAuthAttribs)
{
    int32_t iPos = RTCrPkcs7Attributes_Append(pAuthAttribs);
    if (iPos >= 0)
        return pAuthAttribs->papItems[iPos];
    RTMsgError("RTCrPkcs7Attributes_Append failed: %Rrc", iPos);
    return NULL;
}


static RTEXITCODE SignToolPkcs7_AuthAttribsAddSigningTime(PRTCRPKCS7ATTRIBUTES pAuthAttribs, RTTIMESPEC SigningTime)
{
    /*
     * Signing time.  For the old-style timestamps, Symantec used ASN.1 UTC TIME.
     *                              start -vv    vv=ASN1_TAG_UTC_TIME
     *  00000187d6a65fd0/23b0: 0d 01 09 05 31 0f 17 0d-31 36 31 30 30 35 30 37 ....1...16100507
     *  00000187d6a65fe0/23c0: 35 30 33 30 5a 30 23 06-09 2a 86 48 86 f7 0d 01 5030Z0#..*.H....
     *                                     ^^- end 2016-10-05T07:50:30.000000000Z (161005075030Z)
     */
    PRTCRPKCS7ATTRIBUTE pAttr = SignToolPkcs7_AuthAttribAppend(pAuthAttribs);
    if (!pAttr)
        return RTEXITCODE_FAILURE;

    int rc = RTCrPkcs7Attribute_SetSigningTime(pAttr, NULL, pAuthAttribs->Allocation.pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrPkcs7Attribute_SetSigningTime failed: %Rrc", rc);

    /* Create the timestamp. */
    int32_t iPos = RTAsn1SetOfTimes_Append(pAttr->uValues.pSigningTime);
    if (iPos < 0)
        return RTMsgErrorExitFailure("RTAsn1SetOfTimes_Append failed: %Rrc", iPos);

    PRTASN1TIME pTime = pAttr->uValues.pSigningTime->papItems[iPos];
    rc = RTAsn1Time_SetTimeSpec(pTime, pAttr->Allocation.pAllocator, &SigningTime);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1Time_SetTimeSpec failed: %Rrc", rc);

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE SignToolPkcs7_AuthAttribsAddSpcOpusInfo(PRTCRPKCS7ATTRIBUTES pAuthAttribs, void *pvInfo)
{
    /** @todo The OpusInfo is a structure with an optional SpcString and an
     * optional SpcLink (url). The two attributes can be set using the /d and /du
     * options of MS signtool.exe, I think.  We shouldn't be using them atm. */

    PRTCRPKCS7ATTRIBUTE pAttr = SignToolPkcs7_AuthAttribAppend(pAuthAttribs);
    if (!pAttr)
        return RTEXITCODE_FAILURE;

    int rc = RTCrPkcs7Attribute_SetMsStatementType(pAttr, NULL, pAuthAttribs->Allocation.pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrPkcs7Attribute_SetMsStatementType failed: %Rrc", rc);

    /* Override the ID. */
    rc = RTAsn1ObjId_SetFromString(&pAttr->Type, RTCR_PKCS9_ID_MS_SP_OPUS_INFO, pAuthAttribs->Allocation.pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1ObjId_SetFromString failed: %Rrc", rc);

    /* Add attribute value entry. */
    int32_t iPos = RTAsn1SetOfObjIdSeqs_Append(pAttr->uValues.pObjIdSeqs);
    if (iPos < 0)
        return RTMsgErrorExitFailure("RTAsn1SetOfObjIdSeqs_Append failed: %Rrc", iPos);

    RT_NOREF(pvInfo); Assert(!pvInfo);
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE SignToolPkcs7_AuthAttribsAddMsStatementType(PRTCRPKCS7ATTRIBUTES pAuthAttribs, const char *pszTypeId)
{
    PRTCRPKCS7ATTRIBUTE pAttr = SignToolPkcs7_AuthAttribAppend(pAuthAttribs);
    if (!pAttr)
        return RTEXITCODE_FAILURE;

    int rc = RTCrPkcs7Attribute_SetMsStatementType(pAttr, NULL, pAuthAttribs->Allocation.pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrPkcs7Attribute_SetMsStatementType failed: %Rrc", rc);

    /* Add attribute value entry. */
    int32_t iPos = RTAsn1SetOfObjIdSeqs_Append(pAttr->uValues.pObjIdSeqs);
    if (iPos < 0)
        return RTMsgErrorExitFailure("RTAsn1SetOfObjIdSeqs_Append failed: %Rrc", iPos);
    PRTASN1SEQOFOBJIDS pSeqObjIds = pAttr->uValues.pObjIdSeqs->papItems[iPos];

    /* Add a object id to the value. */
    RTASN1OBJID ObjIdValue;
    rc = RTAsn1ObjId_InitFromString(&ObjIdValue, pszTypeId, &g_RTAsn1DefaultAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1ObjId_InitFromString/%s failed: %Rrc", pszTypeId, rc);

    rc = RTAsn1SeqOfObjIds_InsertEx(pSeqObjIds, 0 /*iPos*/, &ObjIdValue, &g_RTAsn1DefaultAllocator, NULL);
    RTAsn1ObjId_Delete(&ObjIdValue);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1SeqOfObjIds_InsertEx failed: %Rrc", rc);

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE SignToolPkcs7_AuthAttribsAddContentType(PRTCRPKCS7ATTRIBUTES pAuthAttribs, const char *pszContentTypeId)
{
    PRTCRPKCS7ATTRIBUTE pAttr = SignToolPkcs7_AuthAttribAppend(pAuthAttribs);
    if (!pAttr)
        return RTEXITCODE_FAILURE;

    int rc = RTCrPkcs7Attribute_SetContentType(pAttr, NULL, pAuthAttribs->Allocation.pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrPkcs7Attribute_SetContentType failed: %Rrc", rc);

    /* Add a object id to the value. */
    RTASN1OBJID ObjIdValue;
    rc = RTAsn1ObjId_InitFromString(&ObjIdValue, pszContentTypeId, pAuthAttribs->Allocation.pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1ObjId_InitFromString/%s failed: %Rrc", pszContentTypeId, rc);

    rc = RTAsn1SetOfObjIds_InsertEx(pAttr->uValues.pObjIds, 0 /*iPos*/, &ObjIdValue, pAuthAttribs->Allocation.pAllocator, NULL);
    RTAsn1ObjId_Delete(&ObjIdValue);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1SetOfObjIds_InsertEx failed: %Rrc", rc);

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE SignToolPkcs7_AddAuthAttribsForTimestamp(PRTCRPKCS7ATTRIBUTES pAuthAttribs, TIMESTAMPTYPE enmTimestampType,
                                                           RTTIMESPEC SigningTime, PCRTCRX509CERTIFICATE pTimestampCert)
{
    /*
     * Add content type.
     */
    RTEXITCODE rcExit = SignToolPkcs7_AuthAttribsAddContentType(pAuthAttribs,
                                                                enmTimestampType == kTimestampType_Old
                                                                ? RTCR_PKCS7_DATA_OID : RTCRTSPTSTINFO_OID);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Add signing time.
     */
    rcExit = SignToolPkcs7_AuthAttribsAddSigningTime(pAuthAttribs, SigningTime);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * More later if we want to support fTimestampTypeOld = false perhaps?
     */
    Assert(enmTimestampType == kTimestampType_Old);
    RT_NOREF(pTimestampCert);

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE SignToolPkcs7_AddAuthAttribsForImageOrCatSignature(PRTCRPKCS7ATTRIBUTES pAuthAttribs, RTTIMESPEC SigningTime,
                                                                     bool fNoSigningTime, const char *pszContentTypeId)
{
    /*
     * Add SpcOpusInfo.  No attribute values.
     *                      SEQ start -vv    vv- Type ObjId
     *   1c60: 0e 03 02 1a 05 00 a0 70-30 10 06 0a 2b 06 01 04 .......p0...+...
     *   1c70: 01 82 37 02 01 0c 31 02-30 00 30 19 06 09 2a 86 ..7...1.0.0...*.
     *                   Set Of -^^    ^^- Empty Sequence.
     */
    RTEXITCODE rcExit = SignToolPkcs7_AuthAttribsAddSpcOpusInfo(pAuthAttribs, NULL /*pvInfo - none*/);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Add ContentType = Ms-SpcIndirectDataContext?
     *                            SEQ start -vv    vv- Type ObjId
     *   1c70: 01 82 37 02 01 0c 31 02-30 00 30 19 06 09 2a 86 ..7...1.0.0...*.
     *   1c80: 48 86 f7 0d 01 09 03 31-0c 06 0a 2b 06 01 04 01 H......1...+....
     *   1c90: 82 37 02 01 04       ^^-   ^^- ObjId
     *                              ^- Set Of
     */
    rcExit = SignToolPkcs7_AuthAttribsAddContentType(pAuthAttribs, pszContentTypeId);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Add Ms-SpcStatementType = Ms-SpcIndividualCodeSigning.
     *             SEQ start -vv    vv- Type ObjId
     *   1c90: 82 37 02 01 04 30 1c 06-0a 2b 06 01 04 01 82 37 .7...0...+.....7
     *   1ca0: 02 01 0b 31 0e 30 0c 06-0a 2b 06 01 04 01 82 37 ...1.0...+.....7
     *   1cb0: 02 01 15 ^^    ^^    ^^- ObjId
     *          Set Of -^^    ^^- Sequence Of
     */
    rcExit = SignToolPkcs7_AuthAttribsAddMsStatementType(pAuthAttribs, RTCRSPC_STMT_TYPE_INDIVIDUAL_CODE_SIGNING);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Add signing time. We add this, even if signtool.exe, since OpenSSL will always do it otherwise.
     */
    if (!fNoSigningTime) /** @todo requires disabling the code in do_pkcs7_signed_attrib that adds it when absent */
    {
        rcExit = SignToolPkcs7_AuthAttribsAddSigningTime(pAuthAttribs, SigningTime);
        if (rcExit != RTEXITCODE_SUCCESS)
            return rcExit;
    }

    /** @todo more? Some certificate stuff?   */

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE SignToolPkcs7_AppendCounterSignature(PRTCRPKCS7SIGNERINFO pSignerInfo,
                                                       PCRTCRPKCS7SIGNERINFO pCounterSignerInfo, unsigned cVerbosity)
{
    /* Make sure the UnauthenticatedAttributes member is there. */
    RTEXITCODE rcExit = SignToolPkcs7_EnsureUnauthenticatedAttributesPresent(pSignerInfo);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

#if 0 /* Windows won't accept multiple timestamps either way. Doing the latter as it makes more sense to me... */
    /* Append an entry to UnauthenticatedAttributes. */
    uint32_t iPos;
    int rc = RTCrPkcs7Attributes_InsertEx(&pSignerInfo->UnauthenticatedAttributes, 0 /*iPosition*/, NULL /*pToClone*/,
                                          &g_RTAsn1DefaultAllocator, &iPos);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrPkcs7Attributes_Append failed: %Rrc", rc);
    Assert(iPos < pSignerInfo->UnauthenticatedAttributes.cItems); Assert(iPos == 0);
    PRTCRPKCS7ATTRIBUTE pAttr = pSignerInfo->UnauthenticatedAttributes.papItems[iPos];

    if (cVerbosity >= 2)
        RTMsgInfo("Adding UnauthenticatedAttribute #%u...", iPos);
#else
    /* Look up the counter signature attribute, create one if needed. */
    int                 rc;
    uint32_t            iPos  = 0;
    PRTCRPKCS7ATTRIBUTE pAttr = NULL;
    for (; iPos < pSignerInfo->UnauthenticatedAttributes.cItems; iPos++)
    {
        pAttr = pSignerInfo->UnauthenticatedAttributes.papItems[iPos];
        if (pAttr->enmType == RTCRPKCS7ATTRIBUTETYPE_COUNTER_SIGNATURES)
            break;
    }
    if (iPos >= pSignerInfo->UnauthenticatedAttributes.cItems)
    {
        /* Append a new entry to UnauthenticatedAttributes. */
        rc = RTCrPkcs7Attributes_InsertEx(&pSignerInfo->UnauthenticatedAttributes, 0 /*iPosition*/, NULL /*pToClone*/,
                                          &g_RTAsn1DefaultAllocator, &iPos);
        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("RTCrPkcs7Attributes_Append failed: %Rrc", rc);
        Assert(iPos < pSignerInfo->UnauthenticatedAttributes.cItems); Assert(iPos == 0);
        pAttr = pSignerInfo->UnauthenticatedAttributes.papItems[iPos];

        /* Create the attrib and its sub-set of counter signatures. */
        rc = RTCrPkcs7Attribute_SetCounterSignatures(pAttr, NULL, pAttr->Allocation.pAllocator);
        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("RTCrPkcs7Attribute_SetCounterSignatures failed: %Rrc", rc);
    }

    if (cVerbosity >= 2)
        RTMsgInfo("Adding UnauthenticatedAttribute #%u.%u...", iPos, pAttr->uValues.pCounterSignatures->cItems);

#endif

    /* Insert the counter signature. */
    rc = RTCrPkcs7SignerInfos_InsertEx(pAttr->uValues.pCounterSignatures, pAttr->uValues.pCounterSignatures->cItems /*iPosition*/,
                                       pCounterSignerInfo, pAttr->Allocation.pAllocator, NULL);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrPkcs7SignerInfos_InsertEx failed: %Rrc", rc);

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE SignToolPkcs7_AppendCertificate(PRTCRPKCS7SIGNEDDATA pSignedData, PCRTCRX509CERTIFICATE pCertToAppend)
{
    if (pSignedData->Certificates.cItems == 0 && !RTCrPkcs7SetOfCerts_IsPresent(&pSignedData->Certificates))
        return RTMsgErrorExitFailure("PKCS#7 signature includes no certificates! Didn't expect that");

    /* Already there? */
    PCRTCRX509CERTIFICATE pExisting
        = RTCrPkcs7SetOfCerts_FindX509ByIssuerAndSerialNumber(&pSignedData->Certificates, &pCertToAppend->TbsCertificate.Issuer,
                                                              &pCertToAppend->TbsCertificate.SerialNumber);
    if (!pExisting || RTCrX509Certificate_Compare(pExisting, pCertToAppend) != 0)
    {
        /* Prepend a RTCRPKCS7CERT entry. */
        uint32_t iPos;
        int rc = RTCrPkcs7SetOfCerts_InsertEx(&pSignedData->Certificates, 0 /*iPosition*/, NULL /*pToClone*/,
                                              &g_RTAsn1DefaultAllocator, &iPos);
        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("RTCrPkcs7SetOfCerts_Append failed: %Rrc", rc);
        PRTCRPKCS7CERT pCertEntry = pSignedData->Certificates.papItems[iPos];

        /* Set (clone) the certificate. */
        rc = RTCrPkcs7Cert_SetX509Cert(pCertEntry, pCertToAppend, pCertEntry->Allocation.pAllocator);
        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("RTCrPkcs7Cert_X509Cert failed: %Rrc", rc);
    }
    return RTEXITCODE_SUCCESS;
}

#ifdef RT_OS_WINDOWS

static PCRTUTF16 GetBCryptNameFromCrDigest(RTCRDIGEST hDigest)
{
    switch (RTCrDigestGetType(hDigest))
    {
        case RTDIGESTTYPE_MD2:      return BCRYPT_MD2_ALGORITHM;
        case RTDIGESTTYPE_MD4:      return BCRYPT_MD4_ALGORITHM;
        case RTDIGESTTYPE_SHA1:     return BCRYPT_SHA1_ALGORITHM;
        case RTDIGESTTYPE_SHA256:   return BCRYPT_SHA256_ALGORITHM;
        case RTDIGESTTYPE_SHA384:   return BCRYPT_SHA384_ALGORITHM;
        case RTDIGESTTYPE_SHA512:   return BCRYPT_SHA512_ALGORITHM;
        default:
            RTMsgError("No BCrypt translation for %s/%d!", RTCrDigestGetAlgorithmOid(hDigest), RTCrDigestGetType(hDigest));
            return L"No BCrypt translation";
    }
}

static RTEXITCODE
SignToolPkcs7_Pkcs7SignStuffAgainWithReal(const char *pszWhat, SignToolKeyPair *pCertKeyPair, unsigned cVerbosity,
                                          PRTCRPKCS7CONTENTINFO pContentInfo, void **ppvSigned, size_t *pcbSigned)

{
    RT_NOREF(cVerbosity);

    /*
     * First remove the fake certificate from the PKCS7 structure and insert the real one.
     */
    PRTCRPKCS7SIGNEDDATA pSignedData = pContentInfo->u.pSignedData;
    unsigned             iCert       = pSignedData->Certificates.cItems;
    unsigned             cErased     = 0;
    while (iCert-- > 0)
    {
        PCRTCRPKCS7CERT pCert = pSignedData->Certificates.papItems[iCert];
        if (   pCert->enmChoice == RTCRPKCS7CERTCHOICE_X509
            && RTCrX509Certificate_MatchIssuerAndSerialNumber(pCert->u.pX509Cert,
                                                              &pCertKeyPair->pCertificate->TbsCertificate.Issuer,
                                                              &pCertKeyPair->pCertificate->TbsCertificate.SerialNumber))
        {
            RTCrPkcs7SetOfCerts_Erase(&pSignedData->Certificates, iCert);
            cErased++;
        }
    }
    if (cErased == 0)
        return RTMsgErrorExitFailure("(%s) Failed to find temporary signing certificate in PKCS#7 from OpenSSL: %u certs",
                                     pszWhat, pSignedData->Certificates.cItems);

    /* Then insert the real signing certificate. */
    PCRTCRX509CERTIFICATE const pRealCertificate = pCertKeyPair->getRealCertificate();
    RTEXITCODE rcExit = SignToolPkcs7_AppendCertificate(pSignedData, pRealCertificate);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Modify the signer info to reflect the real certificate.
     */
    PRTCRPKCS7SIGNERINFO pSignerInfo = pSignedData->SignerInfos.papItems[0];
    RTCrX509Name_Delete(&pSignerInfo->IssuerAndSerialNumber.Name);
    int rc = RTCrX509Name_Clone(&pSignerInfo->IssuerAndSerialNumber.Name,
                                &pRealCertificate->TbsCertificate.Issuer, &g_RTAsn1DefaultAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("(%s) RTCrX509Name_Clone failed: %Rrc", pszWhat, rc);

    RTAsn1Integer_Delete(&pSignerInfo->IssuerAndSerialNumber.SerialNumber);
    rc = RTAsn1Integer_Clone(&pSignerInfo->IssuerAndSerialNumber.SerialNumber,
                             &pRealCertificate->TbsCertificate.SerialNumber, &g_RTAsn1DefaultAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("(%s) RTAsn1Integer_Clone failed: %Rrc", pszWhat, rc);

    /* There shouldn't be anything in the authenticated attributes that
       we need to modify... */

    /*
     * Now a create a new signature using the real key.  Since we haven't modified
     * the authenticated attributes, we can just hash them as-is.
     */
    /* Create the hash to sign. */
    RTCRDIGEST hDigest;
    rc = RTCrDigestCreateByObjId(&hDigest, &pSignerInfo->DigestAlgorithm.Algorithm);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("(%s) RTCrDigestCreateByObjId failed on '%s': %Rrc",
                                     pszWhat, pSignerInfo->DigestAlgorithm.Algorithm.szObjId, rc);

    rcExit = RTEXITCODE_FAILURE;
    RTERRINFOSTATIC ErrInfo;
    rc = RTCrPkcs7Attributes_HashAttributes(&pSignerInfo->AuthenticatedAttributes, hDigest, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
    {
        BCRYPT_PKCS1_PADDING_INFO PaddingInfo = { GetBCryptNameFromCrDigest(hDigest) };
        DWORD                     cbSignature = 0;
        SECURITY_STATUS rcNCrypt = NCryptSignHash(pCertKeyPair->hNCryptPrivateKey, &PaddingInfo,
                                                  (PBYTE)RTCrDigestGetHash(hDigest), RTCrDigestGetHashSize(hDigest),
                                                  NULL, 0, &cbSignature, NCRYPT_SILENT_FLAG | BCRYPT_PAD_PKCS1);
        if (rcNCrypt == ERROR_SUCCESS)
        {
            if (cVerbosity)
                RTMsgInfo("PaddingInfo: '%ls' cb=%#x, was %#zx\n",
                          PaddingInfo.pszAlgId, cbSignature, pSignerInfo->EncryptedDigest.Asn1Core.cb);

            rc = RTAsn1OctetString_AllocContent(&pSignerInfo->EncryptedDigest, NULL /*pvSrc*/, cbSignature,
                                                &g_RTAsn1DefaultAllocator);
            if (RT_SUCCESS(rc))
            {
                Assert(pSignerInfo->EncryptedDigest.Asn1Core.uData.pv);
                rcNCrypt = NCryptSignHash(pCertKeyPair->hNCryptPrivateKey, &PaddingInfo,
                                          (PBYTE)RTCrDigestGetHash(hDigest), RTCrDigestGetHashSize(hDigest),
                                          (PBYTE)pSignerInfo->EncryptedDigest.Asn1Core.uData.pv, cbSignature, &cbSignature,
                                          /*NCRYPT_SILENT_FLAG |*/ BCRYPT_PAD_PKCS1);
                if (rcNCrypt == ERROR_SUCCESS)
                {
                    /*
                     * Now we need to re-encode the whole thing and decode it again.
                     */
                    PRTASN1CORE pRoot = RTCrPkcs7ContentInfo_GetAsn1Core(pContentInfo);
                    uint32_t    cbRealSigned;
                    rc = RTAsn1EncodePrepare(pRoot, RTASN1ENCODE_F_DER, &cbRealSigned, RTErrInfoInitStatic(&ErrInfo));
                    if (RT_SUCCESS(rc))
                    {
                        void *pvRealSigned = RTMemAllocZ(cbRealSigned);
                        if (pvRealSigned)
                        {
                            rc = RTAsn1EncodeToBuffer(pRoot, RTASN1ENCODE_F_DER, pvRealSigned, cbRealSigned,
                                                      RTErrInfoInitStatic(&ErrInfo));
                            if (RT_SUCCESS(rc))
                            {
                                /* Decode it */
                                RTCrPkcs7ContentInfo_Delete(pContentInfo);

                                RTASN1CURSORPRIMARY PrimaryCursor;
                                RTAsn1CursorInitPrimary(&PrimaryCursor, pvRealSigned, cbRealSigned, RTErrInfoInitStatic(&ErrInfo),
                                                        &g_RTAsn1DefaultAllocator, 0, pszWhat);
                                rc = RTCrPkcs7ContentInfo_DecodeAsn1(&PrimaryCursor.Cursor, 0, pContentInfo, "CI");
                                if (RT_SUCCESS(rc))
                                {
                                    Assert(RTCrPkcs7ContentInfo_IsSignedData(pContentInfo));

                                    /* Almost done! Just replace output buffer. */
                                    RTMemFree(*ppvSigned);
                                    *ppvSigned = pvRealSigned;
                                    *pcbSigned = cbRealSigned;
                                    pvRealSigned = NULL;
                                    rcExit = RTEXITCODE_SUCCESS;
                                }
                                else
                                    RTMsgError("(%s) RTCrPkcs7ContentInfo_DecodeAsn1 failed: %Rrc%#RTeim",
                                               pszWhat, rc, &ErrInfo.Core);
                            }
                            else
                                RTMsgError("(%s) RTAsn1EncodeToBuffer failed: %Rrc%#RTeim", pszWhat, rc, &ErrInfo.Core);

                            RTMemFree(pvRealSigned);
                        }
                        else
                            RTMsgError("(%s) Failed to allocate %u bytes!", pszWhat, cbRealSigned);
                    }
                    else
                        RTMsgError("(%s) RTAsn1EncodePrepare failed: %Rrc%#RTeim", pszWhat, rc, &ErrInfo.Core);
                }
                else
                    RTMsgError("(%s) NCryptSignHash/2 failed: %Rwc %#x (%u)", pszWhat, rcNCrypt, rcNCrypt, rcNCrypt);
            }
            else
                RTMsgError("(%s) RTAsn1OctetString_AllocContent(,,%#x) failed: %Rrc", pszWhat, cbSignature, rc);
        }
        else
            RTMsgError("(%s) NCryptSignHash/1 failed: %Rwc %#x (%u)", pszWhat, rcNCrypt, rcNCrypt, rcNCrypt);
    }
    else
        RTMsgError("(%s) RTCrPkcs7Attributes_HashAttributes failed: %Rrc%#RTeim", pszWhat, rc, &ErrInfo.Core);
    RTCrDigestRelease(hDigest);
    return rcExit;
}

#endif /* RT_OS_WINDOWS */

static RTEXITCODE SignToolPkcs7_Pkcs7SignStuffInner(const char *pszWhat, const void *pvToDataToSign, size_t cbToDataToSign,
                                                    PCRTCRPKCS7ATTRIBUTES pAuthAttribs, RTCRSTORE hAdditionalCerts,
                                                    uint32_t fExtraFlags, RTDIGESTTYPE enmDigestType,
                                                    SignToolKeyPair *pCertKeyPair, unsigned cVerbosity,
                                                    void **ppvSigned, size_t *pcbSigned, PRTCRPKCS7CONTENTINFO pContentInfo,
                                                    PRTCRPKCS7SIGNEDDATA *ppSignedData)
{
    *ppvSigned = NULL;
    if (pcbSigned)
        *pcbSigned = 0;
    if (ppSignedData)
        *ppSignedData = NULL;

    /* Figure out how large the signature will be. */
    uint32_t const  fSignFlags = RTCRPKCS7SIGN_SD_F_USE_V1 | RTCRPKCS7SIGN_SD_F_NO_SMIME_CAP | fExtraFlags;
    size_t          cbSigned   = 1024;
    RTERRINFOSTATIC ErrInfo;
    int rc = RTCrPkcs7SimpleSignSignedData(fSignFlags, pCertKeyPair->pCertificate, pCertKeyPair->hPrivateKey,
                                           pvToDataToSign, cbToDataToSign,enmDigestType, hAdditionalCerts, pAuthAttribs,
                                           NULL, &cbSigned, RTErrInfoInitStatic(&ErrInfo));
    if (rc != VERR_BUFFER_OVERFLOW)
        return RTMsgErrorExitFailure("(%s) RTCrPkcs7SimpleSignSignedData failed: %Rrc%#RTeim", pszWhat, rc, &ErrInfo.Core);

    /* Allocate memory for it and do the actual signing. */
    void *pvSigned = RTMemAllocZ(cbSigned);
    if (!pvSigned)
        return RTMsgErrorExitFailure("(%s) Failed to allocate %#zx bytes for %s signature", pszWhat, cbSigned, pszWhat);
    rc = RTCrPkcs7SimpleSignSignedData(fSignFlags, pCertKeyPair->pCertificate, pCertKeyPair->hPrivateKey,
                                       pvToDataToSign, cbToDataToSign, enmDigestType, hAdditionalCerts, pAuthAttribs,
                                       pvSigned, &cbSigned, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
    {
        if (cVerbosity > 2)
            RTMsgInfo("%s signature: %#zx bytes\n%.*Rhxd\n", pszWhat, cbSigned, cbSigned, pvSigned);

        /*
         * Decode the signature and check that it is SignedData.
         */
        RTASN1CURSORPRIMARY PrimaryCursor;
        RTAsn1CursorInitPrimary(&PrimaryCursor, pvSigned, (uint32_t)cbSigned, RTErrInfoInitStatic(&ErrInfo),
                                &g_RTAsn1DefaultAllocator, 0, pszWhat);
        rc = RTCrPkcs7ContentInfo_DecodeAsn1(&PrimaryCursor.Cursor, 0, pContentInfo, "CI");
        if (RT_SUCCESS(rc))
        {
            if (RTCrPkcs7ContentInfo_IsSignedData(pContentInfo))
            {
#ifdef RT_OS_WINDOWS
                /*
                 * If we're using a fake key+cert, we now have to re-do the signing using the real
                 * key+cert and the windows crypto API.   This kludge is necessary because we can't
                 * typically get that the encoded private key, so it isn't possible to feed it to
                 * openssl.
                 */
                RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
                if (pCertKeyPair->pCertificateReal)
                    rcExit = SignToolPkcs7_Pkcs7SignStuffAgainWithReal(pszWhat, pCertKeyPair, cVerbosity, pContentInfo,
                                                                       &pvSigned, &cbSigned);
                if (rcExit == RTEXITCODE_SUCCESS)
#endif
                {
                    /*
                     * Set returns and maybe display the result before returning.
                     */
                    *ppvSigned = pvSigned;
                    if (pcbSigned)
                        *pcbSigned = cbSigned;
                    if (ppSignedData)
                        *ppSignedData = pContentInfo->u.pSignedData;

                    if (cVerbosity)
                    {
                        SHOWEXEPKCS7 ShowExe;
                        RT_ZERO(ShowExe);
                        ShowExe.cVerbosity = cVerbosity;
                        HandleShowExeWorkerPkcs7Display(&ShowExe, pContentInfo->u.pSignedData, 0, pContentInfo);
                    }
                    return RTEXITCODE_SUCCESS;
                }
            }

            RTMsgError("(%s) RTCrPkcs7SimpleSignSignedData did not create SignedData: %s",
                       pszWhat, pContentInfo->ContentType.szObjId);
        }
        else
            RTMsgError("(%s) RTCrPkcs7ContentInfo_DecodeAsn1 failed: %Rrc%#RTeim", pszWhat, rc, &ErrInfo.Core);
        RTCrPkcs7ContentInfo_Delete(pContentInfo);
    }
    RTMemFree(pvSigned);
    return RTEXITCODE_FAILURE;
}


static RTEXITCODE SignToolPkcs7_Pkcs7SignStuff(const char *pszWhat, const void *pvToDataToSign, size_t cbToDataToSign,
                                               PCRTCRPKCS7ATTRIBUTES pAuthAttribs, RTCRSTORE hAdditionalCerts,
                                               uint32_t fExtraFlags, RTDIGESTTYPE enmDigestType, SignToolKeyPair *pCertKeyPair,
                                               unsigned cVerbosity, void **ppvSigned, size_t *pcbSigned,
                                               PRTCRPKCS7CONTENTINFO pContentInfo, PRTCRPKCS7SIGNEDDATA *ppSignedData)
{
    /*
     * Gather all additional certificates before doing the actual work.
     */
    RTCRSTORE hAllAdditionalCerts = pCertKeyPair->assembleAllAdditionalCertificates(hAdditionalCerts);
    if (hAllAdditionalCerts == NIL_RTCRSTORE)
        return RTEXITCODE_FAILURE;
    RTEXITCODE rcExit = SignToolPkcs7_Pkcs7SignStuffInner(pszWhat, pvToDataToSign, cbToDataToSign, pAuthAttribs,
                                                          hAllAdditionalCerts, fExtraFlags, enmDigestType, pCertKeyPair,
                                                          cVerbosity, ppvSigned, pcbSigned, pContentInfo, ppSignedData);
    RTCrStoreRelease(hAllAdditionalCerts);
    return rcExit;
}


static RTEXITCODE SignToolPkcs7_AddTimestampSignatureEx(PRTCRPKCS7SIGNERINFO pSignerInfo, PRTCRPKCS7SIGNEDDATA pSignedData,
                                                        unsigned cVerbosity,  bool fReplaceExisting,
                                                        RTTIMESPEC SigningTime, SignToolTimestampOpts *pTimestampOpts)
{
    AssertReturn(!pTimestampOpts->isNewType(), RTMsgErrorExitFailure("New style signatures not supported yet"));

    /*
     * Create a set of attributes we need to include in the AuthenticatedAttributes
     * of the timestamp signature.
     */
    RTCRPKCS7ATTRIBUTES AuthAttribs;
    int rc = RTCrPkcs7Attributes_Init(&AuthAttribs, &g_RTAsn1DefaultAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrPkcs7SetOfAttributes_Init failed: %Rrc", rc);

    RTEXITCODE rcExit = SignToolPkcs7_AddAuthAttribsForTimestamp(&AuthAttribs, pTimestampOpts->m_enmType, SigningTime,
                                                                 pTimestampOpts->getRealCertificate());
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /*
         * Now create a PKCS#7 signature of the encrypted signature from the selected signer info.
         */
        void                *pvSigned      = NULL;
        PRTCRPKCS7SIGNEDDATA pTsSignedData = NULL;
        RTCRPKCS7CONTENTINFO TsContentInfo;
        rcExit = SignToolPkcs7_Pkcs7SignStuffInner("timestamp", pSignerInfo->EncryptedDigest.Asn1Core.uData.pv,
                                                   pSignerInfo->EncryptedDigest.Asn1Core.cb, &AuthAttribs,
                                                   NIL_RTCRSTORE /*hAdditionalCerts*/, RTCRPKCS7SIGN_SD_F_DEATCHED,
                                                   RTDIGESTTYPE_SHA1, pTimestampOpts, cVerbosity,
                                                   &pvSigned, NULL /*pcbSigned*/, &TsContentInfo, &pTsSignedData);
        if (rcExit == RTEXITCODE_SUCCESS)
        {

            /*
             * If we're replacing existing timestamp signatures, remove old ones now.
             */
            if (   fReplaceExisting
                && RTCrPkcs7Attributes_IsPresent(&pSignerInfo->UnauthenticatedAttributes))
            {
                uint32_t iItem = pSignerInfo->UnauthenticatedAttributes.cItems;
                while (iItem-- > 0)
                {
                    PRTCRPKCS7ATTRIBUTE pAttr = pSignerInfo->UnauthenticatedAttributes.papItems[iItem];
                    if (pAttr->enmType == RTCRPKCS7ATTRIBUTETYPE_COUNTER_SIGNATURES) /* ASSUMES all counter sigs are timstamps */
                    {
                        if (cVerbosity > 1)
                            RTMsgInfo("Removing counter signature in attribute #%u\n", iItem);
                        rc = RTCrPkcs7Attributes_Erase(&pSignerInfo->UnauthenticatedAttributes, iItem);
                        if (RT_FAILURE(rc))
                            rcExit = RTMsgErrorExitFailure("RTCrPkcs7Attributes_Erase failed on #%u: %Rrc", iItem, rc);
                    }
                }
            }

            /*
             * Add the new one.
             */
            if (rcExit == RTEXITCODE_SUCCESS)
                rcExit = SignToolPkcs7_AppendCounterSignature(pSignerInfo, pTsSignedData->SignerInfos.papItems[0], cVerbosity);

            /*
             * Make sure the signing certificate is included.
             */
            if (rcExit == RTEXITCODE_SUCCESS)
            {
                rcExit = SignToolPkcs7_AppendCertificate(pSignedData, pTimestampOpts->getRealCertificate());

                PCRTCRCERTCTX pInterCaCtx = NULL;
                while ((pInterCaCtx = pTimestampOpts->findNextIntermediateCert(pInterCaCtx)) != NULL)
                    if (rcExit == RTEXITCODE_SUCCESS)
                        rcExit = SignToolPkcs7_AppendCertificate(pSignedData, pInterCaCtx->pCert);
            }

            /*
             * Clean up.
             */
            RTCrPkcs7ContentInfo_Delete(&TsContentInfo);
            RTMemFree(pvSigned);
        }
    }
    RTCrPkcs7Attributes_Delete(&AuthAttribs);
    return rcExit;
}


static RTEXITCODE SignToolPkcs7_AddTimestampSignature(SIGNTOOLPKCS7EXE *pThis, unsigned cVerbosity, unsigned iSignature,
                                                      bool fReplaceExisting, RTTIMESPEC SigningTime,
                                                      SignToolTimestampOpts *pTimestampOpts)
{
    /*
     * Locate the signature specified by iSignature and add a timestamp to it.
     */
    PRTCRPKCS7SIGNEDDATA pSignedData = NULL;
    PRTCRPKCS7SIGNERINFO pSignerInfo = SignToolPkcs7_FindNestedSignatureByIndex(pThis, iSignature, &pSignedData);
    if (!pSignerInfo)
        return RTMsgErrorExitFailure("No signature #%u in %s", iSignature, pThis->pszFilename);

    return SignToolPkcs7_AddTimestampSignatureEx(pSignerInfo, pSignedData, cVerbosity, fReplaceExisting,
                                                 SigningTime, pTimestampOpts);
}


typedef enum SIGNDATATWEAK
{
    kSignDataTweak_NoTweak = 1,
    kSignDataTweak_RootIsParent
} SIGNDATATWEAK;

static RTEXITCODE SignToolPkcs7_SignData(SIGNTOOLPKCS7 *pThis, PRTASN1CORE pToSignRoot, SIGNDATATWEAK enmTweak,
                                         const char *pszContentTypeId, unsigned cVerbosity, uint32_t fExtraFlags,
                                         RTDIGESTTYPE enmSigType, bool fReplaceExisting, bool fNoSigningTime,
                                         SignToolKeyPair *pSigningCertKey, RTCRSTORE hAddCerts,
                                         RTTIMESPEC SigningTime, size_t cTimestampOpts, SignToolTimestampOpts *paTimestampOpts)
{
    /*
     * Encode it.
     */
    RTERRINFOSTATIC ErrInfo;
    uint32_t        cbEncoded = 0;
    int rc = RTAsn1EncodePrepare(pToSignRoot, RTASN1ENCODE_F_DER, &cbEncoded, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1EncodePrepare failed: %Rrc%RTeim", rc, &ErrInfo.Core);

    if (cVerbosity >= 4)
        RTAsn1Dump(pToSignRoot, 0, 0, RTStrmDumpPrintfV, g_pStdOut);

    uint8_t *pbEncoded = (uint8_t *)RTMemTmpAllocZ(cbEncoded );
    if (!pbEncoded)
        return RTMsgErrorExitFailure("Failed to allocate %#z bytes for encoding data we're signing (%s)",
                                     cbEncoded, pszContentTypeId);

    RTEXITCODE rcExit = RTEXITCODE_FAILURE;
    rc = RTAsn1EncodeToBuffer(pToSignRoot, RTASN1ENCODE_F_DER, pbEncoded, cbEncoded, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
    {
        size_t const cbToSign = cbEncoded - (enmTweak == kSignDataTweak_RootIsParent ? pToSignRoot->cbHdr : 0);
        void const  *pvToSign = pbEncoded + (enmTweak == kSignDataTweak_RootIsParent ? pToSignRoot->cbHdr : 0);

        /*
         * Create additional authenticated attributes.
         */
        RTCRPKCS7ATTRIBUTES AuthAttribs;
        rc = RTCrPkcs7Attributes_Init(&AuthAttribs, &g_RTAsn1DefaultAllocator);
        if (RT_SUCCESS(rc))
        {
            rcExit = SignToolPkcs7_AddAuthAttribsForImageOrCatSignature(&AuthAttribs, SigningTime, fNoSigningTime,
                                                                        pszContentTypeId);
            if (rcExit == RTEXITCODE_SUCCESS)
            {
                /*
                 * Ditch the old signature if so desired.
                 * (It is okay to do this in the CAT case too, as we've already
                 * encoded the data and won't touch pToSignRoot any more.)
                 */
                pToSignRoot = NULL; /* (may become invalid if replacing) */
                if (fReplaceExisting && pThis->pSignedData)
                {
                    RTCrPkcs7ContentInfo_Delete(&pThis->ContentInfo);
                    pThis->pSignedData = NULL;
                    RTMemFree(pThis->pbBuf);
                    pThis->pbBuf = NULL;
                    pThis->cbBuf = 0;
                }

                /*
                 * Do the actual signing.
                 */
                SIGNTOOLPKCS7  Src     = { RTSIGNTOOLFILETYPE_DETECT, NULL, 0, NULL };
                PSIGNTOOLPKCS7 pSigDst = !pThis->pSignedData ? pThis : &Src;
                rcExit = SignToolPkcs7_Pkcs7SignStuff("image", pvToSign, cbToSign, &AuthAttribs, hAddCerts,
                                                      fExtraFlags | RTCRPKCS7SIGN_SD_F_NO_DATA_ENCAP, enmSigType /** @todo ?? */,
                                                      pSigningCertKey, cVerbosity,
                                                      (void **)&pSigDst->pbBuf, &pSigDst->cbBuf,
                                                      &pSigDst->ContentInfo, &pSigDst->pSignedData);
                if (rcExit == RTEXITCODE_SUCCESS)
                {
                    /*
                     * Add the requested timestamp signatures if requested.
                     */
                    for (size_t i = 0; rcExit == RTEXITCODE_SUCCESS &&i < cTimestampOpts; i++)
                        if (paTimestampOpts[i].isComplete())
                            rcExit = SignToolPkcs7_AddTimestampSignatureEx(pSigDst->pSignedData->SignerInfos.papItems[0],
                                                                           pSigDst->pSignedData,
                                                                           cVerbosity, false /*fReplaceExisting*/,
                                                                           SigningTime, &paTimestampOpts[i]);

                    /*
                     * Append the signature to the existing one, if that's what we're doing.
                     */
                    if (rcExit == RTEXITCODE_SUCCESS && pSigDst == &Src)
                        rcExit = SignToolPkcs7_AddNestedSignature(pThis, &Src, cVerbosity, true /*fPrepend*/); /** @todo prepend/append option */

                    /* cleanup */
                    if (pSigDst == &Src)
                        SignToolPkcs7_Delete(&Src);
                }

            }
            RTCrPkcs7Attributes_Delete(&AuthAttribs);
        }
        else
            RTMsgError("RTCrPkcs7SetOfAttributes_Init failed: %Rrc", rc);
    }
    else
        RTMsgError("RTAsn1EncodeToBuffer failed: %Rrc", rc);
    RTMemTmpFree(pbEncoded);
    return rcExit;
}


static RTEXITCODE SignToolPkcs7_SpcCompleteWithoutPageHashes(RTCRSPCINDIRECTDATACONTENT *pSpcIndData)
{
    PCRTASN1ALLOCATORVTABLE const pAllocator = &g_RTAsn1DefaultAllocator;
    PRTCRSPCPEIMAGEDATA const     pPeImage   = pSpcIndData->Data.uValue.pPeImage;
    Assert(pPeImage);

    /*
     * Set it to File with an empty name.
     *         RTCRSPCPEIMAGEDATA::Flags -vv
     * RTCRSPCPEIMAGEDATA::SeqCore -vv         T0 -vv    vv- pT2/CtxTag2
     *   0040: 04 01 82 37 02 01 0f 30-09 03 01 00 a0 04 a2 02 ...7...0........
     *   0050: 80 00 30 21 30 09 06 05-2b 0e 03 02 1a 05 00 04 ..0!0...+.......
     *         ^^- pUcs2 / empty string
     */

    /* Create an empty BMP string. */
    RTASN1STRING EmptyStr;
    int rc = RTAsn1BmpString_Init(&EmptyStr, pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1BmpString_Init/Ucs2 failed: %Rrc", rc);

    /* Create an SPC string and use the above empty string with the Ucs2 setter. */
    RTEXITCODE    rcExit = RTEXITCODE_FAILURE;
    RTCRSPCSTRING SpcString;
    rc = RTCrSpcString_Init(&SpcString, pAllocator);
    if (RT_SUCCESS(rc))
    {
        rc = RTCrSpcString_SetUcs2(&SpcString, &EmptyStr, pAllocator);
        if (RT_SUCCESS(rc))
        {
            /* Create a temporary SpcLink with the empty SpcString. */
            RTCRSPCLINK SpcLink;
            rc = RTCrSpcLink_Init(&SpcLink, pAllocator);
            if (RT_SUCCESS(rc))
            {
                /* Use the setter on the SpcLink object to copy the SpcString to it. */
                rc = RTCrSpcLink_SetFile(&SpcLink, &SpcString, pAllocator);
                if (RT_SUCCESS(rc))
                {
                    /* Use the setter to copy SpcLink to the PeImage structure. */
                    rc = RTCrSpcPeImageData_SetFile(pPeImage, &SpcLink, pAllocator);
                    if (RT_SUCCESS(rc))
                        rcExit = RTEXITCODE_SUCCESS;
                    else
                        RTMsgError("RTCrSpcPeImageData_SetFile failed: %Rrc", rc);
                }
                else
                    RTMsgError("RTCrSpcLink_SetFile failed: %Rrc", rc);
                RTCrSpcLink_Delete(&SpcLink);
            }
            else
                RTMsgError("RTCrSpcLink_Init failed: %Rrc", rc);
        }
        else
            RTMsgError("RTCrSpcString_SetUcs2 failed: %Rrc", rc);
        RTCrSpcString_Delete(&SpcString);
    }
    else
        RTMsgError("RTCrSpcString_Init failed: %Rrc", rc);
    RTAsn1BmpString_Delete(&EmptyStr);
    return rcExit;
}


static RTEXITCODE SignToolPkcs7_SpcAddImagePageHashes(SIGNTOOLPKCS7EXE *pThis, RTCRSPCINDIRECTDATACONTENT *pSpcIndData,
                                                      RTDIGESTTYPE enmSigType)
{
    PCRTASN1ALLOCATORVTABLE const pAllocator = &g_RTAsn1DefaultAllocator;
    PRTCRSPCPEIMAGEDATA const     pPeImage   = pSpcIndData->Data.uValue.pPeImage;
    Assert(pPeImage);

    /*
     * The hashes are stored in the 'Moniker' attribute.
     */
    /* Create a temporary SpcLink with a default moniker. */
    RTCRSPCLINK SpcLink;
    int rc = RTCrSpcLink_Init(&SpcLink, pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrSpcLink_Init failed: %Rrc", rc);
    rc = RTCrSpcLink_SetMoniker(&SpcLink, NULL, pAllocator);
    if (RT_SUCCESS(rc))
    {
        /* Use the setter to copy SpcLink to the PeImage structure. */
        rc = RTCrSpcPeImageData_SetFile(pPeImage, &SpcLink, pAllocator);
        if (RT_FAILURE(rc))
            RTMsgError("RTCrSpcLink_SetFile failed: %Rrc", rc);
    }
    else
        RTMsgError("RTCrSpcLink_SetMoniker failed: %Rrc", rc);
    RTCrSpcLink_Delete(&SpcLink);
    if (RT_FAILURE(rc))
        return RTEXITCODE_FAILURE;

    /*
     * Now go to work on the moniker.  It doesn't have any autogenerated
     * setters, so we must do stuff manually.
     */
    PRTCRSPCSERIALIZEDOBJECT pMoniker = pPeImage->T0.File.u.pMoniker;
    RTUUID                   Uuid;
    rc = RTUuidFromStr(&Uuid, RTCRSPCSERIALIZEDOBJECT_UUID_STR);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTUuidFromStr failed: %Rrc", rc);

    rc = RTAsn1OctetString_AllocContent(&pMoniker->Uuid, &Uuid, sizeof(Uuid), pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1String_InitWithValue/UUID failed: %Rrc", rc);

    /* Create a new set of attributes and associate this with the SerializedData member. */
    PRTCRSPCSERIALIZEDOBJECTATTRIBUTES pSpcAttribs;
    rc = RTAsn1MemAllocZ(&pMoniker->SerializedData.EncapsulatedAllocation,
                         (void **)&pSpcAttribs, sizeof(*pSpcAttribs));
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1MemAllocZ/pSpcAttribs failed: %Rrc", rc);
    pMoniker->SerializedData.pEncapsulated = RTCrSpcSerializedObjectAttributes_GetAsn1Core(pSpcAttribs);
    pMoniker->enmType                      = RTCRSPCSERIALIZEDOBJECTTYPE_ATTRIBUTES;
    pMoniker->u.pData                      = pSpcAttribs;

    rc = RTCrSpcSerializedObjectAttributes_Init(pSpcAttribs, pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrSpcSerializedObjectAttributes_Init failed: %Rrc", rc);

    /*
     * Add a single attribute to the set that we'll use for page hashes.
     */
    int32_t iPos = RTCrSpcSerializedObjectAttributes_Append(pSpcAttribs);
    if (iPos < 0)
        return RTMsgErrorExitFailure("RTCrSpcSerializedObjectAttributes_Append failed: %Rrc", iPos);
    PRTCRSPCSERIALIZEDOBJECTATTRIBUTE pSpcObjAttr = pSpcAttribs->papItems[iPos];

    if (enmSigType == RTDIGESTTYPE_SHA1)
        rc = RTCrSpcSerializedObjectAttribute_SetV1Hashes(pSpcObjAttr, NULL, pAllocator);
    else if (enmSigType == RTDIGESTTYPE_SHA256)
        rc = RTCrSpcSerializedObjectAttribute_SetV2Hashes(pSpcObjAttr, NULL, pAllocator);
    else
        rc = VERR_CR_DIGEST_NOT_SUPPORTED;
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrSpcSerializedObjectAttribute_SetV1Hashes/SetV2Hashes failed: %Rrc", rc);
    PRTCRSPCSERIALIZEDPAGEHASHES pSpcPageHashes = pSpcObjAttr->u.pPageHashes;
    Assert(pSpcPageHashes);

    /*
     * Now ask the loader for the number of pages in the page hash table
     * and calculate its size.
     */
    uint32_t cPages = 0;
    rc = RTLdrQueryPropEx(pThis->hLdrMod, RTLDRPROP_HASHABLE_PAGES, NULL, &cPages, sizeof(cPages), NULL);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTLdrQueryPropEx/RTLDRPROP_HASHABLE_PAGES failed: %Rrc", rc);

    uint32_t const cbHash  = RTCrDigestTypeToHashSize(enmSigType);
    AssertReturn(cbHash > 0, RTMsgErrorExitFailure("Invalid value: enmSigType=%d", enmSigType));
    uint32_t const cbTable = (sizeof(uint32_t) + cbHash) * cPages;

    /*
     * Allocate memory in the octect string.
     */
    rc = RTAsn1ContentAllocZ(&pSpcPageHashes->RawData.Asn1Core, cbTable, pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1ContentAllocZ failed to allocate %#x bytes for page hashes: %Rrc", cbTable, rc);
    pSpcPageHashes->pData = (PCRTCRSPCPEIMAGEPAGEHASHES)pSpcPageHashes->RawData.Asn1Core.uData.pu8;

    RTLDRPROP enmLdrProp;
    switch (enmSigType)
    {
        case RTDIGESTTYPE_SHA1:     enmLdrProp = RTLDRPROP_SHA1_PAGE_HASHES; break;
        case RTDIGESTTYPE_SHA256:   enmLdrProp = RTLDRPROP_SHA256_PAGE_HASHES; break;
        default: AssertFailedReturn(RTMsgErrorExitFailure("Invalid value: enmSigType=%d", enmSigType));

    }
    rc = RTLdrQueryPropEx(pThis->hLdrMod, enmLdrProp, NULL, (void *)pSpcPageHashes->RawData.Asn1Core.uData.pv, cbTable, NULL);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTLdrQueryPropEx/RTLDRPROP_SHA?_PAGE_HASHES/%#x failed: %Rrc", cbTable, rc);

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE SignToolPkcs7_SpcAddImageHash(SIGNTOOLPKCS7EXE *pThis, RTCRSPCINDIRECTDATACONTENT *pSpcIndData,
                                                RTDIGESTTYPE enmSigType)
{
    uint32_t     const cbHash   = RTCrDigestTypeToHashSize(enmSigType);
    const char * const pszAlgId = RTCrDigestTypeToAlgorithmOid(enmSigType);

    /*
     * Ask the loader for the hash.
     */
    uint8_t abHash[RTSHA512_HASH_SIZE];
    int rc = RTLdrHashImage(pThis->hLdrMod, enmSigType, abHash, sizeof(abHash));
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTLdrHashImage/%s failed: %Rrc", RTCrDigestTypeToName(enmSigType), rc);

    /*
     * Set it.
     */
    /** @todo no setter, this should be okay, though...   */
    rc = RTAsn1ObjId_InitFromString(&pSpcIndData->DigestInfo.DigestAlgorithm.Algorithm, pszAlgId, &g_RTAsn1DefaultAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1ObjId_InitFromString/%s failed: %Rrc", pszAlgId, rc);
    RTAsn1DynType_SetToNull(&pSpcIndData->DigestInfo.DigestAlgorithm.Parameters); /* ASSUMES RSA or similar */

    rc = RTAsn1ContentDup(&pSpcIndData->DigestInfo.Digest.Asn1Core, abHash, cbHash, &g_RTAsn1DefaultAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1ContentDup/%#x failed: %Rrc", cbHash, rc);

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE SignToolPkcs7_AddOrReplaceSignature(SIGNTOOLPKCS7EXE *pThis, unsigned cVerbosity, RTDIGESTTYPE enmSigType,
                                                      bool fReplaceExisting,  bool fHashPages, bool fNoSigningTime,
                                                      SignToolKeyPair *pSigningCertKey, RTCRSTORE hAddCerts,
                                                      RTTIMESPEC SigningTime,
                                                      size_t cTimestampOpts, SignToolTimestampOpts *paTimestampOpts)
{
    /*
     * We must construct the data to be packed into the PKCS#7 signature
     * and signed.
     */
    PCRTASN1ALLOCATORVTABLE const   pAllocator = &g_RTAsn1DefaultAllocator;
    RTCRSPCINDIRECTDATACONTENT      SpcIndData;
    int rc = RTCrSpcIndirectDataContent_Init(&SpcIndData, pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrSpcIndirectDataContent_Init failed: %Rrc", rc);

    /* Set the data to PE image. */
    /** @todo Generalize the Type + enmType DYN stuff and generate setters. */
    Assert(SpcIndData.Data.enmType == RTCRSPCAAOVTYPE_NOT_PRESENT);
    Assert(SpcIndData.Data.uValue.pPeImage == NULL);
    RTEXITCODE rcExit;
    rc = RTAsn1ObjId_SetFromString(&SpcIndData.Data.Type, RTCRSPCPEIMAGEDATA_OID, pAllocator);
    if (RT_SUCCESS(rc))
    {
        SpcIndData.Data.enmType = RTCRSPCAAOVTYPE_PE_IMAGE_DATA;
        rc = RTAsn1MemAllocZ(&SpcIndData.Data.Allocation, (void **)&SpcIndData.Data.uValue.pPeImage,
                             sizeof(*SpcIndData.Data.uValue.pPeImage));
        if (RT_SUCCESS(rc))
        {
            rc = RTCrSpcPeImageData_Init(SpcIndData.Data.uValue.pPeImage, pAllocator);
            if (RT_SUCCESS(rc))
            {
                /* Old (SHA1) signatures has a Flags member, it's zero bits, though. */
                if (enmSigType == RTDIGESTTYPE_SHA1)
                {
                    uint8_t         bFlags = 0;
                    RTASN1BITSTRING Flags;
                    rc = RTAsn1BitString_InitWithData(&Flags, &bFlags, 0, pAllocator);
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTCrSpcPeImageData_SetFlags(SpcIndData.Data.uValue.pPeImage, &Flags, pAllocator);
                        RTAsn1BitString_Delete(&Flags);
                        if (RT_FAILURE(rc))
                            rcExit = RTMsgErrorExitFailure("RTCrSpcPeImageData_SetFlags failed: %Rrc", rc);
                    }
                    else
                        rcExit = RTMsgErrorExitFailure("RTAsn1BitString_InitWithData failed: %Rrc", rc);
                }

                /*
                 * Add the hashes.
                 */
                rcExit = SignToolPkcs7_SpcAddImageHash(pThis, &SpcIndData, enmSigType);
                if (rcExit == RTEXITCODE_SUCCESS)
                {
                    if (fHashPages)
                        rcExit = SignToolPkcs7_SpcAddImagePageHashes(pThis, &SpcIndData, enmSigType);
                    else
                        rcExit = SignToolPkcs7_SpcCompleteWithoutPageHashes(&SpcIndData);

                    /*
                     * Encode and sign the SPC data, timestamp it, and line it up for adding to the executable.
                     */
                    if (rcExit == RTEXITCODE_SUCCESS)
                        rcExit = SignToolPkcs7_SignData(pThis, RTCrSpcIndirectDataContent_GetAsn1Core(&SpcIndData),
                                                        kSignDataTweak_NoTweak, RTCRSPCINDIRECTDATACONTENT_OID, cVerbosity, 0,
                                                        enmSigType, fReplaceExisting, fNoSigningTime, pSigningCertKey, hAddCerts,
                                                        SigningTime, cTimestampOpts, paTimestampOpts);
                }
            }
            else
                rcExit = RTMsgErrorExitFailure("RTCrPkcs7SignerInfos_Init failed: %Rrc", rc);
        }
        else
            rcExit = RTMsgErrorExitFailure("RTAsn1MemAllocZ failed for RTCRSPCPEIMAGEDATA: %Rrc", rc);
    }
    else
        rcExit = RTMsgErrorExitFailure("RTAsn1ObjId_SetWithString/SpcPeImageData failed: %Rrc", rc);

    RTCrSpcIndirectDataContent_Delete(&SpcIndData);
    return rcExit;
}


static RTEXITCODE SignToolPkcs7_AddOrReplaceCatSignature(SIGNTOOLPKCS7 *pThis, unsigned cVerbosity, RTDIGESTTYPE enmSigType,
                                                         bool fReplaceExisting, bool fNoSigningTime,
                                                         SignToolKeyPair *pSigningCertKey, RTCRSTORE hAddCerts,
                                                         RTTIMESPEC SigningTime,
                                                         size_t cTimestampOpts, SignToolTimestampOpts *paTimestampOpts)
{
    AssertReturn(pThis->pSignedData, RTMsgErrorExitFailure("pSignedData is NULL!"));

    /*
     * Figure out what to sign first.
     */
    uint32_t    fExtraFlags = 0;
    PRTASN1CORE pToSign     = &pThis->pSignedData->ContentInfo.Content.Asn1Core;
    const char *pszType     = pThis->pSignedData->ContentInfo.ContentType.szObjId;

    if (!fReplaceExisting && pThis->pSignedData->SignerInfos.cItems == 0)
        fReplaceExisting = true;
    if (!fReplaceExisting)
    {
        pszType      = RTCR_PKCS7_DATA_OID;
        fExtraFlags |= RTCRPKCS7SIGN_SD_F_DEATCHED;
    }

    /*
     * Do the signing.
     */
    RTEXITCODE rcExit = SignToolPkcs7_SignData(pThis, pToSign, kSignDataTweak_RootIsParent,
                                               pszType, cVerbosity, fExtraFlags, enmSigType, fReplaceExisting,
                                               fNoSigningTime, pSigningCertKey, hAddCerts,
                                               SigningTime, cTimestampOpts, paTimestampOpts);

    /* probably need to clean up stuff related to nested signatures here later... */
    return rcExit;
}

#endif /* !IPRT_SIGNTOOL_NO_SIGNING */


/*********************************************************************************************************************************
*   Option handlers shared by 'sign-exe', 'sign-cat', 'add-timestamp-exe-signature' and others.                                  *
*********************************************************************************************************************************/
#ifndef IPRT_SIGNTOOL_NO_SIGNING

static RTEXITCODE HandleOptAddCert(PRTCRSTORE phStore, const char *pszFile)
{
    if (*phStore == NIL_RTCRSTORE)
    {
        int rc = RTCrStoreCreateInMem(phStore, 2);
        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("RTCrStoreCreateInMem(,2) failed: %Rrc", rc);
    }
    RTERRINFOSTATIC ErrInfo;
    int rc = RTCrStoreCertAddFromFile(*phStore, RTCRCERTCTX_F_ADD_IF_NOT_FOUND, pszFile, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("Error reading certificate from '%s': %Rrc%#RTeim", pszFile, rc, &ErrInfo.Core);
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE HandleOptSignatureType(RTDIGESTTYPE *penmSigType, const char *pszType)
{
    if (   RTStrICmpAscii(pszType, "sha1") == 0
        || RTStrICmpAscii(pszType, "sha-1") == 0)
        *penmSigType = RTDIGESTTYPE_SHA1;
    else if (   RTStrICmpAscii(pszType, "sha256") == 0
             || RTStrICmpAscii(pszType, "sha-256") == 0)
        *penmSigType = RTDIGESTTYPE_SHA256;
    else
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown signature type: %s (expected sha1 or sha256)", pszType);
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE HandleOptTimestampType(SignToolTimestampOpts *pTimestampOpts, const char *pszType)
{
    if (strcmp(pszType, "old") == 0)
        pTimestampOpts->m_enmType = kTimestampType_Old;
    else if (strcmp(pszType, "new") == 0)
        pTimestampOpts->m_enmType = kTimestampType_New;
    else
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown timestamp type: %s", pszType);
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE HandleOptTimestampOverride(PRTTIMESPEC pSigningTime, const char *pszPartialTs)
{
    /*
     * First try use it as-is.
     */
    if (RTTimeSpecFromString(pSigningTime, pszPartialTs) != NULL)
        return RTEXITCODE_SUCCESS;

    /* Check the input against a pattern, making sure we've got something that
       makes sense before trying to merge. */
    size_t const cchPartialTs = strlen(pszPartialTs);
    static char s_szPattern[] = "0000-00-00T00:00:";
    if (cchPartialTs > sizeof(s_szPattern) - 1) /* It is not a partial timestamp if we've got the seconds component. */
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Invalid timestamp: %s", pszPartialTs);

    for (size_t off = 0; off < cchPartialTs; off++)
        switch (s_szPattern[off])
        {
            case '0':
                if (!RT_C_IS_DIGIT(pszPartialTs[off]))
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Invalid timestamp, expected digit at position %u: %s",
                                          off + 1, pszPartialTs);
                break;
            case '-':
            case ':':
                if (pszPartialTs[off] != s_szPattern[off])
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Invalid timestamp, expected '%c' at position %u: %s",
                                          s_szPattern[off], off + 1, pszPartialTs);
                break;
            case 'T':
                if (   pszPartialTs[off] != 'T'
                    && pszPartialTs[off] != 't'
                    && pszPartialTs[off] != ' ')
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Invalid timestamp, expected 'T' or space at position %u: %s",
                                          off + 1, pszPartialTs);
                break;
            default:
                return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Internal error");
        }

    if (RT_C_IS_DIGIT(s_szPattern[cchPartialTs]) && RT_C_IS_DIGIT(s_szPattern[cchPartialTs - 1]))
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Incomplete timstamp component: %s", pszPartialTs);

    /*
     * Take the current time and merge in the components from pszPartialTs.
     */
    char        szSigningTime[RTTIME_STR_LEN];
    RTTIMESPEC  Now;
    RTTimeSpecToString(RTTimeNow(&Now), szSigningTime, sizeof(szSigningTime));
    memcpy(szSigningTime, pszPartialTs, cchPartialTs);
    szSigningTime[4+1+2+1+2] = 'T';

    /* Fix 29th for non-leap override: */
    if (memcmp(&szSigningTime[5], RT_STR_TUPLE("02-29")) == 0)
    {
        if (!RTTimeIsLeapYear(RTStrToUInt32(szSigningTime)))
            szSigningTime[9] = '8';
    }
    if (RTTimeSpecFromString(pSigningTime, szSigningTime) == NULL)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Invalid timestamp: %s (%s)", pszPartialTs, szSigningTime);

    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE HandleOptFileType(RTSIGNTOOLFILETYPE *penmFileType, const char *pszType)
{
    if (strcmp(pszType, "detect") == 0 || strcmp(pszType, "auto") == 0)
        *penmFileType = RTSIGNTOOLFILETYPE_DETECT;
    else if (strcmp(pszType, "exe") == 0)
        *penmFileType = RTSIGNTOOLFILETYPE_EXE;
    else if (strcmp(pszType, "cat") == 0)
        *penmFileType = RTSIGNTOOLFILETYPE_CAT;
    else
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown forced file type: %s", pszType);
    return RTEXITCODE_SUCCESS;
}

#endif /* !IPRT_SIGNTOOL_NO_SIGNING */

/**
 * Detects the type of files @a pszFile is (by reading from it).
 *
 * @returns The file type, or RTSIGNTOOLFILETYPE_UNKNOWN (error displayed).
 * @param   enmForceFileType    Usually set to RTSIGNTOOLFILETYPE_DETECT, but if
 *                              not we'll return this without probing the file.
 * @param   pszFile             The name of the file to detect the type of.
 */
static RTSIGNTOOLFILETYPE DetectFileType(RTSIGNTOOLFILETYPE enmForceFileType, const char *pszFile)
{
    /*
     * Forced?
     */
    if (enmForceFileType != RTSIGNTOOLFILETYPE_DETECT)
        return enmForceFileType;

    /*
     * Read the start of the file.
     */
    RTFILE hFile = NIL_RTFILE;
    int rc = RTFileOpen(&hFile, pszFile, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_FAILURE(rc))
    {
        RTMsgError("Error opening '%s' for reading: %Rrc", pszFile, rc);
        return RTSIGNTOOLFILETYPE_UNKNOWN;
    }

    union
    {
        uint8_t     ab[256];
        uint16_t    au16[256/2];
        uint32_t    au32[256/4];
    } uBuf;
    RT_ZERO(uBuf);

    size_t cbRead = 0;
    rc = RTFileRead(hFile, &uBuf, sizeof(uBuf), &cbRead);
    if (RT_FAILURE(rc))
        RTMsgError("Error reading from '%s': %Rrc", pszFile, rc);

    uint64_t cbFile;
    int rcSize = RTFileQuerySize(hFile, &cbFile);
    if (RT_FAILURE(rcSize))
        RTMsgError("Error querying size of '%s': %Rrc", pszFile, rc);

    RTFileClose(hFile);
    if (RT_FAILURE(rc) || RT_FAILURE(rcSize))
        return RTSIGNTOOLFILETYPE_UNKNOWN;

    /*
     * Try guess the kind of file.
     */
    /* All the executable magics we know: */
    if (   uBuf.au16[0] == RT_H2LE_U16_C(IMAGE_DOS_SIGNATURE)
        || uBuf.au16[0] == RT_H2LE_U16_C(IMAGE_NE_SIGNATURE)
        || uBuf.au16[0] == RT_H2LE_U16_C(IMAGE_LX_SIGNATURE)
        || uBuf.au16[0] == RT_H2LE_U16_C(IMAGE_LE_SIGNATURE)
        || uBuf.au32[0] == RT_H2LE_U32_C(IMAGE_NT_SIGNATURE)
        || uBuf.au32[0] == RT_H2LE_U32_C(IMAGE_ELF_SIGNATURE)
        || uBuf.au32[0] == IMAGE_FAT_SIGNATURE
        || uBuf.au32[0] == IMAGE_FAT_SIGNATURE_OE
        || uBuf.au32[0] == IMAGE_MACHO32_SIGNATURE
        || uBuf.au32[0] == IMAGE_MACHO32_SIGNATURE_OE
        || uBuf.au32[0] == IMAGE_MACHO64_SIGNATURE
        || uBuf.au32[0] == IMAGE_MACHO64_SIGNATURE_OE)
        return RTSIGNTOOLFILETYPE_EXE;

    /*
     * Catalog files are PKCS#7 SignedData and starts with a ContentInfo, i.e.:
     *  SEQUENCE {
     *      contentType OBJECT IDENTIFIER,
     *      content [0] EXPLICIT ANY DEFINED BY contentType OPTIONAL
     *  }
     *
     * We ASSUME that it's DER encoded and doesn't use an indefinite length form
     * at the start and that contentType is signedData (1.2.840.113549.1.7.2).
     *
     * Example of a 10353 (0x2871) byte long file:
     *                       vv-------- contentType -------vv
     * 00000000  30 82 28 6D 06 09 2A 86 48 86 F7 0D 01 07 02 A0
     * 00000010  82 28 5E 30 82 28 5A 02 01 01 31 0B 30 09 06 05
     */
    if (   uBuf.ab[0] == (ASN1_TAG_SEQUENCE | ASN1_TAGFLAG_CONSTRUCTED)
        && uBuf.ab[1] != 0x80 /* not indefinite form */
        && uBuf.ab[1] >  0x30)
    {
        size_t   off   = 1;
        uint32_t cbRec = uBuf.ab[1];
        if (cbRec & 0x80)
        {
            cbRec &= 0x7f;
            off   += cbRec;
            switch (cbRec)
            {
                case 1: cbRec =                     uBuf.ab[2]; break;
                case 2: cbRec = RT_MAKE_U16(        uBuf.ab[3], uBuf.ab[2]); break;
                case 3: cbRec = RT_MAKE_U32_FROM_U8(uBuf.ab[4], uBuf.ab[3], uBuf.ab[2], 0); break;
                case 4: cbRec = RT_MAKE_U32_FROM_U8(uBuf.ab[5], uBuf.ab[4], uBuf.ab[3], uBuf.ab[2]); break;
                default: cbRec = UINT32_MAX; break;
            }
        }
        if (off <= 5)
        {
            off++;
            if (off + cbRec == cbFile)
            {
                /* If the contentType is signedData we're going to treat it as a catalog file,
                   we don't currently much care about the signed content of a cat file. */
                static const uint8_t s_abSignedDataOid[] =
                { ASN1_TAG_OID, 9 /*length*/, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x07, 0x02 };
                if (memcmp(&uBuf.ab[off], s_abSignedDataOid, sizeof(s_abSignedDataOid)) == 0)
                    return RTSIGNTOOLFILETYPE_CAT;
            }
        }
    }

    RTMsgError("Unable to detect type of '%s'", pszFile);
    return RTSIGNTOOLFILETYPE_UNKNOWN;
}


/*********************************************************************************************************************************
*   The 'extract-exe-signer-cert' command.                                                                                       *
*********************************************************************************************************************************/

static RTEXITCODE HelpExtractExeSignerCert(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT,
                        "extract-exe-signer-cert [--as-c-array=name|--ber|--cer|--der] [--signature-index|-i <num>] [--input|--exe|-e] <exe> [--output|-o] <outfile.cer/h>\n");
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE WriteCertToFile(PCRTCRX509CERTIFICATE pCert, const char *pszFilename, bool fForce)
{
    RTEXITCODE rcExit = RTEXITCODE_FAILURE;
    RTFILE     hFile;
    int rc = RTFileOpen(&hFile, pszFilename,
                        RTFILE_O_WRITE | RTFILE_O_DENY_WRITE | (fForce ? RTFILE_O_CREATE_REPLACE : RTFILE_O_CREATE));
    if (RT_SUCCESS(rc))
    {
        uint32_t cbCert = pCert->SeqCore.Asn1Core.cbHdr + pCert->SeqCore.Asn1Core.cb;
        rc = RTFileWrite(hFile, pCert->SeqCore.Asn1Core.uData.pu8 - pCert->SeqCore.Asn1Core.cbHdr,
                         cbCert, NULL);
        if (RT_SUCCESS(rc))
        {
            rc = RTFileClose(hFile);
            if (RT_SUCCESS(rc))
            {
                hFile  = NIL_RTFILE;
                rcExit = RTEXITCODE_SUCCESS;
                RTMsgInfo("Successfully wrote %u bytes to '%s'", cbCert, pszFilename);
            }
            else
                RTMsgError("RTFileClose failed: %Rrc", rc);
        }
        else
            RTMsgError("RTFileWrite failed: %Rrc", rc);
        RTFileClose(hFile);
    }
    else
        RTMsgError("Error opening '%s' for writing: %Rrc", pszFilename, rc);
    return rcExit;
}


static void PrintCertAsCArray(PCRTCRX509CERTIFICATE pCert, uint32_t iSignature, const char *pszBaseNm, PRTSTREAM pStrm)
{
    uint32_t const        cbCert = pCert->SeqCore.Asn1Core.cbHdr     + pCert->SeqCore.Asn1Core.cb;
    uint8_t const * const pbCert = pCert->SeqCore.Asn1Core.uData.pu8 - pCert->SeqCore.Asn1Core.cbHdr;
    if (iSignature == UINT32_MAX)
        RTStrmPrintf(pStrm,
                     "uint32_t const g_cb%s = %u;\n"
                     "uint8_t const  g_ab%s[%u] =\n"
                     "{",
                     pszBaseNm, cbCert, pszBaseNm, cbCert);
    else
        RTStrmPrintf(pStrm, "static uint8_t const g_ab%sCert%u[%u] =\n{", pszBaseNm, iSignature, cbCert);
    for (uint32_t off = 0; off < cbCert; off++)
    {
        if (off % 16 == 0)
            RTStrmPrintf(pStrm, "\n   ");
        RTStrmPrintf(pStrm, " %#04x,", pbCert[off]);
    }
    RTStrmPrintf(pStrm, "\n};\n\n");
}


static void PrintCertTableAsC(uint32_t cSignatures, const char *pszBaseNm, PRTSTREAM pStrm)
{
    RTStrmPrintf(pStrm,
                 "uint32_t const g_c%s = %u;\n"
                 "struct { uint8_t const *pbCert; size_t cbCert; } const g_a%s[%u] =\n"
                 "{\n"
                 , pszBaseNm, cSignatures, pszBaseNm, cSignatures ? cSignatures : 1);
    for (uint32_t iSignature = 0; iSignature < cSignatures; iSignature++)
        RTStrmPrintf(pStrm, "    { g_ab%sCert%u, sizeof(g_ab%sCert%u) },\n",
                     pszBaseNm, iSignature, pszBaseNm, iSignature);
    if (!cSignatures)
        RTStrmPrintf(pStrm, "    { NULL, 0 } /* dummy */ \n");
    RTStrmPrintf(pStrm, "};\n");
}


static RTEXITCODE WriteCertToFileAsC(PCRTCRX509CERTIFICATE pCert, const char *pszFilename, bool fForce, const char *pszBaseNm)
{
    RTEXITCODE rcExit;
    PRTSTREAM  pStrm  = NULL;
    int rc = RTStrmOpen(pszFilename, fForce ? "wt+" : "wtx", &pStrm);
    if (RT_SUCCESS(rc))
    {
        PrintCertAsCArray(pCert, UINT32_MAX, pszBaseNm, pStrm);
        rc = RTStrmClose(pStrm);
        if (RT_SUCCESS(rc))
            rcExit = RTEXITCODE_SUCCESS;
        else
            rcExit = RTMsgErrorExitFailure("Error writing/closing '%s': %Rrc", pszFilename, rc);
    }
    else
        rcExit = RTMsgErrorExitFailure("Failed to open '%s' for writing: %Rrc", pszFilename, rc);
    return rcExit;
}



static RTEXITCODE HandleExtractExeSignerCert(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--ber",              'b', RTGETOPT_REQ_NOTHING },
        { "--cer",              'c', RTGETOPT_REQ_NOTHING },
        { "--der",              'd', RTGETOPT_REQ_NOTHING },
        { "--exe",              'e', RTGETOPT_REQ_STRING  },
        { "--input",            'e', RTGETOPT_REQ_STRING  },
        { "--output",           'o', RTGETOPT_REQ_STRING  },
        { "--signature-index",  'i', RTGETOPT_REQ_UINT32  },
        { "--force",            'f', RTGETOPT_REQ_NOTHING },
        { "--as-c-array",       'C', RTGETOPT_REQ_STRING  },
    };

    const char *pszExe = NULL;
    const char *pszOut = NULL;
    RTLDRARCH   enmLdrArch   = RTLDRARCH_WHATEVER;
    unsigned    cVerbosity   = 0;
    uint32_t    fCursorFlags = RTASN1CURSOR_FLAGS_DER;
    uint32_t    iSignature   = 0;
    const char *pszCArrayNm  = NULL;
    bool        fForce       = false;

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'e':   pszExe = ValueUnion.psz; break;
            case 'o':   pszOut = ValueUnion.psz; break;
            case 'b':   fCursorFlags = 0; break;
            case 'c':   fCursorFlags = RTASN1CURSOR_FLAGS_CER; break;
            case 'd':   fCursorFlags = RTASN1CURSOR_FLAGS_DER; break;
            case 'f':   fForce = true; break;
            case 'i':   iSignature = ValueUnion.u32; break;
            case 'V':   return HandleVersion(cArgs, papszArgs);
            case 'h':   return HelpExtractExeSignerCert(g_pStdOut, RTSIGNTOOLHELP_FULL);

            case 'C':
                pszCArrayNm = *ValueUnion.psz ? ValueUnion.psz : NULL;
                if (pszCArrayNm)
                    iSignature = UINT32_MAX;
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (!pszExe)
                    pszExe = ValueUnion.psz;
                else if (!pszOut)
                    pszOut = ValueUnion.psz;
                else
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Too many file arguments: %s", ValueUnion.psz);
                break;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (!pszExe)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No executable given.");
    if (!pszOut)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No output file given.");
    if (!fForce && RTPathExists(pszOut))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "The output file '%s' exists.", pszOut);

    /*
     * Do it.
     */
    /* Read & decode the PKCS#7 signature. */
    SIGNTOOLPKCS7EXE This;
    RTEXITCODE rcExit = SignToolPkcs7Exe_InitFromFile(&This, pszExe, cVerbosity, enmLdrArch);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        if (pszCArrayNm == NULL || iSignature != UINT32_MAX)
        {
            /* Find the signing certificate (ASSUMING that the certificate used is shipped in the set of certificates). */
            PRTCRPKCS7SIGNEDDATA  pSignedData;
            PCRTCRPKCS7SIGNERINFO pSignerInfo = SignToolPkcs7_FindNestedSignatureByIndex(&This, iSignature, &pSignedData);
            rcExit = RTEXITCODE_FAILURE;
            if (pSignerInfo)
            {
                PCRTCRPKCS7ISSUERANDSERIALNUMBER pISN = &pSignedData->SignerInfos.papItems[0]->IssuerAndSerialNumber;
                PCRTCRX509CERTIFICATE pCert;
                pCert = RTCrPkcs7SetOfCerts_FindX509ByIssuerAndSerialNumber(&pSignedData->Certificates,
                                                                            &pISN->Name, &pISN->SerialNumber);
                if (pCert)
                {
                    /*
                     * Write it out.
                     */
                    if (pszCArrayNm == NULL)
                        rcExit = WriteCertToFile(pCert, pszOut, fForce);
                    else
                        rcExit = WriteCertToFileAsC(pCert, pszOut, fForce, pszCArrayNm);
                }
                else
                    RTMsgError("Certificate not found.");
            }
            else
                RTMsgError("Could not locate signature #%u!", iSignature);
        }
        else
        {
            uint32_t const cSignatures = SignToolPkcs7_CountSignatures(&This);
            if (cSignatures)
            {
                PRTSTREAM pStrm = NULL;
                rc = RTStrmOpen(pszOut, fForce ? "wt+" : "wtx", &pStrm);
                if (RT_SUCCESS(rc))
                {
                    for (iSignature = 0; iSignature < cSignatures; iSignature++)
                    {
                        PRTCRPKCS7SIGNEDDATA  pSignedData;
                        PCRTCRPKCS7SIGNERINFO pSignerInfo = SignToolPkcs7_FindNestedSignatureByIndex(&This, iSignature, &pSignedData);
                        if (!pSignerInfo)
                        {
                            rcExit = RTMsgErrorExitFailure("Could not locate signature #%u out of %u!", iSignature, cSignatures);
                            break;
                        }
                        PCRTCRPKCS7ISSUERANDSERIALNUMBER pISN = &pSignedData->SignerInfos.papItems[0]->IssuerAndSerialNumber;
                        PCRTCRX509CERTIFICATE pCert;
                        pCert = RTCrPkcs7SetOfCerts_FindX509ByIssuerAndSerialNumber(&pSignedData->Certificates,
                                                                                    &pISN->Name, &pISN->SerialNumber);
                        if (pCert)
                            PrintCertAsCArray(pCert, iSignature, pszCArrayNm, pStrm);
                        else
                        {
                            rcExit = RTMsgErrorExitFailure("Could not locate certificate for signature #%u (out of %u)!",
                                                           iSignature, cSignatures);
                            break;
                        }
                    }

                    PrintCertTableAsC(iSignature, pszCArrayNm, pStrm);

                    rc = RTStrmClose(pStrm);
                    if (RT_FAILURE(rc))
                        rcExit = RTMsgErrorExitFailure("Error writing/closing '%s': %Rrc", pszOut, rc);
                }
                else
                    rcExit = RTMsgErrorExitFailure("Failed to open '%s' for writing: %Rrc", pszOut, rc);
            }
            else
                rcExit = RTMsgErrorExitFailure("No signatures found!");
        }

        /* Delete the signature data. */
        SignToolPkcs7Exe_Delete(&This);
    }
    return rcExit;
}


/*********************************************************************************************************************************
*   The 'extract-signer-root' & 'extract-timestamp-root' commands.                                                               *
*********************************************************************************************************************************/
class BaseExtractState
{
public:
    const char *pszFile;
    const char *pszOut;
    RTLDRARCH   enmLdrArch;
    unsigned    cVerbosity;
    uint32_t    iSignature;
    bool        fForce;
    /** Timestamp or main signature. */
    bool const  fTimestamp;
    const char *pszBaseNm;

    BaseExtractState(bool a_fTimestamp)
        : pszFile(NULL)
        , pszOut(NULL)
        , enmLdrArch(RTLDRARCH_WHATEVER)
        , cVerbosity(0)
        , iSignature(0)
        , fForce(false)
        , fTimestamp(a_fTimestamp)
        , pszBaseNm(NULL)
    {
    }
};

class RootExtractState : public BaseExtractState
{
public:
    CryptoStore RootStore;
    CryptoStore AdditionalStore;

    RootExtractState(bool a_fTimestamp)
        : BaseExtractState(a_fTimestamp)
        , RootStore()
        , AdditionalStore()
    { }

    /**
     * Creates the two stores, filling the root one with trusted CAs and
     * certificates found on the system or in the user's account.
     */
    bool init(void)
    {
        int rc = RTCrStoreCreateInMem(&this->RootStore.m_hStore, 0);
        if (RT_SUCCESS(rc))
        {
            rc = RTCrStoreCreateInMem(&this->AdditionalStore.m_hStore, 0);
            if (RT_SUCCESS(rc))
                return true;
        }
        RTMsgError("RTCrStoreCreateInMem failed: %Rrc", rc);
        return false;
    }
};


/**
 * Locates the target signature and certificate collection.
 */
static PRTCRPKCS7SIGNERINFO BaseExtractFindSignerInfo(SIGNTOOLPKCS7 *pThis, BaseExtractState *pState, bool fOptional,
                                                      PRTCRPKCS7SIGNEDDATA  *ppSignedData, PCRTCRPKCS7SETOFCERTS *ppCerts)
{
    *ppSignedData = NULL;
    *ppCerts      = NULL;

    /*
     * Locate the target signature.
     */
    PRTCRPKCS7SIGNEDDATA pSignedData = NULL;
    PRTCRPKCS7SIGNERINFO pSignerInfo = SignToolPkcs7_FindNestedSignatureByIndex(pThis, pState->iSignature, &pSignedData);
    if (pSignerInfo)
    {
        /*
         * If the target is the timestamp we have to locate the relevant
         * timestamp signature and adjust the return values.
         */
        if (pState->fTimestamp)
        {
            for (uint32_t iItem = 0; iItem < pSignerInfo->UnauthenticatedAttributes.cItems; iItem++)
            {
                PCRTCRPKCS7ATTRIBUTE pAttr = pSignerInfo->UnauthenticatedAttributes.papItems[iItem];
                if (pAttr->enmType == RTCRPKCS7ATTRIBUTETYPE_COUNTER_SIGNATURES)
                {
                    /* ASSUME that all counter signatures are timestamping. */
                    if (pAttr->uValues.pCounterSignatures->cItems > 0)
                    {
                        *ppSignedData = pSignedData;
                        *ppCerts      = &pSignedData->Certificates;
                        return pAttr->uValues.pCounterSignatures->papItems[0];
                    }
                    RTMsgWarning("Timestamp signature attribute is empty!");
                }
                else if (pAttr->enmType == RTCRPKCS7ATTRIBUTETYPE_MS_TIMESTAMP)
                {
                    /* ASSUME that all valid timestamp signatures for now, pick the first. */
                    if (pAttr->uValues.pContentInfos->cItems > 0)
                    {
                        PCRTCRPKCS7CONTENTINFO pContentInfo = pAttr->uValues.pContentInfos->papItems[0];
                        if (RTAsn1ObjId_CompareWithString(&pContentInfo->ContentType, RTCR_PKCS7_SIGNED_DATA_OID) == 0)
                        {
                            pSignedData = pContentInfo->u.pSignedData;
                            if (RTAsn1ObjId_CompareWithString(&pSignedData->ContentInfo.ContentType, RTCRTSPTSTINFO_OID) == 0)
                            {
                                if (pSignedData->SignerInfos.cItems > 0)
                                {
                                    *ppSignedData = pSignedData;
                                    *ppCerts      = &pSignedData->Certificates;
                                    return pSignedData->SignerInfos.papItems[0];
                                }
                                RTMsgWarning("Timestamp signature has no signers!");
                            }
                            else
                                RTMsgWarning("Timestamp signature contains wrong content (%s)!",
                                             pSignedData->ContentInfo.ContentType.szObjId);
                        }
                        else
                            RTMsgWarning("Timestamp signature is not SignedData but %s!", pContentInfo->ContentType.szObjId);
                    }
                    else
                        RTMsgWarning("Timestamp signature attribute is empty!");
                }
            }
            if (!fOptional)
                RTMsgError("Cound not find a timestamp signature associated with signature #%u!", pState->iSignature);
            pSignerInfo = NULL;
        }
        else
        {
            *ppSignedData = pSignedData;
            *ppCerts      = &pSignedData->Certificates;
        }
    }
    else if (!fOptional)
        RTMsgError("Could not locate signature #%u!", pState->iSignature);
    return pSignerInfo;
}


/** @callback_method_impl{FNRTDUMPPRINTFV} */
static DECLCALLBACK(void) DumpToStdOutPrintfV(void *pvUser, const char *pszFormat, va_list va)
{
    RT_NOREF(pvUser);
    RTPrintfV(pszFormat, va);
}


static RTEXITCODE RootExtractWorker3(SIGNTOOLPKCS7 *pThis, RootExtractState *pState, PRTSTREAM pStrm, uint32_t *pidxCert,
                                     PRTERRINFOSTATIC pStaticErrInfo)
{
    /*
     * Locate the target signature.
     */
    bool const            fOptional = pidxCert != NULL && pStrm != NULL && pState->fTimestamp /*fOptional*/;
    PRTCRPKCS7SIGNEDDATA  pSignedData;
    PCRTCRPKCS7SETOFCERTS pCerts;
    PCRTCRPKCS7SIGNERINFO pSignerInfo = BaseExtractFindSignerInfo(pThis, pState, fOptional, &pSignedData, &pCerts);
    if (!pSignerInfo)
    {
        if (!fOptional)
            return RTMsgErrorExitFailure("Could not locate signature #%u!", pState->iSignature);
        return RTEXITCODE_SUCCESS;
    }


    /* The next bit is modelled on first half of rtCrPkcs7VerifySignerInfo. */

    /*
     * Locate the signing certificate.
     */
    PCRTCRCERTCTX pSignerCertCtx = RTCrStoreCertByIssuerAndSerialNo(pState->RootStore.m_hStore,
                                                                    &pSignerInfo->IssuerAndSerialNumber.Name,
                                                                    &pSignerInfo->IssuerAndSerialNumber.SerialNumber);
    if (!pSignerCertCtx)
        pSignerCertCtx = RTCrStoreCertByIssuerAndSerialNo(pState->AdditionalStore.m_hStore,
                                                          &pSignerInfo->IssuerAndSerialNumber.Name,
                                                          &pSignerInfo->IssuerAndSerialNumber.SerialNumber);

    PCRTCRX509CERTIFICATE pSignerCert;
    if (pSignerCertCtx)
        pSignerCert = pSignerCertCtx->pCert;
    else
    {
        pSignerCert = RTCrPkcs7SetOfCerts_FindX509ByIssuerAndSerialNumber(pCerts,
                                                                          &pSignerInfo->IssuerAndSerialNumber.Name,
                                                                          &pSignerInfo->IssuerAndSerialNumber.SerialNumber);
        if (!pSignerCert)
            return RTMsgErrorExitFailure("Certificate not found: serial=%.*Rhxs",
                                         pSignerInfo->IssuerAndSerialNumber.SerialNumber.Asn1Core.cb,
                                         pSignerInfo->IssuerAndSerialNumber.SerialNumber.Asn1Core.uData.pv);
    }

    /*
     * Now we build paths so we can get to the root certificate.
     */
    RTCRX509CERTPATHS hCertPaths;
    int rc = RTCrX509CertPathsCreate(&hCertPaths, pSignerCert);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrX509CertPathsCreate failed: %Rrc", rc);

    /* Configure: */
    RTEXITCODE rcExit = RTEXITCODE_FAILURE;
    rc = RTCrX509CertPathsSetTrustedStore(hCertPaths, pState->RootStore.m_hStore);
    if (RT_SUCCESS(rc))
    {
        rc = RTCrX509CertPathsSetUntrustedStore(hCertPaths, pState->AdditionalStore.m_hStore);
        if (RT_SUCCESS(rc))
        {
            rc = RTCrX509CertPathsSetUntrustedSet(hCertPaths, pCerts);
            if (RT_SUCCESS(rc))
            {
                /* We don't technically need this, I think. */
                rc = RTCrX509CertPathsSetTrustAnchorChecks(hCertPaths, true /*fEnable*/);
                if (RT_SUCCESS(rc))
                {
                    /* Seems we might need this for the sha-1 certs and such. */
                    RTCrX509CertPathsSetValidTimeSpec(hCertPaths, NULL);

                    /* Build the paths: */
                    rc = RTCrX509CertPathsBuild(hCertPaths, RTErrInfoInitStatic(pStaticErrInfo));
                    if (RT_SUCCESS(rc))
                    {
                        uint32_t const cPaths = RTCrX509CertPathsGetPathCount(hCertPaths);

                        /* Validate the paths: */
                        uint32_t cValidPaths = 0;
                        rc = RTCrX509CertPathsValidateAll(hCertPaths, &cValidPaths, RTErrInfoInitStatic(pStaticErrInfo));
                        if (RT_SUCCESS(rc))
                        {
                            if (pState->cVerbosity > 0)
                                RTMsgInfo("%u of %u paths are valid", cValidPaths, cPaths);
                            if (pState->cVerbosity > 1)
                                RTCrX509CertPathsDumpAll(hCertPaths, pState->cVerbosity, DumpToStdOutPrintfV, NULL);

                            /*
                             * Now, pick the first valid path with a real certificate at the end.
                             */
                            for (uint32_t iPath = 0; iPath < cPaths; iPath++)
                            {
                                PCRTCRX509CERTIFICATE pRootCert = NULL;
                                PCRTCRX509NAME        pSubject  = NULL;
                                bool                  fTrusted  = false;
                                int                   rcVerify  = -1;
                                rc = RTCrX509CertPathsQueryPathInfo(hCertPaths, iPath, &fTrusted, NULL /*pcNodes*/,
                                                                    &pSubject, NULL, &pRootCert, NULL /*ppCertCtx*/, &rcVerify);
                                if (RT_SUCCESS(rc))
                                {
                                    if (fTrusted && RT_SUCCESS(rcVerify) && pRootCert)
                                    {
                                        /*
                                         * Now copy out the certificate.
                                         */
                                        if (!pState->pszBaseNm)
                                            rcExit = WriteCertToFile(pRootCert, pState->pszOut, pState->fForce);
                                        else if (!pStrm)
                                            rcExit = WriteCertToFileAsC(pRootCert, pState->pszOut, pState->fForce,
                                                                        pState->pszBaseNm);
                                        else
                                        {
                                            PrintCertAsCArray(pRootCert, *pidxCert, pState->pszBaseNm, pStrm);
                                            *pidxCert += 1;
                                            rcExit = RTEXITCODE_SUCCESS;
                                        }
                                        break;
                                    }
                                }
                                else
                                {
                                    RTMsgError("RTCrX509CertPathsQueryPathInfo failed: %Rrc", rc);
                                    break;
                                }
                            }
                        }
                        else
                        {
                            RTMsgError("RTCrX509CertPathsValidateAll failed: %Rrc%#RTeim", rc, &pStaticErrInfo->Core);
                            RTCrX509CertPathsDumpAll(hCertPaths, pState->cVerbosity, DumpToStdOutPrintfV, NULL);
                        }
                    }
                    else
                        RTMsgError("RTCrX509CertPathsBuild failed: %Rrc%#RTeim", rc, &pStaticErrInfo->Core);
                }
                else
                    RTMsgError("RTCrX509CertPathsSetTrustAnchorChecks failed: %Rrc", rc);
            }
            else
                RTMsgError("RTCrX509CertPathsSetUntrustedSet failed: %Rrc", rc);
        }
        else
            RTMsgError("RTCrX509CertPathsSetUntrustedStore failed: %Rrc", rc);
    }
    else
        RTMsgError("RTCrX509CertPathsSetTrustedStore failed: %Rrc", rc);

    uint32_t cRefs = RTCrX509CertPathsRelease(hCertPaths);
    Assert(cRefs == 0); RT_NOREF(cRefs);

    return rcExit;
}


static RTEXITCODE RootExtractWorker2(SIGNTOOLPKCS7 *pThis, RootExtractState *pState, PRTERRINFOSTATIC pStaticErrInfo)
{
    if (!pState->pszBaseNm || pState->iSignature != UINT32_MAX)
        return RootExtractWorker3(pThis, pState, NULL, NULL, pStaticErrInfo);

    /*
     * Dump all these certificates.
     */
    uint32_t const cSignatures = SignToolPkcs7_CountSignatures(pThis);
    if (!cSignatures)
        return RTMsgErrorExitFailure("No signatures found!");

    PRTSTREAM pStrm = NULL;
    int rc = RTStrmOpen(pState->pszOut, pState->fForce ? "wt+" : "wtx", &pStrm);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("Failed to open '%s' for writing: %Rrc", pState->pszOut, rc);

    uint32_t   idxCert = 0; /* tracking separately, as we ignore missing timestamp chains. */
    RTEXITCODE rcExit  = RTEXITCODE_SUCCESS;
    for (uint32_t iSignature = 0; iSignature < cSignatures && rcExit == RTEXITCODE_SUCCESS; iSignature++)
    {
        pState->iSignature = iSignature;
        rcExit = RootExtractWorker3(pThis, pState, pStrm, &idxCert, pStaticErrInfo);
    }

    PrintCertTableAsC(idxCert, pState->pszBaseNm, pStrm);

    rc = RTStrmClose(pStrm);
    if (RT_FAILURE(rc))
        rcExit = RTMsgErrorExitFailure("Error writing/closing '%s': %Rrc", pState->pszOut, rc);
    return rcExit;
}


static RTEXITCODE RootExtractWorker(RootExtractState *pState, PRTERRINFOSTATIC pStaticErrInfo)
{
    /*
     * Check that all we need is there and whether the output file exists.
     */
    if (!pState->pszFile)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No executable given.");
    if (!pState->pszOut)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No output file given.");
    if (!pState->fForce && RTPathExists(pState->pszOut))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "The output file '%s' exists.", pState->pszOut);

    /*
     * Detect the type of file we're dealing with, do type specific setup and
     * call common worker to do the rest.
     */
    RTEXITCODE         rcExit;
    RTSIGNTOOLFILETYPE enmFileType = DetectFileType(RTSIGNTOOLFILETYPE_DETECT, pState->pszFile);
    if (enmFileType == RTSIGNTOOLFILETYPE_EXE)
    {
        SIGNTOOLPKCS7EXE Exe;
        rcExit = SignToolPkcs7Exe_InitFromFile(&Exe, pState->pszFile, pState->cVerbosity, pState->enmLdrArch);
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            rcExit = RootExtractWorker2(&Exe, pState, pStaticErrInfo);
            SignToolPkcs7Exe_Delete(&Exe);
        }
    }
    else if (enmFileType == RTSIGNTOOLFILETYPE_CAT)
    {
        SIGNTOOLPKCS7 Cat;
        rcExit = SignToolPkcs7_InitFromFile(&Cat, pState->pszFile, pState->cVerbosity);
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            rcExit = RootExtractWorker2(&Cat, pState, pStaticErrInfo);
            SignToolPkcs7_Delete(&Cat);
        }
    }
    else
        rcExit = RTEXITCODE_FAILURE;
    return rcExit;
}


static RTEXITCODE HelpExtractRootCommon(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel, bool fTimestamp)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT,
                        "extract-%s-root [-v|--verbose] [-q|--quiet] [--as-c-array <name>] [--signature-index|-i <num>] "
                        "[--root <root-cert.der>] [--self-signed-roots-from-system] [--additional <supp-cert.der>] "
                        "[--intermediate-certs-from-system] [--input] <signed-file> "
                        "[-f|--force] [--output|-o] <outfile.cer/h>\n",
                        fTimestamp ? "timestamp" : "signer");
    if (enmLevel == RTSIGNTOOLHELP_FULL)
    {
        RTStrmWrappedPrintf(pStrm, 0,
                            "\n"
                            "Extracts the root certificate of the %sgiven "
                            "signature.  If there are more than one valid certificate path, the first one with "
                            "a full certificate will be picked.\n",
                            fTimestamp ? "first timestamp associated with the " : "");
        RTStrmWrappedPrintf(pStrm, 0,
                            "\n"
                            "Options:\n"
                            "  -v, --verbose, -q, --quite\n"
                            "    Controls the noise level.  The '-v' options are accumlative while '-q' is absolute.\n"
                            "    Default: -q\n"
                            "  -C <name>, --as-c-array <name>\n"
                            "    Output a C header file containing the roots of all signatures (or a selected one if"
                            "--signature-index is used again after this option.\n"
                            "    Default: Output one binary certificate.\n"
                            "  -i <num>, --signature-index <num>\n"
                            "    Zero-based index of the signature to extract the root for.\n"
                            "    Default: -i 0\n"
                            "  -r <root-cert.file>, --root <root-cert.file>\n"
                            "    Use the certificate(s) in the specified file as a trusted root(s). "
                            "The file format can be PEM or DER.\n"
                            "  -R, --self-signed-roots-from-system\n"
                            "    Use all self-signed trusted root certificates found on the system and associated with the "
                            "current user as trusted roots.  This is limited to self-signed certificates, so that we get "
                            "a full chain even if a non-end-entity certificate is present in any of those system stores for "
                            "some reason.\n"
                            "  -a <supp-cert.file>, --additional <supp-cert.file>\n"
                            "    Use the certificate(s) in the specified file as a untrusted intermediate certificates. "
                            "The file format can be PEM or DER.\n"
                            "  -A, --intermediate-certs-from-system\n"
                            "    Use all certificates found on the system and associated with the current user as intermediate "
                            "certification authorities.\n"
                            "  --input <signed-file>\n"
                            "    Signed executable or security cabinet file to examine.  The '--input' option bit is optional "
                            "and there to allow more flexible parameter ordering.\n"
                            "  -f, --force\n"
                            "    Overwrite existing output file.  The default is not to overwriting any existing file.\n"
                            "  -o <outfile.cer> --output <outfile.cer>\n"
                            "    The name of the output file.  Again the '-o|--output' bit is optional and only for flexibility.\n"
                            );
    }
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE HandleExtractRootCommon(int cArgs, char **papszArgs, bool fTimestamp)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--root",                          'r', RTGETOPT_REQ_STRING },
        { "--self-signed-roots-from-system", 'R', RTGETOPT_REQ_NOTHING },
        { "--additional",                    'a', RTGETOPT_REQ_STRING },
        { "--intermediate-certs-from-system",'A', RTGETOPT_REQ_NOTHING },
        { "--add",                           'a', RTGETOPT_REQ_STRING },
        { "--input",                         'I', RTGETOPT_REQ_STRING },
        { "--output",                        'o', RTGETOPT_REQ_STRING  },
        { "--signature-index",               'i', RTGETOPT_REQ_UINT32  },
        { "--force",                         'f', RTGETOPT_REQ_NOTHING },
        { "--verbose",                       'v', RTGETOPT_REQ_NOTHING },
        { "--quiet",                         'q', RTGETOPT_REQ_NOTHING },
        { "--as-c-array",                    'C', RTGETOPT_REQ_STRING  },
    };
    RTERRINFOSTATIC  StaticErrInfo;
    RootExtractState State(fTimestamp);
    if (!State.init())
        return RTEXITCODE_FAILURE;
    RTGETOPTSTATE    GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'a':
                if (!State.AdditionalStore.addFromFile(ValueUnion.psz, &StaticErrInfo))
                    return RTEXITCODE_FAILURE;
                break;

            case 'A':
                if (!State.AdditionalStore.addIntermediateCertsFromSystem(&StaticErrInfo))
                    return RTEXITCODE_FAILURE;
                break;

            case 'r':
                if (!State.RootStore.addFromFile(ValueUnion.psz, &StaticErrInfo))
                    return RTEXITCODE_FAILURE;
                break;

            case 'R':
                if (!State.RootStore.addSelfSignedRootsFromSystem(&StaticErrInfo))
                    return RTEXITCODE_FAILURE;
                break;

            case 'I':   State.pszFile = ValueUnion.psz; break;
            case 'o':   State.pszOut = ValueUnion.psz; break;
            case 'f':   State.fForce = true; break;
            case 'i':   State.iSignature = ValueUnion.u32; break;
            case 'v':   State.cVerbosity++; break;
            case 'q':   State.cVerbosity = 0; break;
            case 'V':   return HandleVersion(cArgs, papszArgs);
            case 'h':   return HelpExtractRootCommon(g_pStdOut, RTSIGNTOOLHELP_FULL, fTimestamp);

            case 'C':
                State.pszBaseNm = *ValueUnion.psz ? ValueUnion.psz : NULL;
                if (State.pszBaseNm)
                    State.iSignature = UINT32_MAX;
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (!State.pszFile)
                    State.pszFile = ValueUnion.psz;
                else if (!State.pszOut)
                    State.pszOut = ValueUnion.psz;
                else
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Too many file arguments: %s", ValueUnion.psz);
                break;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    return RootExtractWorker(&State, &StaticErrInfo);
}


static RTEXITCODE HelpExtractSignerRoot(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    return HelpExtractRootCommon(pStrm, enmLevel, false /*fTimestamp*/);
}


static RTEXITCODE HandleExtractSignerRoot(int cArgs, char **papszArgs)
{
    return HandleExtractRootCommon(cArgs, papszArgs, false /*fTimestamp*/ );
}


static RTEXITCODE HelpExtractTimestampRoot(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    return HelpExtractRootCommon(pStrm, enmLevel, true /*fTimestamp*/);
}


static RTEXITCODE HandleExtractTimestampRoot(int cArgs, char **papszArgs)
{
    return HandleExtractRootCommon(cArgs, papszArgs, true /*fTimestamp*/ );
}


/*********************************************************************************************************************************
*   The 'extract-exe-signature' command.                                                                                         *
*********************************************************************************************************************************/

static RTEXITCODE HelpExtractExeSignature(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT,
                        "extract-exe-signerature [--input|--exe|-e] <exe> [--output|-o] <outfile.pkcs7>\n");
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE HandleExtractExeSignature(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--exe",              'e', RTGETOPT_REQ_STRING  },
        { "--input",            'e', RTGETOPT_REQ_STRING  },
        { "--output",           'o', RTGETOPT_REQ_STRING  },
        { "--force",            'f', RTGETOPT_REQ_NOTHING  },
    };

    const char *pszExe = NULL;
    const char *pszOut = NULL;
    RTLDRARCH   enmLdrArch   = RTLDRARCH_WHATEVER;
    unsigned    cVerbosity   = 0;
    bool        fForce       = false;

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'e':   pszExe = ValueUnion.psz; break;
            case 'o':   pszOut = ValueUnion.psz; break;
            case 'f':   fForce = true; break;
            case 'V':   return HandleVersion(cArgs, papszArgs);
            case 'h':   return HelpExtractExeSignerCert(g_pStdOut, RTSIGNTOOLHELP_FULL);

            case VINF_GETOPT_NOT_OPTION:
                if (!pszExe)
                    pszExe = ValueUnion.psz;
                else if (!pszOut)
                    pszOut = ValueUnion.psz;
                else
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Too many file arguments: %s", ValueUnion.psz);
                break;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (!pszExe)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No executable given.");
    if (!pszOut)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No output file given.");
    if (!fForce && RTPathExists(pszOut))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "The output file '%s' exists.", pszOut);

    /*
     * Do it.
     */
    /* Read & decode the PKCS#7 signature. */
    SIGNTOOLPKCS7EXE This;
    RTEXITCODE rcExit = SignToolPkcs7Exe_InitFromFile(&This, pszExe, cVerbosity, enmLdrArch);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /*
         * Write out the PKCS#7 signature.
         */
        RTFILE hFile;
        rc = RTFileOpen(&hFile, pszOut,
                        RTFILE_O_WRITE | RTFILE_O_DENY_WRITE | (fForce ? RTFILE_O_CREATE_REPLACE : RTFILE_O_CREATE));
        if (RT_SUCCESS(rc))
        {
            rc = RTFileWrite(hFile, This.pbBuf, This.cbBuf, NULL);
            if (RT_SUCCESS(rc))
            {
                rc = RTFileClose(hFile);
                if (RT_SUCCESS(rc))
                {
                    hFile  = NIL_RTFILE;
                    RTMsgInfo("Successfully wrote %u bytes to '%s'", This.cbBuf, pszOut);
                    rcExit = RTEXITCODE_SUCCESS;
                }
                else
                    RTMsgError("RTFileClose failed: %Rrc", rc);
            }
            else
                RTMsgError("RTFileWrite failed: %Rrc", rc);
            RTFileClose(hFile);
        }
        else
            RTMsgError("Error opening '%s' for writing: %Rrc", pszOut, rc);

        /* Delete the signature data. */
        SignToolPkcs7Exe_Delete(&This);
    }
    return rcExit;
}


/*********************************************************************************************************************************
*   The 'add-nested-exe-signature' command.                                                                                      *
*********************************************************************************************************************************/

static RTEXITCODE HelpAddNestedExeSignature(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT,
                        "add-nested-exe-signature [-v|--verbose] [-d|--debug] [-p|--prepend] <destination-exe> <source-exe>\n");
    if (enmLevel == RTSIGNTOOLHELP_FULL)
        RTStrmWrappedPrintf(pStrm, 0,
                            "\n"
                            "The --debug option allows the source-exe to be omitted in order to test the "
                            "encoding and PE file modification.\n"
                            "\n"
                            "The --prepend option puts the nested signature first rather than appending it "
                            "to the end of of the nested signature set.  Windows reads nested signatures in "
                            "reverse order, so --prepend will logically putting it last.\n");
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE HandleAddNestedExeSignature(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--prepend", 'p', RTGETOPT_REQ_NOTHING },
        { "--verbose", 'v', RTGETOPT_REQ_NOTHING },
        { "--debug",   'd', RTGETOPT_REQ_NOTHING },
    };

    const char *pszDst     = NULL;
    const char *pszSrc     = NULL;
    unsigned    cVerbosity = 0;
    bool        fDebug     = false;
    bool        fPrepend   = false;

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'v':   cVerbosity++; break;
            case 'd':   fDebug = pszSrc == NULL; break;
            case 'p':   fPrepend = true; break;
            case 'V':   return HandleVersion(cArgs, papszArgs);
            case 'h':   return HelpAddNestedExeSignature(g_pStdOut, RTSIGNTOOLHELP_FULL);

            case VINF_GETOPT_NOT_OPTION:
                if (!pszDst)
                    pszDst = ValueUnion.psz;
                else if (!pszSrc)
                {
                    pszSrc = ValueUnion.psz;
                    fDebug = false;
                }
                else
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Too many file arguments: %s", ValueUnion.psz);
                break;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (!pszDst)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No destination executable given.");
    if (!pszSrc && !fDebug)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No source executable file given.");

    /*
     * Do it.
     */
    /* Read & decode the source PKCS#7 signature. */
    SIGNTOOLPKCS7EXE Src;
    RTEXITCODE rcExit = pszSrc ? SignToolPkcs7Exe_InitFromFile(&Src, pszSrc, cVerbosity) : RTEXITCODE_SUCCESS;
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /* Ditto for the destination PKCS#7 signature. */
        SIGNTOOLPKCS7EXE Dst;
        rcExit = SignToolPkcs7Exe_InitFromFile(&Dst, pszDst, cVerbosity);
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            /* Do the signature manipulation. */
            if (pszSrc)
                rcExit = SignToolPkcs7_AddNestedSignature(&Dst, &Src, cVerbosity, fPrepend);
            if (rcExit == RTEXITCODE_SUCCESS)
                rcExit = SignToolPkcs7_Encode(&Dst, cVerbosity);

            /* Update the destination executable file. */
            if (rcExit == RTEXITCODE_SUCCESS)
                rcExit = SignToolPkcs7Exe_WriteSignatureToFile(&Dst, cVerbosity);

            SignToolPkcs7Exe_Delete(&Dst);
        }
        if (pszSrc)
            SignToolPkcs7Exe_Delete(&Src);
    }

    return rcExit;
}


/*********************************************************************************************************************************
*   The 'add-nested-cat-signature' command.                                                                                      *
*********************************************************************************************************************************/

static RTEXITCODE HelpAddNestedCatSignature(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT,
                        "add-nested-cat-signature [-v|--verbose] [-d|--debug] [-p|--prepend] <destination-cat> <source-cat>\n");
    if (enmLevel == RTSIGNTOOLHELP_FULL)
        RTStrmWrappedPrintf(pStrm, 0,
                            "\n"
                            "The --debug option allows the source-cat to be omitted in order to test the "
                            "ASN.1 re-encoding of the destination catalog file.\n"
                            "\n"
                            "The --prepend option puts the nested signature first rather than appending it "
                            "to the end of of the nested signature set.  Windows reads nested signatures in "
                            "reverse order, so --prepend will logically putting it last.\n");
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE HandleAddNestedCatSignature(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--prepend", 'p', RTGETOPT_REQ_NOTHING },
        { "--verbose", 'v', RTGETOPT_REQ_NOTHING },
        { "--debug",   'd', RTGETOPT_REQ_NOTHING },
    };

    const char *pszDst     = NULL;
    const char *pszSrc     = NULL;
    unsigned    cVerbosity = 0;
    bool        fDebug     = false;
    bool        fPrepend   = false;

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'v':   cVerbosity++; break;
            case 'd':   fDebug = pszSrc == NULL; break;
            case 'p':   fPrepend = true;  break;
            case 'V':   return HandleVersion(cArgs, papszArgs);
            case 'h':   return HelpAddNestedCatSignature(g_pStdOut, RTSIGNTOOLHELP_FULL);

            case VINF_GETOPT_NOT_OPTION:
                if (!pszDst)
                    pszDst = ValueUnion.psz;
                else if (!pszSrc)
                {
                    pszSrc = ValueUnion.psz;
                    fDebug = false;
                }
                else
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Too many file arguments: %s", ValueUnion.psz);
                break;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (!pszDst)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No destination catalog file given.");
    if (!pszSrc && !fDebug)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No source catalog file given.");

    /*
     * Do it.
     */
    /* Read & decode the source PKCS#7 signature. */
    SIGNTOOLPKCS7 Src;
    RTEXITCODE rcExit = pszSrc ? SignToolPkcs7_InitFromFile(&Src, pszSrc, cVerbosity) : RTEXITCODE_SUCCESS;
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /* Ditto for the destination PKCS#7 signature. */
        SIGNTOOLPKCS7EXE Dst;
        rcExit = SignToolPkcs7_InitFromFile(&Dst, pszDst, cVerbosity);
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            /* Do the signature manipulation. */
            if (pszSrc)
                rcExit = SignToolPkcs7_AddNestedSignature(&Dst, &Src, cVerbosity, fPrepend);
            if (rcExit == RTEXITCODE_SUCCESS)
                rcExit = SignToolPkcs7_Encode(&Dst, cVerbosity);

            /* Update the destination executable file. */
            if (rcExit == RTEXITCODE_SUCCESS)
                rcExit = SignToolPkcs7_WriteSignatureToFile(&Dst, pszDst, cVerbosity);

            SignToolPkcs7_Delete(&Dst);
        }
        if (pszSrc)
            SignToolPkcs7_Delete(&Src);
    }

    return rcExit;
}


/*********************************************************************************************************************************
*   The 'add-timestamp-exe-signature' command.                                                                                   *
*********************************************************************************************************************************/
#ifndef IPRT_SIGNTOOL_NO_SIGNING

static RTEXITCODE HelpAddTimestampExeSignature(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);

    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT,
                        "add-timestamp-exe-signature [-v|--verbose] [--signature-index|-i <num>] "
                        OPT_CERT_KEY_SYNOPSIS("--timestamp-", "")
                        "[--timestamp-type old|new] "
                        "[--timestamp-override <partial-isots>] "
                        "[--replace-existing|-r] "
                        "<exe>\n");
    if (enmLevel == RTSIGNTOOLHELP_FULL)
        RTStrmWrappedPrintf(pStrm, 0,
                            "This is mainly to test timestamp code.\n"
                            "\n"
                            "The --timestamp-override option can take a partial or full ISO timestamp.  It is merged "
                            "with the current time if partial.\n"
                            "\n");
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE HandleAddTimestampExeSignature(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--signature-index",      'i',                        RTGETOPT_REQ_UINT32 },
        OPT_CERT_KEY_GETOPTDEF_ENTRIES("--timestamp-", "", 1000),
        { "--timestamp-type",       OPT_TIMESTAMP_TYPE,         RTGETOPT_REQ_STRING },
        { "--timestamp-override",   OPT_TIMESTAMP_OVERRIDE,     RTGETOPT_REQ_STRING },
        { "--replace-existing",     'r',                        RTGETOPT_REQ_NOTHING },
        { "--verbose",              'v',                        RTGETOPT_REQ_NOTHING },
    };

    unsigned                cVerbosity              = 0;
    unsigned                iSignature              = 0;
    bool                    fReplaceExisting        = false;
    SignToolTimestampOpts   TimestampOpts("timestamp");
    RTTIMESPEC              SigningTime;
    RTTimeNow(&SigningTime);

    RTGETOPTSTATE   GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);

    RTEXITCODE      rcExit = RTEXITCODE_SUCCESS;
    RTGETOPTUNION   ValueUnion;
    int             ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        RTEXITCODE rcExit2 = RTEXITCODE_SUCCESS;
        switch (ch)
        {
            OPT_CERT_KEY_SWITCH_CASES(TimestampOpts, 1000, ch, ValueUnion, rcExit2);
            case 'i':                       iSignature = ValueUnion.u32; break;
            case OPT_TIMESTAMP_TYPE:        rcExit2 = HandleOptTimestampType(&TimestampOpts, ValueUnion.psz); break;
            case OPT_TIMESTAMP_OVERRIDE:    rcExit2 = HandleOptTimestampOverride(&SigningTime, ValueUnion.psz); break;
            case 'r':                       fReplaceExisting = true; break;
            case 'v':                       cVerbosity++; break;
            case 'V':                       return HandleVersion(cArgs, papszArgs);
            case 'h':                       return HelpAddTimestampExeSignature(g_pStdOut, RTSIGNTOOLHELP_FULL);

            case VINF_GETOPT_NOT_OPTION:
                /* Do final certificate and key option processing (first file only). */
                rcExit2 = TimestampOpts.finalizeOptions(cVerbosity);
                if (rcExit2 == RTEXITCODE_SUCCESS)
                {
                    /* Do the work: */
                    SIGNTOOLPKCS7EXE Exe;
                    rcExit2 = SignToolPkcs7Exe_InitFromFile(&Exe, ValueUnion.psz, cVerbosity);
                    if (rcExit2 == RTEXITCODE_SUCCESS)
                    {
                        rcExit2 = SignToolPkcs7_AddTimestampSignature(&Exe, cVerbosity, iSignature, fReplaceExisting,
                                                                      SigningTime, &TimestampOpts);
                        if (rcExit2 == RTEXITCODE_SUCCESS)
                            rcExit2 = SignToolPkcs7_Encode(&Exe, cVerbosity);
                        if (rcExit2 == RTEXITCODE_SUCCESS)
                            rcExit2 = SignToolPkcs7Exe_WriteSignatureToFile(&Exe, cVerbosity);
                        SignToolPkcs7Exe_Delete(&Exe);
                    }
                    if (rcExit2 != RTEXITCODE_SUCCESS && rcExit == RTEXITCODE_SUCCESS)
                        rcExit = rcExit2;
                    rcExit2 = RTEXITCODE_SUCCESS;
                }
                break;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }

        if (rcExit2 != RTEXITCODE_SUCCESS)
        {
            rcExit = rcExit2;
            break;
        }
    }
    return rcExit;
}

#endif /*!IPRT_SIGNTOOL_NO_SIGNING */


/*********************************************************************************************************************************
*   The 'sign-exe' command.                                                                                   *
*********************************************************************************************************************************/
#ifndef IPRT_SIGNTOOL_NO_SIGNING

static RTEXITCODE HelpSign(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);

    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT,
                        "sign [-v|--verbose] "
                        "[--file-type exe|cat] "
                        "[--type|/fd sha1|sha256] "
                        "[--hash-pages|/ph] "
                        "[--no-hash-pages|/nph] "
                        "[--append/as] "
                        "[--no-signing-time] "
                        "[--add-cert <file>] "
                        "[--timestamp-type old|new] "
                        "[--timestamp-override <partial-isots>] "
                        "[--verbose|/debug|-v] "
                        OPT_CERT_KEY_SYNOPSIS("--", "")
                        OPT_CERT_KEY_SYNOPSIS("--timestamp-", "")
                        //OPT_CERT_KEY_SYNOPSIS("--timestamp-", "-2") - doesn't work, windows only uses one. Check again with new-style signatures
                        "<exe>\n");
    if (enmLevel == RTSIGNTOOLHELP_FULL)
        RTStrmWrappedPrintf(pStrm, 0,
                            "\n"
                            "Create a new code signature for an executable or catalog.\n"
                            "\n"
                            "Options:\n"
                            "  --append, /as\n"
                            "    Append the signature if one already exists.  The default is to replace any existing signature.\n"
                            "  --type sha1|sha256, /fd sha1|sha256\n"
                            "    Signature type, SHA-1 or SHA-256.\n"
                            "  --hash-pages, /ph, --no-page-hashes, /nph\n"
                            "    Enables or disables page hashing.  Ignored for catalog files.  Default: --no-page-hashes\n"
                            "  --add-cert <file>, /ac <file>\n"
                            "    Adds (first) certificate from the file to the signature.  Both PEM and DER (binary) encodings "
                            "are accepted.  Repeat to add more certiifcates.\n"
                            "  --timestamp-override <partial-iso-timestamp>\n"
                            "    This specifies the signing time as a ISO timestamp.  Partial timestamps are merged with the "
                            "current time. This is applied to any timestamp signature as well as the signingTime attribute of "
                            "main signature. Higher resolution than seconds is not supported.  Default: Current time.\n"
                            "  --no-signing-time\n"
                            "    Don't set the signing time on the main signature, only on the timestamp one.  Unfortunately, "
                            "this doesn't work without modifying OpenSSL a little.\n"
                            "  --timestamp-type old|new\n"
                            "    Selects the timstamp type. 'old' is the old style /t <url> stuff from signtool.exe. "
                            "'new' means a RTC-3161 timstamp - currently not implemented. Default: old\n"
                            //"  --timestamp-type-2 old|new\n"
                            //"    Same as --timestamp-type but for the 2nd timstamp signature.\n"
                            "\n"
                            //"Certificate and Key Options (--timestamp-cert-name[-2] etc for timestamps):\n"
                            "Certificate and Key Options (--timestamp-cert-name etc for timestamps):\n"
                            "  --cert-subject <partial name>, /n <partial name>\n"
                            "    Locate the main signature signing certificate and key, unless anything else is given, "
                            "by the given name substring.  Overrides any previous --cert-sha1 and --cert-file options.\n"
                            "  --cert-sha1 <hex bytes>, /sha1 <hex bytes>\n"
                            "    Locate the main signature signing certificate and key, unless anything else is given, "
                            "by the given thumbprint.  The hex bytes can be space separated, colon separated, just "
                            "bunched together, or a mix of these.  This overrids any previous --cert-name and --cert-file "
                            "options.\n"
                            "  --cert-store <name>, /s <store>\n"
                            "    Certificate store to search when using --cert-name or --cert-sha1. Default: MY\n"
                            "  --cert-machine-store, /sm\n"
                            "    Use the machine store rather the ones of the current user.\n"
                            "  --cert-file <file>, /f <file>\n"
                            "    Load the certificate and key, unless anything else is given, from given file.  Both PEM and "
                            "DER (binary) encodings are supported.  Keys file can be RSA or PKCS#12 formatted.\n"
                            "  --key-file <file>\n"
                            "    Load the private key from the given file.  Support RSA and PKCS#12 formatted files.\n"
                            "  --key-password <password>, /p <password>\n"
                            "    Password to use to decrypt a PKCS#12 password file.\n"
                            "  --key-password-file <file>|stdin\n"
                            "    Load password  to decrypt the password file from the given file or from stdin.\n"
                            "  --key-name <name>, /kc <name>\n"
                            "    The private key container name.  Not implemented.\n"
                            "  --key-provider <name>, /csp <name>\n"
                            "    The name of the crypto provider where the private key conatiner specified via --key-name "
                            "can be found.\n"
                            );

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE HandleSign(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--append",               'A',                        RTGETOPT_REQ_NOTHING },
        { "/as",                    'A',                        RTGETOPT_REQ_NOTHING },
        { "/a",                     OPT_IGNORED,                RTGETOPT_REQ_NOTHING }, /* select best cert automatically */
        { "--type",                 't',                        RTGETOPT_REQ_STRING },
        { "/fd",                    't',                        RTGETOPT_REQ_STRING },
        { "--hash-pages",           OPT_HASH_PAGES,             RTGETOPT_REQ_NOTHING },
        { "/ph",                    OPT_HASH_PAGES,             RTGETOPT_REQ_NOTHING },
        { "--no-hash-pages",        OPT_NO_HASH_PAGES,          RTGETOPT_REQ_NOTHING },
        { "/nph",                   OPT_NO_HASH_PAGES,          RTGETOPT_REQ_NOTHING },
        { "--add-cert",             OPT_ADD_CERT,               RTGETOPT_REQ_STRING },
        { "/ac",                    OPT_ADD_CERT,               RTGETOPT_REQ_STRING },
        { "--description",          'd',                        RTGETOPT_REQ_STRING },
        { "--desc",                 'd',                        RTGETOPT_REQ_STRING },
        { "/d",                     'd',                        RTGETOPT_REQ_STRING },
        { "--description-url",      'D',                        RTGETOPT_REQ_STRING },
        { "--desc-url",             'D',                        RTGETOPT_REQ_STRING },
        { "/du",                    'D',                        RTGETOPT_REQ_STRING },
        { "--no-signing-time",      OPT_NO_SIGNING_TIME,        RTGETOPT_REQ_NOTHING },
        OPT_CERT_KEY_GETOPTDEF_ENTRIES("--", "",             1000),
        OPT_CERT_KEY_GETOPTDEF_COMPAT_ENTRIES(               1000),
        OPT_CERT_KEY_GETOPTDEF_ENTRIES("--timestamp-", "",   1020),
        //OPT_CERT_KEY_GETOPTDEF_ENTRIES("--timestamp-", "-1", 1020),
        //OPT_CERT_KEY_GETOPTDEF_ENTRIES("--timestamp-", "-2", 1040), - disabled as windows cannot make use of it. Try again when
        // new-style timestamp signatures has been implemented. Otherwise, just add two primary signatures with the two
        // different timestamps certificates / hashes / whatever.
        { "--timestamp-type",       OPT_TIMESTAMP_TYPE,         RTGETOPT_REQ_STRING },
        { "--timestamp-type-1",     OPT_TIMESTAMP_TYPE,         RTGETOPT_REQ_STRING },
        { "--timestamp-type-2",     OPT_TIMESTAMP_TYPE_2,       RTGETOPT_REQ_STRING },
        { "--timestamp-override",   OPT_TIMESTAMP_OVERRIDE,     RTGETOPT_REQ_STRING },
        { "--file-type",            OPT_FILE_TYPE,              RTGETOPT_REQ_STRING },
        { "--verbose",              'v',                        RTGETOPT_REQ_NOTHING },
        { "/v",                     'v',                        RTGETOPT_REQ_NOTHING },
        { "/debug",                 'v',                        RTGETOPT_REQ_NOTHING },
    };

    unsigned                cVerbosity              = 0;
    RTDIGESTTYPE            enmSigType              = RTDIGESTTYPE_SHA1;
    bool                    fReplaceExisting        = true;
    bool                    fHashPages              = false;
    bool                    fNoSigningTime          = false;
    RTSIGNTOOLFILETYPE      enmForceFileType        = RTSIGNTOOLFILETYPE_DETECT;
    SignToolKeyPair         SigningCertKey("signing", true);
    CryptoStore             AddCerts;
    const char             *pszDescription          = NULL; /** @todo implement putting descriptions into the OpusInfo stuff. */
    const char             *pszDescriptionUrl       = NULL;
    SignToolTimestampOpts   aTimestampOpts[2] = { SignToolTimestampOpts("timestamp"), SignToolTimestampOpts("timestamp#2") };
    RTTIMESPEC              SigningTime;
    RTTimeNow(&SigningTime);

    RTGETOPTSTATE   GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);

    RTEXITCODE      rcExit = RTEXITCODE_SUCCESS;
    RTGETOPTUNION   ValueUnion;
    int             ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        RTEXITCODE rcExit2 = RTEXITCODE_SUCCESS;
        switch (ch)
        {
            OPT_CERT_KEY_SWITCH_CASES(SigningCertKey,    1000, ch, ValueUnion, rcExit2);
            OPT_CERT_KEY_SWITCH_CASES(aTimestampOpts[0], 1020, ch, ValueUnion, rcExit2);
            OPT_CERT_KEY_SWITCH_CASES(aTimestampOpts[1], 1040, ch, ValueUnion, rcExit2);
            case 't':                       rcExit2 = HandleOptSignatureType(&enmSigType, ValueUnion.psz); break;
            case 'A':                       fReplaceExisting = false; break;
            case 'd':                       pszDescription = ValueUnion.psz; break;
            case 'D':                       pszDescriptionUrl = ValueUnion.psz; break;
            case OPT_HASH_PAGES:            fHashPages = true; break;
            case OPT_NO_HASH_PAGES:         fHashPages = false; break;
            case OPT_NO_SIGNING_TIME:       fNoSigningTime = true; break;
            case OPT_ADD_CERT:              rcExit2 = HandleOptAddCert(&AddCerts.m_hStore, ValueUnion.psz); break;
            case OPT_TIMESTAMP_TYPE:        rcExit2 = HandleOptTimestampType(&aTimestampOpts[0], ValueUnion.psz); break;
            case OPT_TIMESTAMP_TYPE_2:      rcExit2 = HandleOptTimestampType(&aTimestampOpts[1], ValueUnion.psz); break;
            case OPT_TIMESTAMP_OVERRIDE:    rcExit2 = HandleOptTimestampOverride(&SigningTime, ValueUnion.psz); break;
            case OPT_FILE_TYPE:             rcExit2 = HandleOptFileType(&enmForceFileType, ValueUnion.psz); break;
            case OPT_IGNORED:               break;
            case 'v':                       cVerbosity++; break;
            case 'V':                       return HandleVersion(cArgs, papszArgs);
            case 'h':                       return HelpSign(g_pStdOut, RTSIGNTOOLHELP_FULL);

            case VINF_GETOPT_NOT_OPTION:
                /*
                 * Do final certificate and key option processing (first file only).
                 */
                rcExit2 = SigningCertKey.finalizeOptions(cVerbosity);
                for (unsigned i = 0; rcExit2 == RTEXITCODE_SUCCESS && i < RT_ELEMENTS(aTimestampOpts); i++)
                    rcExit2 = aTimestampOpts[i].finalizeOptions(cVerbosity);
                if (rcExit2 == RTEXITCODE_SUCCESS)
                {
                    /*
                     * Detect file type.
                     */
                    RTSIGNTOOLFILETYPE enmFileType = DetectFileType(enmForceFileType, ValueUnion.psz);
                    if (enmFileType == RTSIGNTOOLFILETYPE_EXE)
                    {
                        /*
                         * Sign executable image.
                         */
                        SIGNTOOLPKCS7EXE Exe;
                        rcExit2 = SignToolPkcs7Exe_InitFromFile(&Exe, ValueUnion.psz, cVerbosity,
                                                                RTLDRARCH_WHATEVER, true /*fAllowUnsigned*/);
                        if (rcExit2 == RTEXITCODE_SUCCESS)
                        {
                            rcExit2 = SignToolPkcs7_AddOrReplaceSignature(&Exe, cVerbosity, enmSigType, fReplaceExisting,
                                                                          fHashPages, fNoSigningTime, &SigningCertKey,
                                                                          AddCerts.m_hStore, SigningTime,
                                                                          RT_ELEMENTS(aTimestampOpts), aTimestampOpts);
                            if (rcExit2 == RTEXITCODE_SUCCESS)
                                rcExit2 = SignToolPkcs7_Encode(&Exe, cVerbosity);
                            if (rcExit2 == RTEXITCODE_SUCCESS)
                                rcExit2 = SignToolPkcs7Exe_WriteSignatureToFile(&Exe, cVerbosity);
                            SignToolPkcs7Exe_Delete(&Exe);
                        }
                    }
                    else if (enmFileType == RTSIGNTOOLFILETYPE_CAT)
                    {
                        /*
                         * Sign catalog file.
                         */
                        SIGNTOOLPKCS7 Cat;
                        rcExit2 = SignToolPkcs7_InitFromFile(&Cat, ValueUnion.psz, cVerbosity);
                        if (rcExit2 == RTEXITCODE_SUCCESS)
                        {
                            rcExit2 = SignToolPkcs7_AddOrReplaceCatSignature(&Cat, cVerbosity, enmSigType, fReplaceExisting,
                                                                             fNoSigningTime, &SigningCertKey,
                                                                             AddCerts.m_hStore, SigningTime,
                                                                             RT_ELEMENTS(aTimestampOpts), aTimestampOpts);
                            if (rcExit2 == RTEXITCODE_SUCCESS)
                                rcExit2 = SignToolPkcs7_Encode(&Cat, cVerbosity);
                            if (rcExit2 == RTEXITCODE_SUCCESS)
                                rcExit2 = SignToolPkcs7_WriteSignatureToFile(&Cat, ValueUnion.psz, cVerbosity);
                            SignToolPkcs7_Delete(&Cat);
                        }
                    }
                    else
                        rcExit2 = RTEXITCODE_FAILURE;
                    if (rcExit2 != RTEXITCODE_SUCCESS && rcExit == RTEXITCODE_SUCCESS)
                        rcExit = rcExit2;
                    rcExit2 = RTEXITCODE_SUCCESS;
                }
                break;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
        if (rcExit2 != RTEXITCODE_SUCCESS)
        {
            rcExit = rcExit2;
            break;
        }
    }

    return rcExit;
}

#endif /*!IPRT_SIGNTOOL_NO_SIGNING */


/*********************************************************************************************************************************
*   The 'verify-exe' command.                                                                                                    *
*********************************************************************************************************************************/
#ifndef IPRT_IN_BUILD_TOOL

static RTEXITCODE HelpVerifyExe(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT,
                        "verify-exe [--verbose|--quiet] [--kernel] [--root <root-cert.der>] [--self-signed-roots-from-system] "
                        "[--additional <supp-cert.der>] [--type <win|osx>] <exe1> [exe2 [..]]\n");
    return RTEXITCODE_SUCCESS;
}

typedef struct VERIFYEXESTATE
{
    CryptoStore RootStore;
    CryptoStore KernelRootStore;
    CryptoStore AdditionalStore;
    bool        fKernel;
    int         cVerbose;
    enum { kSignType_Windows, kSignType_OSX } enmSignType;
    RTLDRARCH   enmLdrArch;
    uint32_t    cBad;
    uint32_t    cOkay;
    const char *pszFilename;
    RTTIMESPEC  ValidationTime;

    VERIFYEXESTATE()
        : fKernel(false)
        , cVerbose(0)
        , enmSignType(kSignType_Windows)
        , enmLdrArch(RTLDRARCH_WHATEVER)
        , cBad(0)
        , cOkay(0)
        , pszFilename(NULL)
    {
        RTTimeSpecSetSeconds(&ValidationTime, 0);
    }
} VERIFYEXESTATE;

# ifdef VBOX
/** Certificate store load set.
 * Declared outside HandleVerifyExe because of braindead gcc visibility crap. */
struct STSTORESET
{
    RTCRSTORE       hStore;
    PCSUPTAENTRY    paTAs;
    unsigned        cTAs;
};
# endif

/**
 * @callback_method_impl{FNRTCRPKCS7VERIFYCERTCALLBACK,
 * Standard code signing.  Use this for Microsoft SPC.}
 */
static DECLCALLBACK(int) VerifyExecCertVerifyCallback(PCRTCRX509CERTIFICATE pCert, RTCRX509CERTPATHS hCertPaths, uint32_t fFlags,
                                                      void *pvUser, PRTERRINFO pErrInfo)
{
    VERIFYEXESTATE *pState = (VERIFYEXESTATE *)pvUser;
    uint32_t        cPaths = RTCrX509CertPathsGetPathCount(hCertPaths);

    /*
     * Dump all the paths.
     */
    if (pState->cVerbose > 0)
    {
        RTPrintf(fFlags & RTCRPKCS7VCC_F_TIMESTAMP ? "Timestamp Path%s:\n" : "Signature Path%s:\n",
                 cPaths == 1 ? "" : "s");
        for (uint32_t iPath = 0; iPath < cPaths; iPath++)
        {
            //if (iPath != 0)
            //    RTPrintf("---\n");
            RTCrX509CertPathsDumpOne(hCertPaths, iPath, pState->cVerbose, RTStrmDumpPrintfV, g_pStdOut);
            *pErrInfo->pszMsg = '\0';
        }
        //RTPrintf(fFlags & RTCRPKCS7VCC_F_TIMESTAMP ? "--- end timestamp ---\n" : "--- end signature ---\n");
    }

    /*
     * Test signing certificates normally doesn't have all the necessary
     * features required below.  So, treat them as special cases.
     */
    if (   hCertPaths == NIL_RTCRX509CERTPATHS
        && RTCrX509Name_Compare(&pCert->TbsCertificate.Issuer, &pCert->TbsCertificate.Subject) == 0)
    {
        RTMsgInfo("Test signed.\n");
        return VINF_SUCCESS;
    }

    if (hCertPaths == NIL_RTCRX509CERTPATHS)
        RTMsgInfo("Signed by trusted certificate.\n");

    /*
     * Standard code signing capabilites required.
     */
    int rc = RTCrPkcs7VerifyCertCallbackCodeSigning(pCert, hCertPaths, fFlags, NULL, pErrInfo);
    if (   RT_SUCCESS(rc)
        && (fFlags & RTCRPKCS7VCC_F_SIGNED_DATA))
    {
        /*
         * If windows kernel signing, a valid certificate path must be anchored
         * by the microsoft kernel signing root certificate.  The only
         * alternative is test signing.
         */
        if (   pState->fKernel
            && hCertPaths != NIL_RTCRX509CERTPATHS
            && pState->enmSignType == VERIFYEXESTATE::kSignType_Windows)
        {
            uint32_t cFound = 0;
            uint32_t cValid = 0;
            for (uint32_t iPath = 0; iPath < cPaths; iPath++)
            {
                bool                            fTrusted;
                PCRTCRX509NAME                  pSubject;
                PCRTCRX509SUBJECTPUBLICKEYINFO  pPublicKeyInfo;
                int                             rcVerify;
                rc = RTCrX509CertPathsQueryPathInfo(hCertPaths, iPath, &fTrusted, NULL /*pcNodes*/, &pSubject, &pPublicKeyInfo,
                                                    NULL, NULL /*pCertCtx*/, &rcVerify);
                AssertRCBreak(rc);

                if (RT_SUCCESS(rcVerify))
                {
                    Assert(fTrusted);
                    cValid++;

                    /* Search the kernel signing root store for a matching anchor. */
                    RTCRSTORECERTSEARCH Search;
                    rc = RTCrStoreCertFindBySubjectOrAltSubjectByRfc5280(pState->KernelRootStore.m_hStore, pSubject, &Search);
                    AssertRCBreak(rc);
                    PCRTCRCERTCTX pCertCtx;
                    while ((pCertCtx = RTCrStoreCertSearchNext(pState->KernelRootStore.m_hStore, &Search)) != NULL)
                    {
                        PCRTCRX509SUBJECTPUBLICKEYINFO pPubKeyInfo;
                        if (pCertCtx->pCert)
                            pPubKeyInfo = &pCertCtx->pCert->TbsCertificate.SubjectPublicKeyInfo;
                        else if (pCertCtx->pTaInfo)
                            pPubKeyInfo = &pCertCtx->pTaInfo->PubKey;
                        else
                            pPubKeyInfo = NULL;
                        if (RTCrX509SubjectPublicKeyInfo_Compare(pPubKeyInfo, pPublicKeyInfo) == 0)
                            cFound++;
                        RTCrCertCtxRelease(pCertCtx);
                    }

                    int rc2 = RTCrStoreCertSearchDestroy(pState->KernelRootStore.m_hStore, &Search); AssertRC(rc2);
                }
            }
            if (RT_SUCCESS(rc) && cFound == 0)
                rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE, "Not valid kernel code signature.");
            if (RT_SUCCESS(rc) && cValid != 2)
                RTMsgWarning("%u valid paths, expected 2", cValid);
        }
        /*
         * For Mac OS X signing, check for special developer ID attributes.
         */
        else if (pState->enmSignType == VERIFYEXESTATE::kSignType_OSX)
        {
            uint32_t cDevIdApp    = 0;
            uint32_t cDevIdKext   = 0;
            uint32_t cDevIdMacDev = 0;
            for (uint32_t i = 0; i < pCert->TbsCertificate.T3.Extensions.cItems; i++)
            {
                PCRTCRX509EXTENSION pExt = pCert->TbsCertificate.T3.Extensions.papItems[i];
                if (RTAsn1ObjId_CompareWithString(&pExt->ExtnId, RTCR_APPLE_CS_DEVID_APPLICATION_OID) == 0)
                {
                    cDevIdApp++;
                    if (!pExt->Critical.fValue)
                        rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                                           "Dev ID Application certificate extension is not flagged critical");
                }
                else if (RTAsn1ObjId_CompareWithString(&pExt->ExtnId, RTCR_APPLE_CS_DEVID_KEXT_OID) == 0)
                {
                    cDevIdKext++;
                    if (!pExt->Critical.fValue)
                        rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                                           "Dev ID kext certificate extension is not flagged critical");
                }
                else if (RTAsn1ObjId_CompareWithString(&pExt->ExtnId, RTCR_APPLE_CS_DEVID_MAC_SW_DEV_OID) == 0)
                {
                    cDevIdMacDev++;
                    if (!pExt->Critical.fValue)
                        rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                                           "Dev ID Mac SW dev certificate extension is not flagged critical");
                }
            }
            if (cDevIdApp == 0)
            {
                if (cDevIdMacDev == 0)
                    rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                                       "Certificate is missing the 'Dev ID Application' extension");
                else
                    RTMsgWarning("Mac SW dev certificate used to sign code.");
            }
            if (cDevIdKext == 0 && pState->fKernel)
            {
                if (cDevIdMacDev == 0)
                    rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                                       "Certificate is missing the 'Dev ID kext' extension");
                else
                    RTMsgWarning("Mac SW dev certificate used to sign kernel code.");
            }
        }
    }

    return rc;
}

/** @callback_method_impl{FNRTLDRVALIDATESIGNEDDATA}  */
static DECLCALLBACK(int) VerifyExeCallback(RTLDRMOD hLdrMod, PCRTLDRSIGNATUREINFO pInfo, PRTERRINFO pErrInfo, void *pvUser)
{
    VERIFYEXESTATE *pState = (VERIFYEXESTATE *)pvUser;
    RT_NOREF_PV(hLdrMod);

    switch (pInfo->enmType)
    {
        case RTLDRSIGNATURETYPE_PKCS7_SIGNED_DATA:
        {
            PCRTCRPKCS7CONTENTINFO pContentInfo = (PCRTCRPKCS7CONTENTINFO)pInfo->pvSignature;

            if (pState->cVerbose > 0)
                RTMsgInfo("Verifying '%s' signature #%u ...\n", pState->pszFilename, pInfo->iSignature + 1);

            /*
             * Dump the signed data if so requested and it's the first one, assuming that
             * additional signatures in contained wihtin the same ContentInfo structure.
             */
            if (pState->cVerbose > 1 && pInfo->iSignature == 0)
                RTAsn1Dump(&pContentInfo->SeqCore.Asn1Core, 0, 0, RTStrmDumpPrintfV, g_pStdOut);

            /*
             * We'll try different alternative timestamps here.
             */
            struct { RTTIMESPEC TimeSpec; const char *pszDesc; } aTimes[3];
            unsigned cTimes = 0;

            /* The specified timestamp. */
            if (RTTimeSpecGetSeconds(&pState->ValidationTime) != 0)
            {
                aTimes[cTimes].TimeSpec = pState->ValidationTime;
                aTimes[cTimes].pszDesc  = "validation time";
                cTimes++;
            }

            /* Linking timestamp: */
            uint64_t uLinkingTime = 0;
            int rc = RTLdrQueryProp(hLdrMod, RTLDRPROP_TIMESTAMP_SECONDS, &uLinkingTime, sizeof(uLinkingTime));
            if (RT_SUCCESS(rc))
            {
                RTTimeSpecSetSeconds(&aTimes[cTimes].TimeSpec, uLinkingTime);
                aTimes[cTimes].pszDesc = "at link time";
                cTimes++;
            }
            else if (rc != VERR_NOT_FOUND)
                RTMsgError("RTLdrQueryProp/RTLDRPROP_TIMESTAMP_SECONDS failed on '%s': %Rrc\n", pState->pszFilename, rc);

            /* Now: */
            RTTimeNow(&aTimes[cTimes].TimeSpec);
            aTimes[cTimes].pszDesc = "now";
            cTimes++;

            /*
             * Do the actual verification.
             */
            for (unsigned iTime = 0; iTime < cTimes; iTime++)
            {
                if (pInfo->pvExternalData)
                    rc = RTCrPkcs7VerifySignedDataWithExternalData(pContentInfo,
                                                                   RTCRPKCS7VERIFY_SD_F_COUNTER_SIGNATURE_SIGNING_TIME_ONLY
                                                                   | RTCRPKCS7VERIFY_SD_F_ALWAYS_USE_SIGNING_TIME_IF_PRESENT
                                                                   | RTCRPKCS7VERIFY_SD_F_ALWAYS_USE_MS_TIMESTAMP_IF_PRESENT
                                                                   | RTCRPKCS7VERIFY_SD_F_CHECK_TRUST_ANCHORS,
                                                                   pState->AdditionalStore.m_hStore, pState->RootStore.m_hStore,
                                                                   &aTimes[iTime].TimeSpec,
                                                                   VerifyExecCertVerifyCallback, pState,
                                                                   pInfo->pvExternalData, pInfo->cbExternalData, pErrInfo);
                else
                    rc = RTCrPkcs7VerifySignedData(pContentInfo,
                                                   RTCRPKCS7VERIFY_SD_F_COUNTER_SIGNATURE_SIGNING_TIME_ONLY
                                                   | RTCRPKCS7VERIFY_SD_F_ALWAYS_USE_SIGNING_TIME_IF_PRESENT
                                                   | RTCRPKCS7VERIFY_SD_F_ALWAYS_USE_MS_TIMESTAMP_IF_PRESENT
                                                   | RTCRPKCS7VERIFY_SD_F_CHECK_TRUST_ANCHORS,
                                                   pState->AdditionalStore.m_hStore, pState->RootStore.m_hStore,
                                                   &aTimes[iTime].TimeSpec,
                                                   VerifyExecCertVerifyCallback, pState, pErrInfo);
                if (RT_SUCCESS(rc))
                {
                    Assert(rc == VINF_SUCCESS || rc == VINF_CR_DIGEST_DEPRECATED);
                    const char *pszNote = rc == VINF_CR_DIGEST_DEPRECATED ? " (deprecated digest)" : "";
                    if (pInfo->cSignatures == 1)
                        RTMsgInfo("'%s' is valid %s%s.\n", pState->pszFilename, aTimes[iTime].pszDesc, pszNote);
                    else
                        RTMsgInfo("'%s' signature #%u is valid %s%s.\n",
                                  pState->pszFilename, pInfo->iSignature + 1, aTimes[iTime].pszDesc, pszNote);
                    pState->cOkay++;
                    return VINF_SUCCESS;
                }
                if (rc != VERR_CR_X509_CPV_NOT_VALID_AT_TIME)
                {
                    if (pInfo->cSignatures == 1)
                        RTMsgError("%s: Failed to verify signature: %Rrc%#RTeim\n", pState->pszFilename, rc, pErrInfo);
                    else
                        RTMsgError("%s: Failed to verify signature #%u: %Rrc%#RTeim\n",
                                   pState->pszFilename, pInfo->iSignature + 1, rc, pErrInfo);
                    pState->cBad++;
                    return VINF_SUCCESS;
                }
            }

            if (pInfo->cSignatures == 1)
                RTMsgError("%s: Signature is not valid at present or link time.\n", pState->pszFilename);
            else
                RTMsgError("%s: Signature #%u is not valid at present or link time.\n",
                           pState->pszFilename, pInfo->iSignature + 1);
            pState->cBad++;
            return VINF_SUCCESS;
        }

        default:
            return RTErrInfoSetF(pErrInfo, VERR_NOT_SUPPORTED, "Unsupported signature type: %d", pInfo->enmType);
    }
}

/**
 * Worker for HandleVerifyExe.
 */
static RTEXITCODE HandleVerifyExeWorker(VERIFYEXESTATE *pState, const char *pszFilename, PRTERRINFOSTATIC pStaticErrInfo)
{
    /*
     * Open the executable image and verify it.
     */
    RTLDRMOD hLdrMod;
    int rc = RTLdrOpen(pszFilename, RTLDR_O_FOR_VALIDATION, pState->enmLdrArch, &hLdrMod);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error opening executable image '%s': %Rrc", pszFilename, rc);

    /* Reset the state. */
    pState->cBad        = 0;
    pState->cOkay       = 0;
    pState->pszFilename = pszFilename;

    rc = RTLdrVerifySignature(hLdrMod, VerifyExeCallback, pState, RTErrInfoInitStatic(pStaticErrInfo));
    if (RT_FAILURE(rc))
        RTMsgError("RTLdrVerifySignature failed on '%s': %Rrc - %s\n", pszFilename, rc, pStaticErrInfo->szMsg);

    int rc2 = RTLdrClose(hLdrMod);
    if (RT_FAILURE(rc2))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTLdrClose failed: %Rrc\n", rc2);
    if (RT_FAILURE(rc))
        return rc != VERR_LDRVI_NOT_SIGNED ? RTEXITCODE_FAILURE : RTEXITCODE_SKIPPED;

    return pState->cOkay > 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


static RTEXITCODE HandleVerifyExe(int cArgs, char **papszArgs)
{
    RTERRINFOSTATIC StaticErrInfo;

    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--kernel",                           'k', RTGETOPT_REQ_NOTHING },
        { "--root",                             'r', RTGETOPT_REQ_STRING },
        { "--self-signed-roots-from-system",    'R', RTGETOPT_REQ_NOTHING },
        { "--additional",                       'a', RTGETOPT_REQ_STRING },
        { "--add",                              'a', RTGETOPT_REQ_STRING },
        { "--type",                             't', RTGETOPT_REQ_STRING },
        { "--validation-time",                  'T', RTGETOPT_REQ_STRING },
        { "--verbose",                          'v', RTGETOPT_REQ_NOTHING },
        { "--quiet",                            'q', RTGETOPT_REQ_NOTHING },
    };

    VERIFYEXESTATE State;
    int rc = RTCrStoreCreateInMem(&State.RootStore.m_hStore, 0);
    if (RT_SUCCESS(rc))
        rc = RTCrStoreCreateInMem(&State.KernelRootStore.m_hStore, 0);
    if (RT_SUCCESS(rc))
        rc = RTCrStoreCreateInMem(&State.AdditionalStore.m_hStore, 0);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error creating in-memory certificate store: %Rrc", rc);

    RTGETOPTSTATE GetState;
    rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) && ch != VINF_GETOPT_NOT_OPTION)
    {
        switch (ch)
        {
            case 'a':
                if (!State.AdditionalStore.addFromFile(ValueUnion.psz, &StaticErrInfo))
                    return RTEXITCODE_FAILURE;
                break;

            case 'r':
                if (!State.RootStore.addFromFile(ValueUnion.psz, &StaticErrInfo))
                    return RTEXITCODE_FAILURE;
                break;

            case 'R':
                if (!State.RootStore.addSelfSignedRootsFromSystem(&StaticErrInfo))
                    return RTEXITCODE_FAILURE;
                break;

            case 't':
                if (!strcmp(ValueUnion.psz, "win") || !strcmp(ValueUnion.psz, "windows"))
                    State.enmSignType = VERIFYEXESTATE::kSignType_Windows;
                else if (!strcmp(ValueUnion.psz, "osx") || !strcmp(ValueUnion.psz, "apple"))
                    State.enmSignType = VERIFYEXESTATE::kSignType_OSX;
                else
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown signing type: '%s'", ValueUnion.psz);
                break;

            case 'T':
                if (!RTTimeSpecFromString(&State.ValidationTime, ValueUnion.psz))
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Invalid validation time (%s): %Rrc", ValueUnion.psz, rc);
                break;

            case 'k': State.fKernel = true; break;
            case 'v': State.cVerbose++; break;
            case 'q': State.cVerbose = 0; break;
            case 'V': return HandleVersion(cArgs, papszArgs);
            case 'h': return HelpVerifyExe(g_pStdOut, RTSIGNTOOLHELP_FULL);
            default:  return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (ch != VINF_GETOPT_NOT_OPTION)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No executable given.");

    /*
     * Populate the certificate stores according to the signing type.
     */
# ifdef VBOX
    unsigned          cSets = 0;
    struct STSTORESET aSets[6];
    switch (State.enmSignType)
    {
        case VERIFYEXESTATE::kSignType_Windows:
            aSets[cSets].hStore  = State.RootStore.m_hStore;
            aSets[cSets].paTAs   = g_aSUPTimestampTAs;
            aSets[cSets].cTAs    = g_cSUPTimestampTAs;
            cSets++;
            aSets[cSets].hStore  = State.RootStore.m_hStore;
            aSets[cSets].paTAs   = g_aSUPSpcRootTAs;
            aSets[cSets].cTAs    = g_cSUPSpcRootTAs;
            cSets++;
            aSets[cSets].hStore  = State.RootStore.m_hStore;
            aSets[cSets].paTAs   = g_aSUPNtKernelRootTAs;
            aSets[cSets].cTAs    = g_cSUPNtKernelRootTAs;
            cSets++;
            aSets[cSets].hStore  = State.KernelRootStore.m_hStore;
            aSets[cSets].paTAs   = g_aSUPNtKernelRootTAs;
            aSets[cSets].cTAs    = g_cSUPNtKernelRootTAs;
            cSets++;
            break;

        case VERIFYEXESTATE::kSignType_OSX:
            aSets[cSets].hStore  = State.RootStore.m_hStore;
            aSets[cSets].paTAs   = g_aSUPAppleRootTAs;
            aSets[cSets].cTAs    = g_cSUPAppleRootTAs;
            cSets++;
            break;
    }
    for (unsigned i = 0; i < cSets; i++)
        for (unsigned j = 0; j < aSets[i].cTAs; j++)
        {
            rc = RTCrStoreCertAddEncoded(aSets[i].hStore, RTCRCERTCTX_F_ENC_TAF_DER, aSets[i].paTAs[j].pch,
                                         aSets[i].paTAs[j].cb, RTErrInfoInitStatic(&StaticErrInfo));
            if (RT_FAILURE(rc))
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTCrStoreCertAddEncoded failed (%u/%u): %s",
                                      i, j, StaticErrInfo.szMsg);
        }
# endif /* VBOX */

    /*
     * Do it.
     */
    RTEXITCODE rcExit;
    for (;;)
    {
        rcExit = HandleVerifyExeWorker(&State, ValueUnion.psz, &StaticErrInfo);
        if (rcExit != RTEXITCODE_SUCCESS)
            break;

        /*
         * Next file
         */
        ch = RTGetOpt(&GetState, &ValueUnion);
        if (ch == 0)
            break;
        if (ch != VINF_GETOPT_NOT_OPTION)
        {
            rcExit = RTGetOptPrintError(ch, &ValueUnion);
            break;
        }
    }

    return rcExit;
}

#endif /* !IPRT_IN_BUILD_TOOL */

/*
 * common code for show-exe and show-cat:
 */

/**
 * Display an object ID.
 *
 * @returns IPRT status code.
 * @param   pThis               The show exe instance data.
 * @param   pObjId              The object ID to display.
 * @param   pszLabel            The field label (prefixed by szPrefix).
 * @param   pszPost             What to print after the ID (typically newline).
 */
static void HandleShowExeWorkerDisplayObjId(PSHOWEXEPKCS7 pThis, PCRTASN1OBJID pObjId, const char *pszLabel, const char *pszPost)
{
    int rc = RTAsn1QueryObjIdName(pObjId, pThis->szTmp, sizeof(pThis->szTmp));
    if (RT_SUCCESS(rc))
    {
        if (pThis->cVerbosity > 1)
            RTPrintf("%s%s%s (%s)%s", pThis->szPrefix, pszLabel, pThis->szTmp, pObjId->szObjId, pszPost);
        else
            RTPrintf("%s%s%s%s", pThis->szPrefix, pszLabel, pThis->szTmp, pszPost);
    }
    else
        RTPrintf("%s%s%s%s", pThis->szPrefix, pszLabel, pObjId->szObjId, pszPost);
}


/**
 * Display an object ID, without prefix and label
 *
 * @returns IPRT status code.
 * @param   pThis               The show exe instance data.
 * @param   pObjId              The object ID to display.
 * @param   pszPost             What to print after the ID (typically newline).
 */
static void HandleShowExeWorkerDisplayObjIdSimple(PSHOWEXEPKCS7 pThis, PCRTASN1OBJID pObjId, const char *pszPost)
{
    int rc = RTAsn1QueryObjIdName(pObjId, pThis->szTmp, sizeof(pThis->szTmp));
    if (RT_SUCCESS(rc))
    {
        if (pThis->cVerbosity > 1)
            RTPrintf("%s (%s)%s", pThis->szTmp, pObjId->szObjId, pszPost);
        else
            RTPrintf("%s%s", pThis->szTmp, pszPost);
    }
    else
        RTPrintf("%s%s", pObjId->szObjId, pszPost);
}


/**
 * Display a signer info attribute.
 *
 * @returns IPRT status code.
 * @param   pThis               The show exe instance data.
 * @param   offPrefix           The current prefix offset.
 * @param   pAttr               The attribute to display.
 */
static int HandleShowExeWorkerPkcs7DisplayAttrib(PSHOWEXEPKCS7 pThis, size_t offPrefix, PCRTCRPKCS7ATTRIBUTE pAttr)
{
    HandleShowExeWorkerDisplayObjId(pThis, &pAttr->Type, "", ":\n");
    if (pThis->cVerbosity > 4 && pAttr->SeqCore.Asn1Core.uData.pu8)
        RTPrintf("%s uData.pu8=%p cb=%#x\n", pThis->szPrefix, pAttr->SeqCore.Asn1Core.uData.pu8, pAttr->SeqCore.Asn1Core.cb);

    int rc = VINF_SUCCESS;
    switch (pAttr->enmType)
    {
        case RTCRPKCS7ATTRIBUTETYPE_UNKNOWN:
            if (pAttr->uValues.pCores->cItems <= 1)
                RTPrintf("%s %u bytes\n", pThis->szPrefix,pAttr->uValues.pCores->SetCore.Asn1Core.cb);
            else
                RTPrintf("%s %u bytes divided by %u items\n", pThis->szPrefix, pAttr->uValues.pCores->SetCore.Asn1Core.cb, pAttr->uValues.pCores->cItems);
            break;

        /* Object IDs, use pObjIds. */
        case RTCRPKCS7ATTRIBUTETYPE_OBJ_IDS:
            if (pAttr->uValues.pObjIds->cItems != 1)
                RTPrintf("%s%u object IDs:", pThis->szPrefix, pAttr->uValues.pObjIds->cItems);
            for (unsigned i = 0; i < pAttr->uValues.pObjIds->cItems; i++)
            {
                if (pAttr->uValues.pObjIds->cItems == 1)
                    RTPrintf("%s ", pThis->szPrefix);
                else
                    RTPrintf("%s ObjId[%u]: ", pThis->szPrefix, i);
                HandleShowExeWorkerDisplayObjIdSimple(pThis, pAttr->uValues.pObjIds->papItems[i], "\n");
            }
            break;

        /* Sequence of object IDs, use pObjIdSeqs. */
        case RTCRPKCS7ATTRIBUTETYPE_MS_STATEMENT_TYPE:
            if (pAttr->uValues.pObjIdSeqs->cItems != 1)
                RTPrintf("%s%u object IDs:", pThis->szPrefix, pAttr->uValues.pObjIdSeqs->cItems);
            for (unsigned i = 0; i < pAttr->uValues.pObjIdSeqs->cItems; i++)
            {
                uint32_t const cObjIds = pAttr->uValues.pObjIdSeqs->papItems[i]->cItems;
                for (unsigned j = 0; j < cObjIds; j++)
                {
                    if (pAttr->uValues.pObjIdSeqs->cItems == 1)
                        RTPrintf("%s ", pThis->szPrefix);
                    else
                        RTPrintf("%s ObjIdSeq[%u]: ", pThis->szPrefix, i);
                    if (cObjIds != 1)
                        RTPrintf(" ObjId[%u]: ", j);
                    HandleShowExeWorkerDisplayObjIdSimple(pThis, pAttr->uValues.pObjIdSeqs->papItems[i]->papItems[i], "\n");
                }
            }
            break;

        /* Octet strings, use pOctetStrings. */
        case RTCRPKCS7ATTRIBUTETYPE_OCTET_STRINGS:
            if (pAttr->uValues.pOctetStrings->cItems != 1)
                RTPrintf("%s%u octet strings:", pThis->szPrefix, pAttr->uValues.pOctetStrings->cItems);
            for (unsigned i = 0; i < pAttr->uValues.pOctetStrings->cItems; i++)
            {
                PCRTASN1OCTETSTRING pOctetString = pAttr->uValues.pOctetStrings->papItems[i];
                uint32_t cbContent = pOctetString->Asn1Core.cb;
                if (cbContent > 0 && (cbContent <= 128 || pThis->cVerbosity >= 2))
                {
                    uint8_t const *pbContent = pOctetString->Asn1Core.uData.pu8;
                    uint32_t       off       = 0;
                    while (off < cbContent)
                    {
                        uint32_t cbNow = RT_MIN(cbContent - off, 16);
                        if (pAttr->uValues.pOctetStrings->cItems == 1)
                            RTPrintf("%s %#06x: %.*Rhxs\n", pThis->szPrefix, off, cbNow, &pbContent[off]);
                        else
                            RTPrintf("%s OctetString[%u]: %#06x: %.*Rhxs\n", pThis->szPrefix, i, off, cbNow, &pbContent[off]);
                        off += cbNow;
                    }
                }
                else
                    RTPrintf("%s: OctetString[%u]: %u bytes\n", pThis->szPrefix, i, pOctetString->Asn1Core.cb);
            }
            break;

        /* Counter signatures (PKCS \#9), use pCounterSignatures. */
        case RTCRPKCS7ATTRIBUTETYPE_COUNTER_SIGNATURES:
            RTPrintf("%s%u counter signatures, %u bytes in total\n", pThis->szPrefix,
                     pAttr->uValues.pCounterSignatures->cItems, pAttr->uValues.pCounterSignatures->SetCore.Asn1Core.cb);
            for (uint32_t i = 0; i < pAttr->uValues.pCounterSignatures->cItems; i++)
            {
                size_t offPrefix2 = offPrefix;
                if (pAttr->uValues.pContentInfos->cItems > 1)
                    offPrefix2 += RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix, "CounterSig[%u]: ", i);
                else
                    offPrefix2 += RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix, "  ");

                int rc2 = HandleShowExeWorkerPkcs7DisplaySignerInfo(pThis, offPrefix2,
                                                                    pAttr->uValues.pCounterSignatures->papItems[i]);
                if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                    rc = rc2;
            }
            break;

        /* Signing time (PKCS \#9), use pSigningTime. */
        case RTCRPKCS7ATTRIBUTETYPE_SIGNING_TIME:
            for (uint32_t i = 0; i < pAttr->uValues.pSigningTime->cItems; i++)
            {
                PCRTASN1TIME pTime = pAttr->uValues.pSigningTime->papItems[i];
                char szTS[RTTIME_STR_LEN];
                RTTimeToString(&pTime->Time, szTS, sizeof(szTS));
                if (pAttr->uValues.pSigningTime->cItems == 1)
                    RTPrintf("%s %s (%.*s)\n", pThis->szPrefix, szTS, pTime->Asn1Core.cb, pTime->Asn1Core.uData.pch);
                else
                    RTPrintf("%s #%u: %s (%.*s)\n", pThis->szPrefix, i, szTS, pTime->Asn1Core.cb, pTime->Asn1Core.uData.pch);
            }
            break;

        /* Microsoft timestamp info (RFC-3161) signed data, use pContentInfo. */
        case RTCRPKCS7ATTRIBUTETYPE_MS_TIMESTAMP:
        case RTCRPKCS7ATTRIBUTETYPE_MS_NESTED_SIGNATURE:
            if (pAttr->uValues.pContentInfos->cItems > 1)
                RTPrintf("%s%u nested signatures, %u bytes in total\n", pThis->szPrefix,
                         pAttr->uValues.pContentInfos->cItems, pAttr->uValues.pContentInfos->SetCore.Asn1Core.cb);
            for (unsigned i = 0; i < pAttr->uValues.pContentInfos->cItems; i++)
            {
                size_t offPrefix2 = offPrefix;
                if (pAttr->uValues.pContentInfos->cItems > 1)
                    offPrefix2 += RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix, "NestedSig[%u]: ", i);
                else
                    offPrefix2 += RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix, "  ");
                //    offPrefix2 += RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix, "NestedSig: ", i);
                PCRTCRPKCS7CONTENTINFO pContentInfo = pAttr->uValues.pContentInfos->papItems[i];
                int rc2;
                if (RTCrPkcs7ContentInfo_IsSignedData(pContentInfo))
                    rc2 = HandleShowExeWorkerPkcs7Display(pThis, pContentInfo->u.pSignedData, offPrefix2, pContentInfo);
                else
                    rc2 = RTMsgErrorRc(VERR_ASN1_UNEXPECTED_OBJ_ID, "%sPKCS#7 content in nested signature is not 'signedData': %s",
                                       pThis->szPrefix, pContentInfo->ContentType.szObjId);
                if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                    rc = rc2;
            }
            break;

        case RTCRPKCS7ATTRIBUTETYPE_APPLE_MULTI_CD_PLIST:
            if (pAttr->uValues.pContentInfos->cItems != 1)
                RTPrintf("%s%u plists, expected only 1.\n", pThis->szPrefix, pAttr->uValues.pOctetStrings->cItems);
            for (unsigned i = 0; i < pAttr->uValues.pOctetStrings->cItems; i++)
            {
                PCRTASN1OCTETSTRING pOctetString = pAttr->uValues.pOctetStrings->papItems[i];
                size_t              cbContent    = pOctetString->Asn1Core.cb;
                char  const        *pchContent   = pOctetString->Asn1Core.uData.pch;
                rc = RTStrValidateEncodingEx(pchContent, cbContent, RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
                if (RT_SUCCESS(rc))
                {
                    while (cbContent > 0)
                    {
                        const char *pchNewLine = (const char *)memchr(pchContent, '\n', cbContent);
                        size_t      cchToWrite = pchNewLine ? pchNewLine - pchContent : cbContent;
                        if (pAttr->uValues.pOctetStrings->cItems == 1)
                            RTPrintf("%s %.*s\n", pThis->szPrefix, cchToWrite, pchContent);
                        else
                            RTPrintf("%s plist[%u]: %.*s\n", pThis->szPrefix, i, cchToWrite, pchContent);
                        if (!pchNewLine)
                            break;
                        pchContent = pchNewLine + 1;
                        cbContent -= cchToWrite + 1;
                    }
                }
                else
                {
                    if (pAttr->uValues.pContentInfos->cItems != 1)
                        RTPrintf("%s: plist[%u]: Invalid UTF-8: %Rrc\n", pThis->szPrefix, i, rc);
                    else
                        RTPrintf("%s: Invalid UTF-8: %Rrc\n", pThis->szPrefix, rc);
                    for (uint32_t off = 0; off < cbContent; off += 16)
                    {
                        size_t cbNow = RT_MIN(cbContent - off, 16);
                        if (pAttr->uValues.pOctetStrings->cItems == 1)
                            RTPrintf("%s %#06x: %.*Rhxs\n", pThis->szPrefix, off, cbNow, &pchContent[off]);
                        else
                            RTPrintf("%s plist[%u]: %#06x: %.*Rhxs\n", pThis->szPrefix, i, off, cbNow, &pchContent[off]);
                    }
                }
            }
            break;

        case RTCRPKCS7ATTRIBUTETYPE_INVALID:
            RTPrintf("%sINVALID!\n", pThis->szPrefix);
            break;
        case RTCRPKCS7ATTRIBUTETYPE_NOT_PRESENT:
            RTPrintf("%sNOT PRESENT!\n", pThis->szPrefix);
            break;
        default:
            RTPrintf("%senmType=%d!\n", pThis->szPrefix, pAttr->enmType);
            break;
    }
    return rc;
}


/**
 * Displays a SignerInfo structure.
 *
 * @returns IPRT status code.
 * @param   pThis               The show exe instance data.
 * @param   offPrefix           The current prefix offset.
 * @param   pSignerInfo         The structure to display.
 */
static int HandleShowExeWorkerPkcs7DisplaySignerInfo(PSHOWEXEPKCS7 pThis, size_t offPrefix, PCRTCRPKCS7SIGNERINFO pSignerInfo)
{
    int rc = RTAsn1Integer_ToString(&pSignerInfo->IssuerAndSerialNumber.SerialNumber,
                                    pThis->szTmp, sizeof(pThis->szTmp), 0 /*fFlags*/, NULL);
    if (RT_FAILURE(rc))
        RTStrPrintf(pThis->szTmp, sizeof(pThis->szTmp), "%Rrc", rc);
    RTPrintf("%s                  Serial No: %s\n", pThis->szPrefix, pThis->szTmp);

    rc = RTCrX509Name_FormatAsString(&pSignerInfo->IssuerAndSerialNumber.Name, pThis->szTmp, sizeof(pThis->szTmp), NULL);
    if (RT_FAILURE(rc))
        RTStrPrintf(pThis->szTmp, sizeof(pThis->szTmp), "%Rrc", rc);
    RTPrintf("%s                     Issuer: %s\n", pThis->szPrefix, pThis->szTmp);

    const char *pszType = RTCrDigestTypeToName(RTCrX509AlgorithmIdentifier_GetDigestType(&pSignerInfo->DigestAlgorithm,
                                                                                         true /*fPureDigestsOnly*/));
    if (!pszType)
        pszType = pSignerInfo->DigestAlgorithm.Algorithm.szObjId;
    RTPrintf("%s           Digest Algorithm: %s", pThis->szPrefix, pszType);
    if (pThis->cVerbosity > 1)
        RTPrintf(" (%s)\n", pSignerInfo->DigestAlgorithm.Algorithm.szObjId);
    else
        RTPrintf("\n");

    HandleShowExeWorkerDisplayObjId(pThis, &pSignerInfo->DigestEncryptionAlgorithm.Algorithm,
                                    "Digest Encryption Algorithm: ", "\n");

    if (pSignerInfo->AuthenticatedAttributes.cItems == 0)
        RTPrintf("%s   Authenticated Attributes: none\n", pThis->szPrefix);
    else
    {
        RTPrintf("%s   Authenticated Attributes: %u item%s\n", pThis->szPrefix,
                 pSignerInfo->AuthenticatedAttributes.cItems, pSignerInfo->AuthenticatedAttributes.cItems > 1 ? "s" : "");
        for (unsigned j = 0; j < pSignerInfo->AuthenticatedAttributes.cItems; j++)
        {
            PRTCRPKCS7ATTRIBUTE pAttr = pSignerInfo->AuthenticatedAttributes.papItems[j];
            size_t offPrefix3 = offPrefix+ RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix,
                                                         "              AuthAttrib[%u]: ", j);
            HandleShowExeWorkerPkcs7DisplayAttrib(pThis, offPrefix3, pAttr);
        }
        pThis->szPrefix[offPrefix] = '\0';
    }

    if (pSignerInfo->UnauthenticatedAttributes.cItems == 0)
        RTPrintf("%s Unauthenticated Attributes: none\n", pThis->szPrefix);
    else
    {
        RTPrintf("%s Unauthenticated Attributes: %u item%s\n", pThis->szPrefix,
                 pSignerInfo->UnauthenticatedAttributes.cItems, pSignerInfo->UnauthenticatedAttributes.cItems > 1 ? "s" : "");
        for (unsigned j = 0; j < pSignerInfo->UnauthenticatedAttributes.cItems; j++)
        {
            PRTCRPKCS7ATTRIBUTE pAttr = pSignerInfo->UnauthenticatedAttributes.papItems[j];
            size_t offPrefix3 = offPrefix + RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix,
                                                        "            UnauthAttrib[%u]: ", j);
            HandleShowExeWorkerPkcs7DisplayAttrib(pThis, offPrefix3, pAttr);
        }
        pThis->szPrefix[offPrefix] = '\0';
    }

    /** @todo show the encrypted stuff (EncryptedDigest)?   */
    return rc;
}


/**
 * Displays a Microsoft SPC indirect data structure.
 *
 * @returns IPRT status code.
 * @param   pThis               The show exe instance data.
 * @param   offPrefix           The current prefix offset.
 * @param   pIndData            The indirect data to display.
 */
static int HandleShowExeWorkerPkcs7DisplaySpcIdirectDataContent(PSHOWEXEPKCS7 pThis, size_t offPrefix,
                                                                PCRTCRSPCINDIRECTDATACONTENT pIndData)
{
    /*
     * The image hash.
     */
    RTDIGESTTYPE const enmDigestType = RTCrX509AlgorithmIdentifier_GetDigestType(&pIndData->DigestInfo.DigestAlgorithm,
                                                                                 true /*fPureDigestsOnly*/);
    const char        *pszDigestType = RTCrDigestTypeToName(enmDigestType);
    RTPrintf("%s Digest Type: %s", pThis->szPrefix, pszDigestType);
    if (pThis->cVerbosity > 1)
        RTPrintf(" (%s)\n", pIndData->DigestInfo.DigestAlgorithm.Algorithm.szObjId);
    else
        RTPrintf("\n");
    RTPrintf("%s      Digest: %.*Rhxs\n",
             pThis->szPrefix, pIndData->DigestInfo.Digest.Asn1Core.cb, pIndData->DigestInfo.Digest.Asn1Core.uData.pu8);

    /*
     * The data/file/url.
     */
    switch (pIndData->Data.enmType)
    {
        case RTCRSPCAAOVTYPE_PE_IMAGE_DATA:
        {
            RTPrintf("%s   Data Type: PE Image Data\n", pThis->szPrefix);
            PRTCRSPCPEIMAGEDATA pPeImage = pIndData->Data.uValue.pPeImage;
            /** @todo display "Flags". */

            switch (pPeImage->T0.File.enmChoice)
            {
                case RTCRSPCLINKCHOICE_MONIKER:
                {
                    PRTCRSPCSERIALIZEDOBJECT pMoniker = pPeImage->T0.File.u.pMoniker;
                    if (RTCrSpcSerializedObject_IsPresent(pMoniker))
                    {
                        if (RTUuidCompareStr(pMoniker->Uuid.Asn1Core.uData.pUuid, RTCRSPCSERIALIZEDOBJECT_UUID_STR) == 0)
                        {
                            RTPrintf("%s     Moniker: SpcSerializedObject (%RTuuid)\n",
                                     pThis->szPrefix, pMoniker->Uuid.Asn1Core.uData.pUuid);

                            PCRTCRSPCSERIALIZEDOBJECTATTRIBUTES pData = pMoniker->u.pData;
                            if (pData)
                                for (uint32_t i = 0; i < pData->cItems; i++)
                                {
                                    RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix,
                                                "MonikerAttrib[%u]: ", i);

                                    switch (pData->papItems[i]->enmType)
                                    {
                                        case RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE_PAGE_HASHES_V2:
                                        case RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE_PAGE_HASHES_V1:
                                        {
                                            PCRTCRSPCSERIALIZEDPAGEHASHES pPgHashes = pData->papItems[i]->u.pPageHashes;
                                            uint32_t const cbHash =    pData->papItems[i]->enmType
                                                                    == RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE_PAGE_HASHES_V1
                                                                  ? 160/8 /*SHA-1*/ : 256/8 /*SHA-256*/;
                                            uint32_t const cPages = pPgHashes->RawData.Asn1Core.cb / (cbHash + sizeof(uint32_t));

                                            RTPrintf("%sPage Hashes version %u - %u pages (%u bytes total)\n", pThis->szPrefix,
                                                        pData->papItems[i]->enmType
                                                     == RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE_PAGE_HASHES_V1 ? 1 : 2,
                                                     cPages, pPgHashes->RawData.Asn1Core.cb);
                                            if (pThis->cVerbosity > 0)
                                            {
                                                PCRTCRSPCPEIMAGEPAGEHASHES pPg = pPgHashes->pData;
                                                for (unsigned iPg = 0; iPg < cPages; iPg++)
                                                {
                                                    uint32_t offHash = 0;
                                                    do
                                                    {
                                                        if (offHash == 0)
                                                            RTPrintf("%.*s  Page#%04u/%#08x: ",
                                                                     offPrefix, pThis->szPrefix, iPg, pPg->Generic.offFile);
                                                        else
                                                            RTPrintf("%.*s                      ", offPrefix, pThis->szPrefix);
                                                        uint32_t cbLeft = cbHash - offHash;
                                                        if (cbLeft > 24)
                                                            cbLeft = 16;
                                                        RTPrintf("%.*Rhxs\n", cbLeft, &pPg->Generic.abHash[offHash]);
                                                        offHash += cbLeft;
                                                    } while (offHash < cbHash);
                                                    pPg = (PCRTCRSPCPEIMAGEPAGEHASHES)&pPg->Generic.abHash[cbHash];
                                                }

                                                if (pThis->cVerbosity > 3)
                                                    RTPrintf("%.*Rhxd\n",
                                                             pPgHashes->RawData.Asn1Core.cb,
                                                             pPgHashes->RawData.Asn1Core.uData.pu8);
                                            }
                                            break;
                                        }

                                        case RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE_UNKNOWN:
                                            HandleShowExeWorkerDisplayObjIdSimple(pThis, &pData->papItems[i]->Type, "\n");
                                            break;
                                        case RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE_NOT_PRESENT:
                                            RTPrintf("%sNot present!\n", pThis->szPrefix);
                                            break;
                                        default:
                                            RTPrintf("%senmType=%d!\n", pThis->szPrefix, pData->papItems[i]->enmType);
                                            break;
                                    }
                                    pThis->szPrefix[offPrefix] = '\0';
                                }
                            else
                                RTPrintf("%s              pData is NULL!\n", pThis->szPrefix);
                        }
                        else
                            RTPrintf("%s     Moniker: Unknown UUID: %RTuuid\n",
                                     pThis->szPrefix, pMoniker->Uuid.Asn1Core.uData.pUuid);
                    }
                    else
                        RTPrintf("%s     Moniker: not present\n", pThis->szPrefix);
                    break;
                }

                case RTCRSPCLINKCHOICE_URL:
                {
                    const char *pszUrl = NULL;
                    int rc = pPeImage->T0.File.u.pUrl
                           ? RTAsn1String_QueryUtf8(pPeImage->T0.File.u.pUrl, &pszUrl, NULL)
                           : VERR_NOT_FOUND;
                    if (RT_SUCCESS(rc))
                        RTPrintf("%s         URL: '%s'\n", pThis->szPrefix, pszUrl);
                    else
                        RTPrintf("%s         URL: rc=%Rrc\n", pThis->szPrefix, rc);
                    break;
                }

                case RTCRSPCLINKCHOICE_FILE:
                {
                    const char *pszFile = NULL;
                    int rc = pPeImage->T0.File.u.pT2 && pPeImage->T0.File.u.pT2->File.u.pAscii
                           ? RTAsn1String_QueryUtf8(pPeImage->T0.File.u.pT2->File.u.pAscii, &pszFile, NULL)
                           : VERR_NOT_FOUND;
                    if (RT_SUCCESS(rc))
                        RTPrintf("%s        File: '%s'\n", pThis->szPrefix, pszFile);
                    else
                        RTPrintf("%s        File: rc=%Rrc\n", pThis->szPrefix, rc);
                    if (pThis->cVerbosity > 4 && pPeImage->T0.File.u.pT2 == NULL)
                        RTPrintf("%s        pT2=NULL\n", pThis->szPrefix);
                    else if (pThis->cVerbosity > 4)
                    {
                        PCRTASN1STRING pStr = pPeImage->T0.File.u.pT2->File.u.pAscii;
                        RTPrintf("%s        pT2=%p/%p LB %#x fFlags=%#x pOps=%p (%s)\n"
                                 "%s        enmChoice=%d pStr=%p/%p LB %#x fFlags=%#x\n",
                                 pThis->szPrefix,
                                 pPeImage->T0.File.u.pT2,
                                 pPeImage->T0.File.u.pT2->CtxTag2.Asn1Core.uData.pu8,
                                 pPeImage->T0.File.u.pT2->CtxTag2.Asn1Core.cb,
                                 pPeImage->T0.File.u.pT2->CtxTag2.Asn1Core.fFlags,
                                 pPeImage->T0.File.u.pT2->CtxTag2.Asn1Core.pOps,
                                 pPeImage->T0.File.u.pT2->CtxTag2.Asn1Core.pOps
                                 ? pPeImage->T0.File.u.pT2->CtxTag2.Asn1Core.pOps->pszName : "",
                                 pThis->szPrefix,
                                 pPeImage->T0.File.u.pT2->File.enmChoice,
                                 pStr,
                                 pStr ? pStr->Asn1Core.uData.pu8 : NULL,
                                 pStr ? pStr->Asn1Core.cb : 0,
                                 pStr ? pStr->Asn1Core.fFlags : 0);
                    }
                    break;
                }

                case RTCRSPCLINKCHOICE_NOT_PRESENT:
                    RTPrintf("%s              File not present!\n", pThis->szPrefix);
                    break;
                default:
                    RTPrintf("%s              enmChoice=%d!\n", pThis->szPrefix, pPeImage->T0.File.enmChoice);
                    break;
            }
            break;
        }

        case RTCRSPCAAOVTYPE_UNKNOWN:
            HandleShowExeWorkerDisplayObjId(pThis, &pIndData->Data.Type, "   Data Type: ", "\n");
            break;
        case RTCRSPCAAOVTYPE_NOT_PRESENT:
            RTPrintf("%s   Data Type: Not present!\n", pThis->szPrefix);
            break;
        default:
            RTPrintf("%s   Data Type: enmType=%d!\n", pThis->szPrefix, pIndData->Data.enmType);
            break;
    }

    return VINF_SUCCESS;
}


/**
 * Display an PKCS#7 signed data instance.
 *
 * @returns IPRT status code.
 * @param   pThis               The show exe instance data.
 * @param   pSignedData         The signed data to display.
 * @param   offPrefix           The current prefix offset.
 * @param   pContentInfo        The content info structure (for the size).
 */
static int HandleShowExeWorkerPkcs7Display(PSHOWEXEPKCS7 pThis, PRTCRPKCS7SIGNEDDATA pSignedData, size_t offPrefix,
                                           PCRTCRPKCS7CONTENTINFO pContentInfo)
{
    pThis->szPrefix[offPrefix] = '\0';
    RTPrintf("%sPKCS#7 signature: %u (%#x) bytes\n", pThis->szPrefix,
             RTASN1CORE_GET_RAW_ASN1_SIZE(&pContentInfo->SeqCore.Asn1Core),
             RTASN1CORE_GET_RAW_ASN1_SIZE(&pContentInfo->SeqCore.Asn1Core));

    /*
     * Display list of signing algorithms.
     */
    RTPrintf("%sDigestAlgorithms: ", pThis->szPrefix);
    if (pSignedData->DigestAlgorithms.cItems == 0)
        RTPrintf("none");
    for (unsigned i = 0; i < pSignedData->DigestAlgorithms.cItems; i++)
    {
        PCRTCRX509ALGORITHMIDENTIFIER pAlgoId = pSignedData->DigestAlgorithms.papItems[i];
        const char *pszDigestType = RTCrDigestTypeToName(RTCrX509AlgorithmIdentifier_GetDigestType(pAlgoId,
                                                                                                   true /*fPureDigestsOnly*/));
        if (!pszDigestType)
            pszDigestType = pAlgoId->Algorithm.szObjId;
        RTPrintf(i == 0 ? "%s" : ", %s", pszDigestType);
        if (pThis->cVerbosity > 1)
            RTPrintf(" (%s)", pAlgoId->Algorithm.szObjId);
    }
    RTPrintf("\n");

    /*
     * Display the signed data content.
     */
    if (RTAsn1ObjId_CompareWithString(&pSignedData->ContentInfo.ContentType, RTCRSPCINDIRECTDATACONTENT_OID) == 0)
    {
        RTPrintf("%s     ContentType: SpcIndirectDataContent (" RTCRSPCINDIRECTDATACONTENT_OID ")\n", pThis->szPrefix);
        size_t offPrefix2 = RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix, "    SPC Ind Data: ");
        HandleShowExeWorkerPkcs7DisplaySpcIdirectDataContent(pThis, offPrefix2 + offPrefix,
                                                             pSignedData->ContentInfo.u.pIndirectDataContent);
        pThis->szPrefix[offPrefix] = '\0';
    }
    else
    {
        HandleShowExeWorkerDisplayObjId(pThis, &pSignedData->ContentInfo.ContentType, "     ContentType: ", " - not implemented.\n");
        RTPrintf("%s                  %u (%#x) bytes\n", pThis->szPrefix,
                 pSignedData->ContentInfo.Content.Asn1Core.cb, pSignedData->ContentInfo.Content.Asn1Core.cb);
    }

    /*
     * Display certificates (Certificates).
     */
    if (pSignedData->Certificates.cItems > 0)
    {
        RTPrintf("%s    Certificates: %u\n", pThis->szPrefix, pSignedData->Certificates.cItems);
        for (uint32_t i = 0; i < pSignedData->Certificates.cItems; i++)
        {
            PCRTCRPKCS7CERT pCert = pSignedData->Certificates.papItems[i];
            if (i != 0 && pThis->cVerbosity >= 2)
                RTPrintf("\n");
            switch (pCert->enmChoice)
            {
                case RTCRPKCS7CERTCHOICE_X509:
                {
                    PCRTCRX509CERTIFICATE pX509Cert = pCert->u.pX509Cert;
                    int rc2 = RTAsn1QueryObjIdName(&pX509Cert->SignatureAlgorithm.Algorithm, pThis->szTmp, sizeof(pThis->szTmp));
                    RTPrintf("%s      Certificate #%u: %s\n", pThis->szPrefix, i,
                             RT_SUCCESS(rc2) ? pThis->szTmp : pX509Cert->SignatureAlgorithm.Algorithm.szObjId);

                    rc2 = RTCrX509Name_FormatAsString(&pX509Cert->TbsCertificate.Subject,
                                                      pThis->szTmp, sizeof(pThis->szTmp), NULL);
                    if (RT_FAILURE(rc2))
                        RTStrPrintf(pThis->szTmp, sizeof(pThis->szTmp), "%Rrc", rc2);
                    RTPrintf("%s        Subject: %s\n", pThis->szPrefix, pThis->szTmp);

                    rc2 = RTCrX509Name_FormatAsString(&pX509Cert->TbsCertificate.Issuer,
                                                      pThis->szTmp, sizeof(pThis->szTmp), NULL);
                    if (RT_FAILURE(rc2))
                        RTStrPrintf(pThis->szTmp, sizeof(pThis->szTmp), "%Rrc", rc2);
                    RTPrintf("%s         Issuer: %s\n", pThis->szPrefix, pThis->szTmp);


                    char szNotAfter[RTTIME_STR_LEN];
                    RTPrintf("%s          Valid: %s thru %s\n", pThis->szPrefix,
                             RTTimeToString(&pX509Cert->TbsCertificate.Validity.NotBefore.Time,
                                            pThis->szTmp, sizeof(pThis->szTmp)),
                             RTTimeToString(&pX509Cert->TbsCertificate.Validity.NotAfter.Time,
                                            szNotAfter, sizeof(szNotAfter)));
                    break;
                }

                default:
                    RTPrintf("%s      Certificate #%u: Unsupported type\n", pThis->szPrefix, i);
                    break;
            }


            if (pThis->cVerbosity >= 2)
                RTAsn1Dump(RTCrPkcs7Cert_GetAsn1Core(pSignedData->Certificates.papItems[i]), 0,
                           ((uint32_t)offPrefix + 9) / 2, RTStrmDumpPrintfV, g_pStdOut);
        }

        /** @todo display certificates properly. */
    }

    if (pSignedData->Crls.cb > 0)
        RTPrintf("%s            CRLs: %u bytes\n", pThis->szPrefix, pSignedData->Crls.cb);

    /*
     * Show signatures (SignerInfos).
     */
    unsigned const cSigInfos = pSignedData->SignerInfos.cItems;
    if (cSigInfos != 1)
        RTPrintf("%s     SignerInfos: %u signers\n", pThis->szPrefix, cSigInfos);
    else
        RTPrintf("%s     SignerInfos:\n", pThis->szPrefix);
    int rc = VINF_SUCCESS;
    for (unsigned i = 0; i < cSigInfos; i++)
    {
        size_t offPrefix2 = offPrefix;
        if (cSigInfos != 1)
            offPrefix2 += RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix, "SignerInfo[%u]: ", i);

        int rc2 = HandleShowExeWorkerPkcs7DisplaySignerInfo(pThis, offPrefix2, pSignedData->SignerInfos.papItems[i]);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;
    }
    pThis->szPrefix[offPrefix] = '\0';

    return rc;
}


/*
 * The 'show-exe' command.
 */
static RTEXITCODE HelpShowExe(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT, "show-exe [--verbose|-v] [--quiet|-q] <exe1> [exe2 [..]]\n");
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE HandleShowExe(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--verbose",      'v', RTGETOPT_REQ_NOTHING },
        { "--quiet",        'q', RTGETOPT_REQ_NOTHING },
    };

    unsigned  cVerbosity = 0;
    RTLDRARCH enmLdrArch = RTLDRARCH_WHATEVER;

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) && ch != VINF_GETOPT_NOT_OPTION)
    {
        switch (ch)
        {
            case 'v': cVerbosity++; break;
            case 'q': cVerbosity = 0; break;
            case 'V': return HandleVersion(cArgs, papszArgs);
            case 'h': return HelpShowExe(g_pStdOut, RTSIGNTOOLHELP_FULL);
            default:  return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (ch != VINF_GETOPT_NOT_OPTION)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No executable given.");

    /*
     * Do it.
     */
    unsigned   iFile  = 0;
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    do
    {
        RTPrintf(iFile == 0 ? "%s:\n" : "\n%s:\n", ValueUnion.psz);

        SHOWEXEPKCS7 This;
        RT_ZERO(This);
        This.cVerbosity = cVerbosity;

        RTEXITCODE rcExitThis = SignToolPkcs7Exe_InitFromFile(&This, ValueUnion.psz, cVerbosity, enmLdrArch);
        if (rcExitThis == RTEXITCODE_SUCCESS)
        {
            rc = HandleShowExeWorkerPkcs7Display(&This, This.pSignedData, 0, &This.ContentInfo);
            if (RT_FAILURE(rc))
                rcExit = RTEXITCODE_FAILURE;
            SignToolPkcs7Exe_Delete(&This);
        }
        if (rcExitThis != RTEXITCODE_SUCCESS && rcExit == RTEXITCODE_SUCCESS)
            rcExit = rcExitThis;

        iFile++;
    } while ((ch = RTGetOpt(&GetState, &ValueUnion)) == VINF_GETOPT_NOT_OPTION);
    if (ch != 0)
        return RTGetOptPrintError(ch, &ValueUnion);

    return rcExit;
}


/*
 * The 'show-cat' command.
 */
static RTEXITCODE HelpShowCat(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT, "show-cat [--verbose|-v] [--quiet|-q] <cat1> [cat2 [..]]\n");
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE HandleShowCat(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--verbose",      'v', RTGETOPT_REQ_NOTHING },
        { "--quiet",        'q', RTGETOPT_REQ_NOTHING },
    };

    unsigned  cVerbosity = 0;

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) && ch != VINF_GETOPT_NOT_OPTION)
    {
        switch (ch)
        {
            case 'v': cVerbosity++; break;
            case 'q': cVerbosity = 0; break;
            case 'V': return HandleVersion(cArgs, papszArgs);
            case 'h': return HelpShowCat(g_pStdOut, RTSIGNTOOLHELP_FULL);
            default:  return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (ch != VINF_GETOPT_NOT_OPTION)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No executable given.");

    /*
     * Do it.
     */
    unsigned   iFile  = 0;
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    do
    {
        RTPrintf(iFile == 0 ? "%s:\n" : "\n%s:\n", ValueUnion.psz);

        SHOWEXEPKCS7 This;
        RT_ZERO(This);
        This.cVerbosity = cVerbosity;

        RTEXITCODE rcExitThis = SignToolPkcs7_InitFromFile(&This, ValueUnion.psz, cVerbosity);
        if (rcExitThis == RTEXITCODE_SUCCESS)
        {
            This.hLdrMod = NIL_RTLDRMOD;

            rc = HandleShowExeWorkerPkcs7Display(&This, This.pSignedData, 0, &This.ContentInfo);
            if (RT_FAILURE(rc))
                rcExit = RTEXITCODE_FAILURE;
            SignToolPkcs7Exe_Delete(&This);
        }
        if (rcExitThis != RTEXITCODE_SUCCESS && rcExit == RTEXITCODE_SUCCESS)
            rcExit = rcExitThis;

        iFile++;
    } while ((ch = RTGetOpt(&GetState, &ValueUnion)) == VINF_GETOPT_NOT_OPTION);
    if (ch != 0)
        return RTGetOptPrintError(ch, &ValueUnion);

    return rcExit;
}


/*********************************************************************************************************************************
*   The 'hash-exe' command.                                                                                                      *
*********************************************************************************************************************************/
static RTEXITCODE HelpHashExe(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT, "hash-exe [--verbose|-v] [--quiet|-q] <exe1> [exe2 [..]]\n");
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE HandleHashExe(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--verbose",      'v', RTGETOPT_REQ_NOTHING },
        { "--quiet",        'q', RTGETOPT_REQ_NOTHING },
    };

    unsigned  cVerbosity = 0;
    RTLDRARCH enmLdrArch = RTLDRARCH_WHATEVER;

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) && ch != VINF_GETOPT_NOT_OPTION)
    {
        switch (ch)
        {
            case 'v': cVerbosity++; break;
            case 'q': cVerbosity = 0; break;
            case 'V': return HandleVersion(cArgs, papszArgs);
            case 'h': return HelpHashExe(g_pStdOut, RTSIGNTOOLHELP_FULL);
            default:  return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (ch != VINF_GETOPT_NOT_OPTION)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No executable given.");

    /*
     * Do it.
     */
    unsigned   iFile  = 0;
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    do
    {
        RTPrintf(iFile == 0 ? "%s:\n" : "\n%s:\n", ValueUnion.psz);

        RTERRINFOSTATIC ErrInfo;
        RTLDRMOD        hLdrMod;
        rc = RTLdrOpenEx(ValueUnion.psz, RTLDR_O_FOR_VALIDATION, enmLdrArch, &hLdrMod, RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(rc))
        {
            uint8_t abHash[RTSHA512_HASH_SIZE];
            char    szDigest[RTSHA512_DIGEST_LEN + 1];

            /* SHA-1: */
            rc = RTLdrHashImage(hLdrMod, RTDIGESTTYPE_SHA1, abHash, sizeof(abHash));
            if (RT_SUCCESS(rc))
                RTSha1ToString(abHash, szDigest, sizeof(szDigest));
            else
                RTStrPrintf(szDigest, sizeof(szDigest), "%Rrc", rc);
            RTPrintf("  SHA-1:   %s\n", szDigest);

            /* SHA-256: */
            rc = RTLdrHashImage(hLdrMod, RTDIGESTTYPE_SHA256, abHash, sizeof(abHash));
            if (RT_SUCCESS(rc))
                RTSha256ToString(abHash, szDigest, sizeof(szDigest));
            else
                RTStrPrintf(szDigest, sizeof(szDigest), "%Rrc", rc);
            RTPrintf("  SHA-256: %s\n", szDigest);

            /* SHA-512: */
            rc = RTLdrHashImage(hLdrMod, RTDIGESTTYPE_SHA512, abHash, sizeof(abHash));
            if (RT_SUCCESS(rc))
                RTSha512ToString(abHash, szDigest, sizeof(szDigest));
            else
                RTStrPrintf(szDigest, sizeof(szDigest), "%Rrc", rc);
            RTPrintf("  SHA-512: %s\n", szDigest);

            RTLdrClose(hLdrMod);
        }
        else
            rcExit = RTMsgErrorExitFailure("Failed to open '%s': %Rrc%#RTeim", ValueUnion.psz, rc, &ErrInfo.Core);

    } while ((ch = RTGetOpt(&GetState, &ValueUnion)) == VINF_GETOPT_NOT_OPTION);
    if (ch != 0)
        return RTGetOptPrintError(ch, &ValueUnion);

    return rcExit;
}


/*********************************************************************************************************************************
*   The 'make-tainfo' command.                                                                                                   *
*********************************************************************************************************************************/
static RTEXITCODE HelpMakeTaInfo(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT,
                        "make-tainfo [--verbose|--quiet] [--cert <cert.der>]  [-o|--output] <tainfo.der>\n");
    return RTEXITCODE_SUCCESS;
}


typedef struct MAKETAINFOSTATE
{
    int         cVerbose;
    const char *pszCert;
    const char *pszOutput;
} MAKETAINFOSTATE;


/** @callback_method_impl{FNRTASN1ENCODEWRITER}  */
static DECLCALLBACK(int) handleMakeTaInfoWriter(const void *pvBuf, size_t cbToWrite, void *pvUser, PRTERRINFO pErrInfo)
{
    RT_NOREF_PV(pErrInfo);
    return RTStrmWrite((PRTSTREAM)pvUser, pvBuf, cbToWrite);
}


static RTEXITCODE HandleMakeTaInfo(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--cert",         'c', RTGETOPT_REQ_STRING },
        { "--output",       'o', RTGETOPT_REQ_STRING },
        { "--verbose",      'v', RTGETOPT_REQ_NOTHING },
        { "--quiet",        'q', RTGETOPT_REQ_NOTHING },
    };

    MAKETAINFOSTATE State = { 0, NULL, NULL };

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'c':
                if (State.pszCert)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "The --cert option can only be used once.");
                State.pszCert = ValueUnion.psz;
                break;

            case 'o':
            case VINF_GETOPT_NOT_OPTION:
                if (State.pszOutput)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Multiple output files specified.");
                State.pszOutput = ValueUnion.psz;
                break;

            case 'v': State.cVerbose++; break;
            case 'q': State.cVerbose = 0; break;
            case 'V': return HandleVersion(cArgs, papszArgs);
            case 'h': return HelpMakeTaInfo(g_pStdOut, RTSIGNTOOLHELP_FULL);
            default:  return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (!State.pszCert)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No input certificate was specified.");
    if (!State.pszOutput)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No output file was specified.");

    /*
     * Read the certificate.
     */
    RTERRINFOSTATIC         StaticErrInfo;
    RTCRX509CERTIFICATE     Certificate;
    rc = RTCrX509Certificate_ReadFromFile(&Certificate, State.pszCert, 0, &g_RTAsn1DefaultAllocator,
                                          RTErrInfoInitStatic(&StaticErrInfo));
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error reading certificate from %s: %Rrc - %s",
                              State.pszCert, rc, StaticErrInfo.szMsg);
    /*
     * Construct the trust anchor information.
     */
    RTCRTAFTRUSTANCHORINFO TrustAnchor;
    rc = RTCrTafTrustAnchorInfo_Init(&TrustAnchor, &g_RTAsn1DefaultAllocator);
    if (RT_SUCCESS(rc))
    {
        /* Public key. */
        Assert(RTCrX509SubjectPublicKeyInfo_IsPresent(&TrustAnchor.PubKey));
        RTCrX509SubjectPublicKeyInfo_Delete(&TrustAnchor.PubKey);
        rc = RTCrX509SubjectPublicKeyInfo_Clone(&TrustAnchor.PubKey, &Certificate.TbsCertificate.SubjectPublicKeyInfo,
                                                &g_RTAsn1DefaultAllocator);
        if (RT_FAILURE(rc))
            RTMsgError("RTCrX509SubjectPublicKeyInfo_Clone failed: %Rrc", rc);
        RTAsn1Core_ResetImplict(RTCrX509SubjectPublicKeyInfo_GetAsn1Core(&TrustAnchor.PubKey)); /* temporary hack. */

        /* Key Identifier. */
        PCRTASN1OCTETSTRING pKeyIdentifier = NULL;
        if (Certificate.TbsCertificate.T3.fFlags & RTCRX509TBSCERTIFICATE_F_PRESENT_SUBJECT_KEY_IDENTIFIER)
            pKeyIdentifier = Certificate.TbsCertificate.T3.pSubjectKeyIdentifier;
        else if (   (Certificate.TbsCertificate.T3.fFlags & RTCRX509TBSCERTIFICATE_F_PRESENT_AUTHORITY_KEY_IDENTIFIER)
                 && RTCrX509Certificate_IsSelfSigned(&Certificate)
                 && RTAsn1OctetString_IsPresent(&Certificate.TbsCertificate.T3.pAuthorityKeyIdentifier->KeyIdentifier) )
            pKeyIdentifier = &Certificate.TbsCertificate.T3.pAuthorityKeyIdentifier->KeyIdentifier;
        else if (   (Certificate.TbsCertificate.T3.fFlags & RTCRX509TBSCERTIFICATE_F_PRESENT_OLD_AUTHORITY_KEY_IDENTIFIER)
                 && RTCrX509Certificate_IsSelfSigned(&Certificate)
                 && RTAsn1OctetString_IsPresent(&Certificate.TbsCertificate.T3.pOldAuthorityKeyIdentifier->KeyIdentifier) )
            pKeyIdentifier = &Certificate.TbsCertificate.T3.pOldAuthorityKeyIdentifier->KeyIdentifier;
        if (pKeyIdentifier && pKeyIdentifier->Asn1Core.cb > 0)
        {
            Assert(RTAsn1OctetString_IsPresent(&TrustAnchor.KeyIdentifier));
            RTAsn1OctetString_Delete(&TrustAnchor.KeyIdentifier);
            rc = RTAsn1OctetString_Clone(&TrustAnchor.KeyIdentifier, pKeyIdentifier, &g_RTAsn1DefaultAllocator);
            if (RT_FAILURE(rc))
                RTMsgError("RTAsn1OctetString_Clone failed: %Rrc", rc);
            RTAsn1Core_ResetImplict(RTAsn1OctetString_GetAsn1Core(&TrustAnchor.KeyIdentifier)); /* temporary hack. */
        }
        else
            RTMsgWarning("No key identifier found or has zero length.");

        /* Subject */
        if (RT_SUCCESS(rc))
        {
            Assert(!RTCrTafCertPathControls_IsPresent(&TrustAnchor.CertPath));
            rc = RTCrTafCertPathControls_Init(&TrustAnchor.CertPath, &g_RTAsn1DefaultAllocator);
            if (RT_SUCCESS(rc))
            {
                Assert(RTCrX509Name_IsPresent(&TrustAnchor.CertPath.TaName));
                RTCrX509Name_Delete(&TrustAnchor.CertPath.TaName);
                rc = RTCrX509Name_Clone(&TrustAnchor.CertPath.TaName, &Certificate.TbsCertificate.Subject,
                                        &g_RTAsn1DefaultAllocator);
                if (RT_SUCCESS(rc))
                {
                    RTAsn1Core_ResetImplict(RTCrX509Name_GetAsn1Core(&TrustAnchor.CertPath.TaName)); /* temporary hack. */
                    rc = RTCrX509Name_RecodeAsUtf8(&TrustAnchor.CertPath.TaName, &g_RTAsn1DefaultAllocator);
                    if (RT_FAILURE(rc))
                        RTMsgError("RTCrX509Name_RecodeAsUtf8 failed: %Rrc", rc);
                }
                else
                    RTMsgError("RTCrX509Name_Clone failed: %Rrc", rc);
            }
            else
                RTMsgError("RTCrTafCertPathControls_Init failed: %Rrc", rc);
        }

        /* Check that what we've constructed makes some sense. */
        if (RT_SUCCESS(rc))
        {
            rc = RTCrTafTrustAnchorInfo_CheckSanity(&TrustAnchor, 0, RTErrInfoInitStatic(&StaticErrInfo), "TAI");
            if (RT_FAILURE(rc))
                RTMsgError("RTCrTafTrustAnchorInfo_CheckSanity failed: %Rrc - %s", rc, StaticErrInfo.szMsg);
        }

        if (RT_SUCCESS(rc))
        {
            /*
             * Encode it and write it to the output file.
             */
            uint32_t cbEncoded;
            rc = RTAsn1EncodePrepare(RTCrTafTrustAnchorInfo_GetAsn1Core(&TrustAnchor), RTASN1ENCODE_F_DER, &cbEncoded,
                                     RTErrInfoInitStatic(&StaticErrInfo));
            if (RT_SUCCESS(rc))
            {
                if (State.cVerbose >= 1)
                    RTAsn1Dump(RTCrTafTrustAnchorInfo_GetAsn1Core(&TrustAnchor), 0, 0, RTStrmDumpPrintfV, g_pStdOut);

                PRTSTREAM pStrm;
                rc = RTStrmOpen(State.pszOutput, "wb", &pStrm);
                if (RT_SUCCESS(rc))
                {
                    rc = RTAsn1EncodeWrite(RTCrTafTrustAnchorInfo_GetAsn1Core(&TrustAnchor), RTASN1ENCODE_F_DER,
                                           handleMakeTaInfoWriter, pStrm, RTErrInfoInitStatic(&StaticErrInfo));
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTStrmClose(pStrm);
                        if (RT_SUCCESS(rc))
                            RTMsgInfo("Successfully wrote TrustedAnchorInfo to '%s'.", State.pszOutput);
                        else
                            RTMsgError("RTStrmClose failed: %Rrc", rc);
                    }
                    else
                    {
                        RTMsgError("RTAsn1EncodeWrite failed: %Rrc - %s", rc, StaticErrInfo.szMsg);
                        RTStrmClose(pStrm);
                    }
                }
                else
                    RTMsgError("Error opening '%s' for writing: %Rrcs", State.pszOutput, rc);
            }
            else
                RTMsgError("RTAsn1EncodePrepare failed: %Rrc - %s", rc, StaticErrInfo.szMsg);
        }

        RTCrTafTrustAnchorInfo_Delete(&TrustAnchor);
    }
    else
        RTMsgError("RTCrTafTrustAnchorInfo_Init failed: %Rrc", rc);

    RTCrX509Certificate_Delete(&Certificate);
    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}



/*********************************************************************************************************************************
*   The 'create-self-signed-rsa-cert' command.                                                                                   *
*********************************************************************************************************************************/
#ifndef IPRT_IN_BUILD_TOOL

static RTEXITCODE HelpCreateSelfSignedRsaCert(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT,
                        "create-self-signed-rsa-cert [--verbose|--quiet] [--key-bits <count>] [--digest <hash>] [--out-cert=]<certificate-file.pem> [--out-pkey=]<private-key-file.pem>\n");
    return RTEXITCODE_SUCCESS;
}


static RTDIGESTTYPE DigestTypeStringToValue(const char *pszType)
{
    for (int iType = RTDIGESTTYPE_INVALID + 1; iType < RTDIGESTTYPE_END; iType++)
        if (iType != RTDIGESTTYPE_UNKNOWN)
        {
            const char * const pszName = RTCrDigestTypeToName((RTDIGESTTYPE)iType);
            size_t             offType = 0;
            size_t             offName = 0;
            for (;;)
            {
                char chType = RT_C_TO_UPPER(pszType[offType]);
                char chName = RT_C_TO_UPPER(pszName[offType]);
                if (chType != chName)
                {
                    /* allow 'sha1' as well as 'sha-1' */
                    if (chName != '-')
                        break;
                    chName = pszName[++offName];
                    chName = RT_C_TO_UPPER(chName);
                    if (chType != chName)
                        break;
                }
                if (chType == '\0')
                    return (RTDIGESTTYPE)iType;
            }
        }
    return RTDIGESTTYPE_INVALID;
}


static RTEXITCODE HandleCreateSelfSignedRsaCert(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--digest",           'd', RTGETOPT_REQ_STRING },
        { "--bits",             'b', RTGETOPT_REQ_UINT32 },
        { "--key-bits",         'b', RTGETOPT_REQ_UINT32 },
        { "--days",             'D', RTGETOPT_REQ_UINT32 },
        { "--days",             'D', RTGETOPT_REQ_UINT32 },
        { "--out-cert",         'c', RTGETOPT_REQ_UINT32 },
        { "--out-certificate",  'c', RTGETOPT_REQ_UINT32 },
        { "--out-pkey",         'p', RTGETOPT_REQ_UINT32 },
        { "--out-private-key",  'p', RTGETOPT_REQ_UINT32 },
        { "--secs",             's', RTGETOPT_REQ_UINT32 },
        { "--seconds",          's', RTGETOPT_REQ_UINT32 },
    };

    RTDIGESTTYPE    enmDigestType   = RTDIGESTTYPE_SHA384;
    uint32_t        cKeyBits        = 4096;
    uint32_t        cSecsValidFor   = 365 * RT_SEC_1DAY;
    uint32_t        fKeyUsage       = 0;
    uint32_t        fExtKeyUsage    = 0;
    const char     *pszOutCert      = NULL;
    const char     *pszOutPrivKey   = NULL;

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'b':
                cKeyBits = ValueUnion.u32;
                break;

            case 'd':
                enmDigestType = DigestTypeStringToValue(ValueUnion.psz);
                if (enmDigestType == RTDIGESTTYPE_INVALID)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Unknown digest type: %s", ValueUnion.psz);
                break;

            case 'D':
                cSecsValidFor = ValueUnion.u32 * RT_SEC_1DAY;
                if (cSecsValidFor / RT_SEC_1DAY != ValueUnion.u32)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "The --days option value is out of range: %u", ValueUnion.u32);
                break;

            case 'c':
                if (pszOutCert)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "The --out-cert option can only be used once.");
                pszOutCert = ValueUnion.psz;
                break;

            case 'p':
                if (pszOutPrivKey)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "The --out-pkey option can only be used once.");
                pszOutPrivKey = ValueUnion.psz;
                break;

            case 's':
                cSecsValidFor = ValueUnion.u32;
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (!pszOutCert)
                    pszOutCert = ValueUnion.psz;
                else if (!pszOutPrivKey)
                    pszOutPrivKey = ValueUnion.psz;
                else
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Too many output files specified: %s", ValueUnion.psz);
                break;

            case 'V': return HandleVersion(cArgs, papszArgs);
            case 'h': return HelpCreateSelfSignedRsaCert(g_pStdOut, RTSIGNTOOLHELP_FULL);
            default:  return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (!pszOutCert)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No output certificate file name specified.");
    if (!pszOutPrivKey)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No output private key file name specified.");

    /*
     * Do the work.
     */
    RTERRINFOSTATIC StaticErrInfo;
    rc = RTCrX509Certificate_GenerateSelfSignedRsa(enmDigestType, cKeyBits, cSecsValidFor,
                                                   fKeyUsage, fExtKeyUsage, NULL /*pvSubjectTodo*/,
                                                   pszOutCert, pszOutPrivKey, RTErrInfoInitStatic(&StaticErrInfo));
    if (RT_SUCCESS(rc))
    {
        /*
         * Test load it.
         */
        RTCRX509CERTIFICATE Certificate;
        rc = RTCrX509Certificate_ReadFromFile(&Certificate, pszOutCert, RTCRX509CERT_READ_F_PEM_ONLY,
                                              &g_RTAsn1DefaultAllocator, RTErrInfoInitStatic(&StaticErrInfo));
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error reading the new certificate from %s: %Rrc%#RTeim",
                                  pszOutCert, rc, &StaticErrInfo.Core);
        RTCrX509Certificate_Delete(&Certificate);
        return RTEXITCODE_SUCCESS;
    }

    return RTMsgErrorExitFailure("RTCrX509Certificate_GenerateSelfSignedRsa(%d,%u,%u,%s,%s,) failed: %Rrc%#RTeim",
                                 enmDigestType, cKeyBits, cSecsValidFor, pszOutCert, pszOutPrivKey, rc, &StaticErrInfo.Core);
}

#endif /* !IPRT_IN_BUILD_TOOL */


/*
 * The 'version' command.
 */
static RTEXITCODE HelpVersion(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmPrintf(pStrm, "version\n");
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE HandleVersion(int cArgs, char **papszArgs)
{
    RT_NOREF_PV(cArgs); RT_NOREF_PV(papszArgs);
#ifndef IN_BLD_PROG  /* RTBldCfgVersion or RTBldCfgRevision in build time IPRT lib. */
    RTPrintf("%s\n", RTBldCfgVersion());
    return RTEXITCODE_SUCCESS;
#else
    return RTEXITCODE_FAILURE;
#endif
}



/**
 * Command mapping.
 */
static struct
{
    /** The command. */
    const char *pszCmd;
    /**
     * Handle the command.
     * @returns Program exit code.
     * @param   cArgs       Number of arguments.
     * @param   papszArgs   The argument vector, starting with the command name.
     */
    RTEXITCODE (*pfnHandler)(int cArgs, char **papszArgs);
    /**
     * Produce help.
     * @returns RTEXITCODE_SUCCESS to simplify handling '--help' in the handler.
     * @param   pStrm       Where to send help text.
     * @param   enmLevel    The level of the help information.
     */
    RTEXITCODE (*pfnHelp)(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel);
}
/** Mapping commands to handler and helper functions. */
const g_aCommands[] =
{
    { "extract-exe-signer-cert",        HandleExtractExeSignerCert,         HelpExtractExeSignerCert },
    { "extract-signer-root",            HandleExtractSignerRoot,            HelpExtractSignerRoot },
    { "extract-timestamp-root",         HandleExtractTimestampRoot,         HelpExtractTimestampRoot },
    { "extract-exe-signature",          HandleExtractExeSignature,          HelpExtractExeSignature },
    { "add-nested-exe-signature",       HandleAddNestedExeSignature,        HelpAddNestedExeSignature },
    { "add-nested-cat-signature",       HandleAddNestedCatSignature,        HelpAddNestedCatSignature },
#ifndef IPRT_SIGNTOOL_NO_SIGNING
    { "add-timestamp-exe-signature",    HandleAddTimestampExeSignature,     HelpAddTimestampExeSignature },
    { "sign",                           HandleSign,                         HelpSign },
#endif
#ifndef IPRT_IN_BUILD_TOOL
    { "verify-exe",                     HandleVerifyExe,                    HelpVerifyExe },
#endif
    { "show-exe",                       HandleShowExe,                      HelpShowExe },
    { "show-cat",                       HandleShowCat,                      HelpShowCat },
    { "hash-exe",                       HandleHashExe,                      HelpHashExe },
    { "make-tainfo",                    HandleMakeTaInfo,                   HelpMakeTaInfo },
#ifndef IPRT_IN_BUILD_TOOL
    { "create-self-signed-rsa-cert",    HandleCreateSelfSignedRsaCert,      HelpCreateSelfSignedRsaCert },
#endif
    { "help",                           HandleHelp,                         HelpHelp },
    { "--help",                         HandleHelp,                         NULL },
    { "-h",                             HandleHelp,                         NULL },
    { "version",                        HandleVersion,                      HelpVersion },
    { "--version",                      HandleVersion,                      NULL },
    { "-V",                             HandleVersion,                      NULL },
};


/*
 * The 'help' command.
 */
static RTEXITCODE HelpHelp(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmPrintf(pStrm, "help [cmd-patterns]\n");
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE HandleHelp(int cArgs, char **papszArgs)
{
    PRTSTREAM const pStrm    = g_pStdOut;
    RTSIGNTOOLHELP  enmLevel = cArgs <= 1 ? RTSIGNTOOLHELP_USAGE : RTSIGNTOOLHELP_FULL;
    uint32_t        cShowed  = 0;
    uint32_t        cchWidth;
    if (RT_FAILURE(RTStrmQueryTerminalWidth(g_pStdOut, &cchWidth)))
        cchWidth = 80;

    RTStrmPrintf(pStrm,
                 "Usage: RTSignTool <command> [command-options]\n"
                 "   or: RTSignTool <-V|--version|version>\n"
                 "   or: RTSignTool <-h|--help|help> [command-pattern [..]]\n"
                 "\n"
                 );

    if (enmLevel == RTSIGNTOOLHELP_USAGE)
        RTStrmPrintf(pStrm, "Syntax summary for the RTSignTool commands:\n");

    for (uint32_t iCmd = 0; iCmd < RT_ELEMENTS(g_aCommands); iCmd++)
    {
        if (g_aCommands[iCmd].pfnHelp)
        {
            bool fShow = false;
            if (cArgs <= 1)
                fShow = true;
            else
                for (int iArg = 1; iArg < cArgs; iArg++)
                    if (RTStrSimplePatternMultiMatch(papszArgs[iArg], RTSTR_MAX, g_aCommands[iCmd].pszCmd, RTSTR_MAX, NULL))
                    {
                        fShow = true;
                        break;
                    }
            if (fShow)
            {
                if (enmLevel == RTSIGNTOOLHELP_FULL)
                    RTPrintf("%.*s\n", RT_MIN(cchWidth, 100),
                             "- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - ");
                g_aCommands[iCmd].pfnHelp(pStrm, enmLevel);
                cShowed++;
            }
        }
    }
    return cShowed ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}



int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Parse global arguments.
     */
    int iArg = 1;
    /* none presently. */

    /*
     * Command dispatcher.
     */
    if (iArg < argc)
    {
        const char *pszCmd = argv[iArg];
        uint32_t i = RT_ELEMENTS(g_aCommands);
        while (i-- > 0)
            if (!strcmp(g_aCommands[i].pszCmd, pszCmd))
                return g_aCommands[i].pfnHandler(argc - iArg, &argv[iArg]);
        RTMsgError("Unknown command '%s'.", pszCmd);
    }
    else
        RTMsgError("No command given. (try --help)");

    return RTEXITCODE_SYNTAX;
}

