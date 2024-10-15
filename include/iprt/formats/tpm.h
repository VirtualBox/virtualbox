/* $Id: tpm.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT, TPM common definitions (this is actually a protocol and not a format).
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_formats_tpm_h
#define IPRT_INCLUDED_formats_tpm_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/asm.h>
#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/assertcompile.h>
#include <iprt/string.h>


/** A TPM generic handle (TPM_HANDLE). */
typedef uint32_t TPMHANDLE;
/** TPM interface object handle. */
typedef TPMHANDLE TPMIDHOBJECT;

/** A TPM boolean value (TPMI_YES_NO). */
typedef uint8_t  TPMYESNO;
/** A No aka False value for TPMYESNO. */
#define TPMYESNO_NO         0
/** A Yes aka True value for TPMYESNO. */
#define TPMYESNO_YES        1

/** A TPM capability value (TPM_CAP). */
typedef uint32_t TPMCAP;


/**
 * TPM sized buffer.
 */
#pragma pack(1)
typedef struct TPMBUF
{
    /** Size of the buffer in bytes - can be 0. */
    uint16_t            u16Size;
    /** Buffer area. */
    uint8_t             abBuf[RT_FLEXIBLE_ARRAY_NESTED];
} TPMBUF;
#pragma pack()
/** Pointer to a TPM buffer. */
typedef TPMBUF *PTPMBUF;
/** Pointer to a const TPM buffer. */
typedef const TPMBUF *PCTPMBUF;



/**
 * TPM request header (everything big endian).
 */
#pragma pack(1)
typedef struct TPMREQHDR
{
    /** The tag for this request. */
    uint16_t            u16Tag;
    /** Size of the request in bytes. */
    uint32_t            cbReq;
    /** The request ordinal to execute. */
    uint32_t            u32Ordinal;
} TPMREQHDR;
#pragma pack()
AssertCompileSize(TPMREQHDR, 2 + 4 + 4);
/** Pointer to a TPM request header. */
typedef TPMREQHDR *PTPMREQHDR;
/** Pointer to a const TPM request header. */
typedef const TPMREQHDR *PCTPMREQHDR;


/**
 * TPM response header (everything big endian).
 */
#pragma pack(1)
typedef struct TPMRESPHDR
{
    /** The tag for this request. */
    uint16_t            u16Tag;
    /** Size of the response in bytes. */
    uint32_t            cbResp;
    /** The error code for the response. */
    uint32_t            u32ErrCode;
} TPMRESPHDR;
#pragma pack()
AssertCompileSize(TPMRESPHDR, 2 + 4 + 4);
/** Pointer to a TPM response header. */
typedef TPMRESPHDR *PTPMRESPHDR;
/** Pointer to a const TPM response header. */
typedef const TPMRESPHDR *PCTPMRESPHDR;


/** @name TPM 1.2 request tags
 * @{ */
/** Command with no authentication. */
#define TPM_TAG_RQU_COMMAND                 UINT16_C(0x00c1)
/** An authenticated command with one authentication handle. */
#define TPM_TAG_RQU_AUTH1_COMMAND           UINT16_C(0x00c2)
/** An authenticated command with two authentication handles. */
#define TPM_TAG_RQU_AUTH2_COMMAND           UINT16_C(0x00c3)
/** @} */


/** @name TPM 2.0 request/response tags
 * @{ */
/** Command with no associated session. */
#define TPM2_ST_NO_SESSIONS                 UINT16_C(0x8001)
/** Command with an associated session. */
#define TPM2_ST_SESSIONS                    UINT16_C(0x8002)
/** @} */


/** @name TPM 1.2 request ordinals.
 * @{ */
/** Perform a full self test. */
#define TPM_ORD_SELFTESTFULL                UINT32_C(80)
/** Continue the selftest. */
#define TPM_ORD_CONTINUESELFTEST            UINT32_C(83)
/** Return the test result. */
#define TPM_ORD_GETTESTRESULT               UINT32_C(84)
/** Get a capability. */
#define TPM_ORD_GETCAPABILITY               UINT32_C(101)
/** @} */


/** @name TPM 2.0 Algorithm ID codes.
 * @{ */
/** Invalid algorithm ID - should not occur. */
#define TPM2_ALG_ERROR                      UINT16_C(0x0000)
/** RSA algorithm ID. */
#define TPM2_ALG_RSA                        UINT16_C(0x0001)
/** TDES (Triple Data Encryption Standard) algorithm ID. */
#define TPM2_ALG_TDES                       UINT16_C(0x0003)
/** SHA1 algorithm ID. */
#define TPM2_ALG_SHA1                       UINT16_C(0x0004)
/** HMAC (Hash Message Authentication Code) algorithm ID. */
#define TPM2_ALG_HMAC                       UINT16_C(0x0005)
/** AES algorithm ID. */
#define TPM2_ALG_AES                        UINT16_C(0x0006)
/** Hash-based mask-generation function algorithm ID. */
#define TPM2_ALG_MGF1                       UINT16_C(0x0007)
/** Object type that may use XOR for encryption or an HMAC for signing. */
#define TPM2_ALG_KEYEDHASH                  UINT16_C(0x0008)
/** XOR algorithm ID. */
#define TPM2_ALG_XOR                        UINT16_C(0x000a)
/** SHA256 algorithm ID. */
#define TPM2_ALG_SHA256                     UINT16_C(0x000b)
/** SHA384 algorithm ID. */
#define TPM2_ALG_SHA384                     UINT16_C(0x000c)
/** SHA512 algorithm ID. */
#define TPM2_ALG_SHA512                     UINT16_C(0x000d)
/** SHA256 with only 192 most significant bits algorithm ID. */
#define TPM2_ALG_SHA256_192                 UINT16_C(0x000e)
/** Null algorithm ID. */
#define TPM2_ALG_NULL                       UINT16_C(0x0010)
/** SM3 hash algorithm ID. */
#define TPM2_ALG_SM3_256                    UINT16_C(0x0012)
/** SM4 symmetric block cipher algorithm ID. */
#define TPM2_ALG_SM4                        UINT16_C(0x0013)
/** RSASSA-PKCS1-v1_5 signature algorithm ID. */
#define TPM2_ALG_RSASSA                     UINT16_C(0x0014)
/** RSAES-PKCS1-v1_5 padding algorithm ID. */
#define TPM2_ALG_RSAES                      UINT16_C(0x0015)
/** RSASSA-PSS signature algorithm ID. */
#define TPM2_ALG_RSAPSS                     UINT16_C(0x0016)
/** RSAES_OAEP padding algorithm ID. */
#define TPM2_ALG_OAEP                       UINT16_C(0x0017)
/** Elliptic curve cryptography signature algorithm ID. */
#define TPM2_ALG_ECDSA                      UINT16_C(0x0018)
/** Secret sharing using ECC algorithm ID. */
#define TPM2_ALG_ECDH                       UINT16_C(0x0019)
/** Elliptic curve based anonymous signing scheme algorithm ID. */
#define TPM2_ALG_ECDAA                      UINT16_C(0x001a)
/** SM2 algorithm ID. */
#define TPM2_ALG_SM2                        UINT16_C(0x001b)
/** Elliptic-curve based Schnorr signature algorithm ID. */
#define TPM2_ALG_ECSCHNORR                  UINT16_C(0x001c)
/** Two phase elliptic curve key exchange algorithm ID. */
#define TPM2_ALG_ECMQV                      UINT16_C(0x001d)
/** NIST SP800-56A Concatenation key derivation function algorithm ID. */
#define TPM2_ALG_KDF1_SP800_56A             UINT16_C(0x0020)
/** Key derivation function KDF2 algorithm ID. */
#define TPM2_ALG_KDF2                       UINT16_C(0x0021)
/** NIST SP800-108 key derivation function algorithm ID. */
#define TPM2_ALG_KDF1_SP800_108             UINT16_C(0x0022)
/** Prime field ECC algorithm ID. */
#define TPM2_ALG_ECC                        UINT16_C(0x0023)
/** Object type for a symmetric block cipher algorithm ID. */
#define TPM2_ALG_SYMCIPHER                  UINT16_C(0x0025)
/** Camellia symmetric block cipher algorithm ID. */
#define TPM2_ALG_CAMELLIA                   UINT16_C(0x0026)
/** SHA3 hash algorithm ID - produces 256-bit digest. */
#define TPM2_ALG_SHA3_256                   UINT16_C(0x0027)
/** SHA3 hash algorithm ID - produces 384-bit digest. */
#define TPM2_ALG_SHA3_384                   UINT16_C(0x0028)
/** SHA3 hash algorithm ID - produces 512-bit digest. */
#define TPM2_ALG_SHA3_512                   UINT16_C(0x0029)
/** ISO/IEC 10118-3 extendable output function algorithm ID - provides 128-bits of collision and preimage resistance. */
#define TPM2_ALG_SHAKE128                   UINT16_C(0x002a)
/** ISO/IEC 10118-3 extendable output function algorithm ID - provides 256-bits of collision and preimage resistance. */
#define TPM2_ALG_SHAKE256                   UINT16_C(0x002b)
/** ISO/IEC 10118-3 extendable output function algorithm ID - the first 192 bits of SHAKE256 output. */
#define TPM2_ALG_SHAKE256_192               UINT16_C(0x002c)
/** ISO/IEC 10118-3 extendable output function algorithm ID - the first 256 bits of SHAKE256 output. */
#define TPM2_ALG_SHAKE256_256               UINT16_C(0x002d)
/** ISO/IEC 10118-3 extendable output function algorithm ID - the first 512 bits of SHAKE256 output. */
#define TPM2_ALG_SHAKE256_512               UINT16_C(0x002e)
/** ISO/IEC 9797-1:2011 Block Cipher based Message Authentication Code algorithm ID. */
#define TPM2_ALG_CMAC                       UINT16_C(0x003f)
/** ISO/IEC 10116 Counter mode for symmetric block ciphers algorithm ID. */
#define TPM2_ALG_CTR                        UINT16_C(0x0040)
/** ISO/IEC 10116 Output feedback mode for symmetric block ciphers algorithm ID. */
#define TPM2_ALG_OFB                        UINT16_C(0x0041)
/** ISO/IEC 10116 Cipher Block Chaining mode for symmetric block ciphers algorithm ID. */
#define TPM2_ALG_CBC                        UINT16_C(0x0042)
/** ISO/IEC 10116 Cipher Feedback mode for symmetric block ciphers algorithm ID. */
#define TPM2_ALG_CFB                        UINT16_C(0x0043)
/** ISO/IEC 10116 Electronic codebook mode for symmetric block ciphers algorithm ID. */
#define TPM2_ALG_ECB                        UINT16_C(0x0044)
/** NIST SP800-38C Counter with Cipher Block Chaining Message Authentication Code algorithm ID. */
#define TPM2_ALG_CCM                        UINT16_C(0x0050)
/** NIST SP800-38D Galois/Counter Mode algorithm ID. */
#define TPM2_ALG_GCM                        UINT16_C(0x0051)
/** NIST SP800-38F AES Key Wrap (KW) algorithm ID. */
#define TPM2_ALG_KW                         UINT16_C(0x0052)
/** NIST SP800-38F AES Key Wrap with Padding (KWP) algorithm ID. */
#define TPM2_ALG_KWP                        UINT16_C(0x0053)
/** ISO/IEC 19772 Authentication Encryption Mode algorithm ID. */
#define TPM2_ALG_EAX                        UINT16_C(0x0054)
/** IETF RFC 8083 Edwards curve Digital Signature Algorithm (PureEdDSA) algorithm ID. */
#define TPM2_ALG_EDDSA                      UINT16_C(0x0060)
/** IETF RFC 8082 Edwards curve Digital Signature Algorithm (HashEdDSA) algorithm ID. */
#define TPM2_ALG_EDDSA_PH                   UINT16_C(0x0061)
/** NIST SP800-208 Leighton-Micali Signatures algorithm ID. */
#define TPM2_ALG_LMS                        UINT16_C(0x0070)
/** NIST SP800-208 eXtended Merkle Signature Scheme algorithm ID. */
#define TPM2_ALG_XMSS                       UINT16_C(0x0071)
/** Keyed XOF algorithm ID. */
#define TPM2_ALG_KEYEDXOF                   UINT16_C(0x0080)
/** NIST SP800-185 Keyed XOF providing 128-bit security strength algorithm ID. */
#define TPM2_ALG_KMACXOF128                 UINT16_C(0x0081)
/** NIST SP800-185 Keyed XOF providing 256-bit security strength algorithm ID. */
#define TPM2_ALG_KMACXOF256                 UINT16_C(0x0082)
/** NIST SP800-185 Variable length MAC providing 128-bit security strength algorithm ID. */
#define TPM2_ALG_KMAC128                    UINT16_C(0x0090)
/** NIST SP800-185 Variable length MAC providing 256-bit security strength algorithm ID. */
#define TPM2_ALG_KMAC256                    UINT16_C(0x0091)
/** @} */


/** @name TPM 2.0 ECC Curve codes.
 * @{ */
#define TPM2_ECC_NONE                       UINT16_C(0x0000)
#define TPM2_ECC_NIST_P192                  UINT16_C(0x0001)
#define TPM2_ECC_NIST_P224                  UINT16_C(0x0002)
#define TPM2_ECC_NIST_P256                  UINT16_C(0x0003)
#define TPM2_ECC_NIST_P384                  UINT16_C(0x0004)
#define TPM2_ECC_NIST_P521                  UINT16_C(0x0005)
#define TPM2_ECC_BN_P256                    UINT16_C(0x0010)
#define TPM2_ECC_BN_P638                    UINT16_C(0x0011)
#define TPM2_ECC_SM2_P256                   UINT16_C(0x0020)
#define TPM2_ECC_BP_P256_R1                 UINT16_C(0x0030)
#define TPM2_ECC_BP_P384_R1                 UINT16_C(0x0031)
#define TPM2_ECC_BP_P512_R1                 UINT16_C(0x0032)
#define TPM2_ECC_CURVE_25519                UINT16_C(0x0040)
#define TPM2_ECC_CURVE_448                  UINT16_C(0x0041)
/** @} */


/** @name TPM 2.0 command codes.
 * @{ */
#define TPM2_CC_NV_UNDEFINE_SPACE_SPECIAL       UINT32_C(0x11f)
#define TPM2_CC_EVICT_CONTROL                   UINT32_C(0x120)
#define TPM2_CC_HIERARCHY_CONTROL               UINT32_C(0x121)
#define TPM2_CC_NV_UNDEFINE_SPACE               UINT32_C(0x122)
#define TPM2_CC_CHANGE_EPS                      UINT32_C(0x124)
#define TPM2_CC_CHANGE_PPS                      UINT32_C(0x125)
#define TPM2_CC_CLEAR                           UINT32_C(0x126)
#define TPM2_CC_CLEAR_CONTROL                   UINT32_C(0x127)
#define TPM2_CC_CLOCK_SET                       UINT32_C(0x128)
#define TPM2_CC_HIERARCHY_CHANGE_AUTH           UINT32_C(0x129)
#define TPM2_CC_NV_DEFINE_SPACE                 UINT32_C(0x12a)
#define TPM2_CC_PCR_ALLOCATE                    UINT32_C(0x12b)
#define TPM2_CC_PCR_SET_AUTH_POLICY             UINT32_C(0x12c)
#define TPM2_CC_PP_COMMANDS                     UINT32_C(0x12d)
#define TPM2_CC_SET_PRIMARY_POLICY              UINT32_C(0x12e)
#define TPM2_CC_FIELD_UPGRADE_START             UINT32_C(0x12f)
#define TPM2_CC_CLOCK_RATE_ADJUST               UINT32_C(0x130)
#define TPM2_CC_CREATE_PRIMARY                  UINT32_C(0x131)
#define TPM2_CC_NV_GLOBAL_WRITE_LOCK            UINT32_C(0x132)
#define TPM2_CC_GET_COMMAND_AUDIT_DIGEST        UINT32_C(0x133)
#define TPM2_CC_NV_INCREMENT                    UINT32_C(0x134)
#define TPM2_CC_NV_SET_BITS                     UINT32_C(0x135)
#define TPM2_CC_NV_EXTEND                       UINT32_C(0x136)
#define TPM2_CC_NV_WRITE                        UINT32_C(0x137)
#define TPM2_CC_NV_WRITE_LOCK                   UINT32_C(0x138)
#define TPM2_CC_DICTIONARY_ATTACK_LOCK_RESET    UINT32_C(0x139)
#define TPM2_CC_DICTIONARY_ATTACK_PARAMETERS    UINT32_C(0x13a)
#define TPM2_CC_NV_CHANGE_AUTH                  UINT32_C(0x13b)
#define TPM2_CC_PCR_EVENT                       UINT32_C(0x13c)
#define TPM2_CC_PCR_RESET                       UINT32_C(0x13d)
#define TPM2_CC_SEQUENCE_COMPLETE               UINT32_C(0x13e)
#define TPM2_CC_SET_ALGORITHM_SET               UINT32_C(0x13f)
#define TPM2_CC_SET_COMMAND_CODE_AUDIT_STATUS   UINT32_C(0x140)
#define TPM2_CC_FIELD_UPGRADE_DATA              UINT32_C(0x141)
#define TPM2_CC_INCREMENTAL_SELF_TEST           UINT32_C(0x142)
#define TPM2_CC_SELF_TEST                       UINT32_C(0x143)
#define TPM2_CC_STARTUP                         UINT32_C(0x144)
#define TPM2_CC_SHUTDOWN                        UINT32_C(0x145)
#define TPM2_CC_STIR_RANDOM                     UINT32_C(0x146)
#define TPM2_CC_ACTIVATE_CREDENTIAL             UINT32_C(0x147)
#define TPM2_CC_CERTIFY                         UINT32_C(0x148)
#define TPM2_CC_POLICY_NV                       UINT32_C(0x149)
#define TPM2_CC_CERTIFY_CREATION                UINT32_C(0x14a)
#define TPM2_CC_DUPLICATE                       UINT32_C(0x14b)
#define TPM2_CC_GET_TIME                        UINT32_C(0x14c)
#define TPM2_CC_GET_SESSION_AUDIT_DIGEST        UINT32_C(0x14d)
#define TPM2_CC_NV_READ                         UINT32_C(0x14e)
#define TPM2_CC_NV_READ_LOCK                    UINT32_C(0x14f)
#define TPM2_CC_OBJECT_CHANGE_AUTH              UINT32_C(0x150)
#define TPM2_CC_POLICY_SECRET                   UINT32_C(0x151)
#define TPM2_CC_REWRAP                          UINT32_C(0x152)
#define TPM2_CC_CREATE                          UINT32_C(0x153)
#define TPM2_CC_ECDH_ZGEN                       UINT32_C(0x154)
#define TPM2_CC_HMAC_MAC                        UINT32_C(0x155)
#define TPM2_CC_IMPORT                          UINT32_C(0x156)
#define TPM2_CC_LOAD                            UINT32_C(0x157)
#define TPM2_CC_QUOTE                           UINT32_C(0x158)
#define TPM2_CC_RSA_DECRYPT                     UINT32_C(0x159)
#define TPM2_CC_HMAC_MAC_START                  UINT32_C(0x15b)
#define TPM2_CC_SEQUENCE_UPDATE                 UINT32_C(0x15c)
#define TPM2_CC_SIGN                            UINT32_C(0x15d)
#define TPM2_CC_UNSEAL                          UINT32_C(0x15e)
#define TPM2_CC_POLICY_SIGNED                   UINT32_C(0x160)
#define TPM2_CC_CONTEXT_LOAD                    UINT32_C(0x161)
#define TPM2_CC_CONTEXT_SAVE                    UINT32_C(0x162)
#define TPM2_CC_ECDH_KEY_GEN                    UINT32_C(0x163)
#define TPM2_CC_ENCRYPT_DECRYPT                 UINT32_C(0x164)
#define TPM2_CC_FLUSH_CONTEXT                   UINT32_C(0x165)
#define TPM2_CC_LOAD_EXTERNAL                   UINT32_C(0x167)
#define TPM2_CC_MAKE_CREDENTIAL                 UINT32_C(0x168)
#define TPM2_CC_NV_READ_PUBLIC                  UINT32_C(0x169)
#define TPM2_CC_POLICY_AUTHORIZE                UINT32_C(0x16a)
#define TPM2_CC_POLICY_AUTH_VALUE               UINT32_C(0x16b)
#define TPM2_CC_POLICY_COMMAND_CODE             UINT32_C(0x16c)
#define TPM2_CC_POLICY_COUNTER_TIMER            UINT32_C(0x16d)
#define TPM2_CC_POLICY_CP_HASH                  UINT32_C(0x16e)
#define TPM2_CC_POLICY_LOCALITY                 UINT32_C(0x16f)
#define TPM2_CC_POLICY_NAME_HASH                UINT32_C(0x170)
#define TPM2_CC_POLICY_OR                       UINT32_C(0x171)
#define TPM2_CC_POLICY_TICKET                   UINT32_C(0x172)
#define TPM2_CC_READ_PUBLIC                     UINT32_C(0x173)
#define TPM2_CC_RSA_ENCRYPT                     UINT32_C(0x174)
#define TPM2_CC_START_AUTH_SESSION              UINT32_C(0x176)
#define TPM2_CC_VERIFY_SIGNATURE                UINT32_C(0x177)
#define TPM2_CC_ECC_PARAMETERS                  UINT32_C(0x178)
#define TPM2_CC_FIRMWARE_READ                   UINT32_C(0x179)
#define TPM2_CC_GET_CAPABILITY                  UINT32_C(0x17a)
#define TPM2_CC_GET_RANDOM                      UINT32_C(0x17b)
#define TPM2_CC_GET_TEST_RESULT                 UINT32_C(0x17c)
#define TPM2_CC_GET_HASH                        UINT32_C(0x17d)
#define TPM2_CC_PCR_READ                        UINT32_C(0x17e)
#define TPM2_CC_POLICY_PCR                      UINT32_C(0x17f)
#define TPM2_CC_POLICY_RESTART                  UINT32_C(0x180)
#define TPM2_CC_READ_CLOCK                      UINT32_C(0x181)
#define TPM2_CC_PCR_EXTEND                      UINT32_C(0x182)
#define TPM2_CC_PCR_SET_AUTH_VALUE              UINT32_C(0x183)
#define TPM2_CC_NV_CERTIFY                      UINT32_C(0x184)
#define TPM2_CC_EVENT_SEQUENCE_COMPLETE         UINT32_C(0x185)
#define TPM2_CC_HASH_SEQUENCE_START             UINT32_C(0x186)
#define TPM2_CC_POLICY_PHYSICAL_PRESENCE        UINT32_C(0x187)
#define TPM2_CC_POLICY_DUPLICATION_SELECT       UINT32_C(0x188)
#define TPM2_CC_POLICY_GET_DIGEST               UINT32_C(0x189)
#define TPM2_CC_TEST_PARMS                      UINT32_C(0x18a)
#define TPM2_CC_COMMIT                          UINT32_C(0x18b)
#define TPM2_CC_POLICY_PASSWORD                 UINT32_C(0x18c)
#define TPM2_CC_ZGEN_2PHASE                     UINT32_C(0x18d)
#define TPM2_CC_EC_EPHEMERAL                    UINT32_C(0x18e)
#define TPM2_CC_POLICY_NV_WRITTEN               UINT32_C(0x18f)
#define TPM2_CC_POLICY_TEMPLATE                 UINT32_C(0x190)
#define TPM2_CC_CREATE_LOADED                   UINT32_C(0x191)
#define TPM2_CC_POLICY_AUTHORIZE_NV             UINT32_C(0x192)
#define TPM2_CC_ENCRYPT_DECRYPT_2               UINT32_C(0x193)
#define TPM2_CC_AC_GET_CAPABILITY               UINT32_C(0x194)
#define TPM2_CC_AC_SEND                         UINT32_C(0x195)
#define TPM2_CC_POLICY_AC_SEND_SELECT           UINT32_C(0x196)
#define TPM2_CC_CERTIFY_X509                    UINT32_C(0x197)
#define TPM2_CC_ACT_SET_TIMEOUT                 UINT32_C(0x198)
#define TPM2_CC_ECC_ENCRYPT                     UINT32_C(0x199)
#define TPM2_CC_ECC_DECRYPT                     UINT32_C(0x19a)
#define TPM2_CC_POLICY_CAPABILITY               UINT32_C(0x19b)
#define TPM2_CC_POLICY_PARAMETERS               UINT32_C(0x19c)
#define TPM2_CC_NV_DEFINE_SPACE_2               UINT32_C(0x19d)
#define TPM2_CC_NV_READ_PUBLIC_2                UINT32_C(0x19e)
#define TPM2_CC_SET_CAPABILITY                  UINT32_C(0x19f)
/** @} */


/** @name Defines related to TPM_ORD_GETCAPABILITY.
 * @{ */
/** Return a TPM related property. */
#define TPM_CAP_PROPERTY                    UINT32_C(5)

/** Returns the size of the input buffer. */
#define TPM_CAP_PROP_INPUT_BUFFER           UINT32_C(0x124)

/**
 * TPM_ORD_GETCAPABILITY request.
 */
#pragma pack(1)
typedef struct TPMREQGETCAPABILITY
{
    /** Request header. */
    TPMREQHDR                   Hdr;
    /** The capability group to query. */
    uint32_t                    u32Cap;
    /** Length of the capability. */
    uint32_t                    u32Length;
    /** The sub capability to query. */
    uint32_t                    u32SubCap;
} TPMREQGETCAPABILITY;
#pragma pack()
/** Pointer to a TPM_ORD_GETCAPABILITY request. */
typedef TPMREQGETCAPABILITY *PTPMREQGETCAPABILITY;
/** Pointer to a const TPM_ORD_GETCAPABILITY request. */
typedef const TPMREQGETCAPABILITY *PCTPMREQGETCAPABILITY;
/** @} */


/** @name Defines related to TPM2_CC_STARTUP
 * @{ */
#define TPM2_SU_CLEAR                       UINT16_C(0x0000)
#define TPM2_SU_STATE                       UINT16_C(0x0001)
/** @} */

/** @name Defines related to TPM2_CC_GET_CAPABILITY.
 * @{ */
#define TPM2_CAP_ALGS                       UINT32_C(0x00000000)
#define TPM2_CAP_HANDLES                    UINT32_C(0x00000001)
#define TPM2_CAP_COMMANDS                   UINT32_C(0x00000002)
#define TPM2_CAP_PP_COMMANDS                UINT32_C(0x00000003)
#define TPM2_CAP_AUDIT_COMMANDS             UINT32_C(0x00000004)
#define TPM2_CAP_PCRS                       UINT32_C(0x00000005)
/** Return a TPM related property. */
#define TPM2_CAP_TPM_PROPERTIES             UINT32_C(0x00000006)
#define TPM2_CAP_PCR_PROPERTIES             UINT32_C(0x00000007)
#define TPM2_CAP_ECC_CURVES                 UINT32_C(0x00000008)
#define TPM2_CAP_AUTH_POLICIES              UINT32_C(0x00000009)
#define TPM2_CAP_ACT                        UINT32_C(0x0000000a)


#define TPM2_PT_FAMILY_INDICATOR            UINT32_C(0x00000100)
#define TPM2_PT_LEVEL                       UINT32_C(0x00000101)
#define TPM2_PT_REVISION                    UINT32_C(0x00000102)
#define TPM2_PT_DAY_OF_YEAR                 UINT32_C(0x00000103)
#define TPM2_PT_YEAR                        UINT32_C(0x00000104)
#define TPM2_PT_MANUFACTURER                UINT32_C(0x00000105)
#define TPM2_PT_VENDOR_STRING_1             UINT32_C(0x00000106)
#define TPM2_PT_VENDOR_STRING_2             UINT32_C(0x00000107)
#define TPM2_PT_VENDOR_STRING_3             UINT32_C(0x00000108)
#define TPM2_PT_VENDOR_STRING_4             UINT32_C(0x00000109)
#define TPM2_PT_VENDOR_TPM_TYPE             UINT32_C(0x0000010a)
#define TPM2_PT_FIRMWARE_VERSION_1          UINT32_C(0x0000010b)
#define TPM2_PT_FIRMWARE_VERSION_2          UINT32_C(0x0000010c)
/** Returns the size of the input buffer. */
#define TPM2_PT_INPUT_BUFFER                UINT32_C(0x0000010d)
#define TPM2_PT_HR_TRANSIENT_MIN            UINT32_C(0x0000010e)
#define TPM2_PT_HR_PERSISTENT_MIN           UINT32_C(0x0000010f)
#define TPM2_PT_HR_LOADED_MIN               UINT32_C(0x00000110)
#define TPM2_PT_ACTIVE_SESSIONS_MAX         UINT32_C(0x00000111)
#define TPM2_PT_PCR_COUNT                   UINT32_C(0x00000112)
#define TPM2_PT_PCR_SELECT_MIN              UINT32_C(0x00000113)
#define TPM2_PT_CONTEXT_GAP_MAX             UINT32_C(0x00000114)
#define TPM2_PT_RESERVED                    UINT32_C(0x00000115)
#define TPM2_PT_NV_COUNTERS_MAX             UINT32_C(0x00000116)
#define TPM2_PT_NV_INDEX                    UINT32_C(0x00000117)
#define TPM2_PT_MEMORY                      UINT32_C(0x00000118)
#define TPM2_PT_CLOCK_UPDATE                UINT32_C(0x00000119)
#define TPM2_PT_CONTEXT_HASH                UINT32_C(0x0000011a)
#define TPM2_PT_CONTEXT_SYM                 UINT32_C(0x0000011b)
#define TPM2_PT_CONTEXT_SYM_SIZE            UINT32_C(0x0000011c)
#define TPM2_PT_ORDERLY_COUNT               UINT32_C(0x0000011d)
#define TPM2_PT_MAX_COMMAND_SIZE            UINT32_C(0x0000011e)
#define TPM2_PT_MAX_RESPONSE_SIZE           UINT32_C(0x0000011f)
#define TPM2_PT_MAX_DIGEST                  UINT32_C(0x00000120)
#define TPM2_PT_MAX_OBJECT_CONTEXT          UINT32_C(0x00000121)
#define TPM2_PT_MAX_SESSION_CONTEXT         UINT32_C(0x00000122)
#define TPM2_PT_PS_FAMILY_INDICATOR         UINT32_C(0x00000123)
#define TPM2_PT_PS_LEVEL                    UINT32_C(0x00000124)
#define TPM2_PT_PS_REVISION                 UINT32_C(0x00000125)
#define TPM2_PT_PS_DAY_OF_YEAR              UINT32_C(0x00000126)
#define TPM2_PT_PS_YEAR                     UINT32_C(0x00000127)
#define TPM2_PT_SPLIT_MAX                   UINT32_C(0x00000128)
#define TPM2_PT_TOTAL_COMMANDS              UINT32_C(0x00000129)
#define TPM2_PT_LIBRARY_COMMANDS            UINT32_C(0x0000012a)
#define TPM2_PT_VENDOR_COMMANDS             UINT32_C(0x0000012b)
#define TPM2_PT_NV_BUFFER_MAX               UINT32_C(0x0000012c)
#define TPM2_PT_MODES                       UINT32_C(0x0000012d)
#define TPM2_PT_MAX_CAP_BUFFER              UINT32_C(0x0000012e)
#define TPM2_PT_FIRMWARE_SVN                UINT32_C(0x0000012f)
#define TPM2_PT_FIRMWARE_MAX_SVN            UINT32_C(0x00000130)


/**
 * TPM2_CC_GET_CAPABILITY request.
 */
#pragma pack(1)
typedef struct TPM2REQGETCAPABILITY
{
    /** Request header. */
    TPMREQHDR                   Hdr;
    /** The capability group to query. */
    uint32_t                    u32Cap;
    /** Property to query. */
    uint32_t                    u32Property;
    /** Number of values to return. */
    uint32_t                    u32Count;
} TPM2REQGETCAPABILITY;
#pragma pack()
/** Pointer to a TPM2_CC_GET_CAPABILITY request. */
typedef TPM2REQGETCAPABILITY *PTPM2REQGETCAPABILITY;
/** Pointer to a const TPM2_CC_GET_CAPABILITY request. */
typedef const TPM2REQGETCAPABILITY *PCTPM2REQGETCAPABILITY;

/**
 * TPM2_CC_GET_CAPABILITY response.
 */
#pragma pack(1)
typedef struct TPM2RESPGETCAPABILITY
{
    /** Request header. */
    TPMREQHDR                   Hdr;
    /** The capability group to query. */
    TPMYESNO                    fMoreData;
    /** The capability being returned (part of TPMS_CAPABILITY_DATA). */
    TPMCAP                      u32Cap;
    /** Capability data. */
    uint8_t                     abCap[RT_FLEXIBLE_ARRAY_NESTED];
} TPM2RESPGETCAPABILITY;
#pragma pack()
/** Pointer to a TPM2_CC_GET_CAPABILITY request. */
typedef TPM2RESPGETCAPABILITY *PTPM2RESPGETCAPABILITY;
/** Pointer to a const TPM2_CC_GET_CAPABILITY request. */
typedef const TPM2RESPGETCAPABILITY *PCTPM2RESPGETCAPABILITY;
/** @} */


/** @name Defines related to TPM2_CC_READ_PUBLIC.
 * @{ */
/**
 * TPM2_CC_READ_PUBLIC request.
 */
#pragma pack(1)
typedef struct TPM2REQREADPUBLIC
{
    /** Request header. */
    TPMREQHDR                   Hdr;
    /** The object handle to query. */
    TPMIDHOBJECT                hObj;
} TPM2REQREADPUBLIC;
#pragma pack()
/** Pointer to a TPM2_CC_READ_PUBLIC request. */
typedef TPM2REQREADPUBLIC *PTPM2REQREADPUBLIC;
/** Pointer to a const TPM2_CC_READ_PUBLIC request. */
typedef const TPM2REQREADPUBLIC *PCTPM2REQREADPUBLIC;
/** @} */


/** @name Defines related to TPM2_CC_GET_RANDOM.
 * @{ */
/**
 * TPM2_CC_GET_RANDOM request.
 */
#pragma pack(1)
typedef struct TPM2REQGETRANDOM
{
    /** Request header. */
    TPMREQHDR                   Hdr;
    /** The number of random bytes requested. */
    uint16_t                    u16RandomBytes;
} TPM2REQGETRANDOM;
#pragma pack()
/** Pointer to a TPM2_CC_GET_RANDOM request. */
typedef TPM2REQGETRANDOM *PTPM2REQGETRANDOM;
/** Pointer to a const TPM2_CC_GET_RANDOM request. */
typedef const TPM2REQGETRANDOM *PCTPM2REQGETRANDOM;

/**
 * TPM2_CC_GET_RANDOM response.
 */
#pragma pack(1)
typedef struct TPM2RESPGETRANDOM
{
    /** Request header. */
    TPMRESPHDR                  Hdr;
    /** The buffer holding the response data. */
    TPMBUF                      Buf;
} TPM2RESPGETRANDOM;
#pragma pack()
/** Pointer to a TPM2_CC_GET_RANDOM response. */
typedef TPM2RESPGETRANDOM *PTPM2RESPGETRANDOM;
/** Pointer to a const TPM2_CC_GET_RANDOM response. */
typedef const TPM2RESPGETRANDOM *PCTPM2RESPGETRANDOM;
/** @} */


/** @name TPM 1.2 response tags
 * @{ */
/** A response from a command with no authentication. */
#define TPM_TAG_RSP_COMMAND                 UINT16_C(0x00c4)
/** An authenticated response with one authentication handle. */
#define TPM_TAG_RSP_AUTH1_COMMAND           UINT16_C(0x00c5)
/** An authenticated response with two authentication handles. */
#define TPM_TAG_RSP_AUTH2_COMMAND           UINT16_C(0x00c6)
/** @} */


/** @name TPM status codes.
 * @{ */
#ifndef TPM_SUCCESS
/** Request executed successfully. */
# define TPM_SUCCESS                        UINT32_C(0)
#endif
#ifndef TPM_AUTHFAIL
/** Authentication failed. */
# define TPM_AUTHFAIL                       UINT32_C(1)
#endif
#ifndef TPM_BADINDEX
/** An index is malformed. */
# define TPM_BADINDEX                       UINT32_C(2)
#endif
#ifndef TPM_BAD_PARAMETER
/** A request parameter is invalid. */
# define TPM_BAD_PARAMETER                  UINT32_C(3)
#endif
#ifndef TPM_FAIL
/** The TPM failed to execute the request. */
# define TPM_FAIL                           UINT32_C(9)
#endif
/** @todo Extend as need arises. */
/** @} */


/* Some inline helpers to account for the unaligned members of the request and response headers. */

/**
 * Returns the request tag of the given TPM request header.
 *
 * @returns TPM request tag in bytes.
 * @param   pTpmReqHdr          Pointer to the TPM request header.
 */
DECLINLINE(uint16_t) RTTpmReqGetTag(PCTPMREQHDR pTpmReqHdr)
{
    return RT_BE2H_U16(pTpmReqHdr->u16Tag);
}


/**
 * Returns the request size of the given TPM request header.
 *
 * @returns TPM request size in bytes.
 * @param   pTpmReqHdr          Pointer to the TPM request header.
 */
DECLINLINE(size_t) RTTpmReqGetSz(PCTPMREQHDR pTpmReqHdr)
{
    uint32_t cbReq;
    memcpy(&cbReq, &pTpmReqHdr->cbReq, sizeof(pTpmReqHdr->cbReq));
    return RT_BE2H_U32(cbReq);
}


/**
 * Returns the request ordinal of the given TPM request header.
 *
 * @returns TPM request ordinal in bytes.
 * @param   pTpmReqHdr          Pointer to the TPM request header.
 */
DECLINLINE(uint32_t) RTTpmReqGetOrdinal(PCTPMREQHDR pTpmReqHdr)
{
    uint32_t u32Ordinal;
    memcpy(&u32Ordinal, &pTpmReqHdr->u32Ordinal, sizeof(pTpmReqHdr->u32Ordinal));
    return RT_BE2H_U32(u32Ordinal);
}


/**
 * Returns the response tag of the given TPM response header.
 *
 * @returns TPM request tag in bytes.
 * @param   pTpmRespHdr         Pointer to the TPM response header.
 */
DECLINLINE(uint16_t) RTTpmRespGetTag(PCTPMRESPHDR pTpmRespHdr)
{
    return RT_BE2H_U16(pTpmRespHdr->u16Tag);
}


/**
 * Returns the response size included in the given TPM response header.
 *
 * @returns TPM response size in bytes.
 * @param   pTpmRespHdr         Pointer to the TPM response header.
 */
DECLINLINE(size_t) RTTpmRespGetSz(PCTPMRESPHDR pTpmRespHdr)
{
    uint32_t cbResp;
    memcpy(&cbResp, &pTpmRespHdr->cbResp, sizeof(pTpmRespHdr->cbResp));
    return RT_BE2H_U32(cbResp);
}


/**
 * Returns the error code of the given TPM response header.
 *
 * @returns TPM response error code.
 * @param   pTpmRespHdr         Pointer to the TPM response header.
 */
DECLINLINE(uint32_t) RTTpmRespGetErrCode(PCTPMRESPHDR pTpmRespHdr)
{
    uint32_t u32ErrCode;
    memcpy(&u32ErrCode, &pTpmRespHdr->u32ErrCode, sizeof(pTpmRespHdr->u32ErrCode));
    return RT_BE2H_U32(u32ErrCode);
}

#endif /* !IPRT_INCLUDED_formats_tpm_h */

