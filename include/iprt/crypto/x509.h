/** @file
 * IPRT - Crypto - X.509, Public Key and Privilege Management Infrastructure.
 */

/*
 * Copyright (C) 2014-2026 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_crypto_x509_h
#define IPRT_INCLUDED_crypto_x509_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/asn1.h>
#include <iprt/crypto/pem.h>


RT_C_DECLS_BEGIN

struct RTCRPKCS7SETOFCERTS;


/** @defgroup grp_rt_crypto Crypto
 * @ingroup grp_rt
 * @{
 */

/** @defgroup grp_rt_crx509 RTCrX509 - Public Key and Privilege Management Infrastructure.
 * @{
 */

/**
 * X.509 algorithm identifier (IPRT representation).
 */
typedef struct RTCRX509ALGORITHMIDENTIFIER
{
    /** The sequence making up this algorithm identifier. */
    RTASN1SEQUENCECORE                  SeqCore;
    /** The algorithm object ID. */
    RTASN1OBJID                         Algorithm;
    /** Optional parameters specified by the algorithm. */
    RTASN1DYNTYPE                       Parameters;
} RTCRX509ALGORITHMIDENTIFIER;
/** Poitner to the IPRT representation of a X.509 algorithm identifier. */
typedef RTCRX509ALGORITHMIDENTIFIER *PRTCRX509ALGORITHMIDENTIFIER;
/** Poitner to the const IPRT representation of a X.509 algorithm identifier. */
typedef RTCRX509ALGORITHMIDENTIFIER const *PCRTCRX509ALGORITHMIDENTIFIER;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509ALGORITHMIDENTIFIER, RTDECL, RTCrX509AlgorithmIdentifier, SeqCore.Asn1Core);
RTASN1_IMPL_GEN_SET_OF_TYPEDEFS_AND_PROTOS(RTCRX509ALGORITHMIDENTIFIERS, RTCRX509ALGORITHMIDENTIFIER, RTDECL, RTCrX509AlgorithmIdentifiers);

/**
 * Tries to convert an X.509 digest algorithm ID into a RTDIGESTTYPE value.
 *
 * @returns Valid RTDIGESTTYPE on success, RTDIGESTTYPE_INVALID on failure.
 * @param   pThis               The IPRT representation of a X.509 algorithm
 *                              identifier object.
 * @param   fPureDigestsOnly    Whether to only match IDs that only identify
 *                              digest algorithms, or whether to also include
 *                              IDs that mixes hash and encryption/whatever.
 */
RTDECL(RTDIGESTTYPE) RTCrX509AlgorithmIdentifier_GetDigestType(PCRTCRX509ALGORITHMIDENTIFIER pThis, bool fPureDigestsOnly);

/**
 * Tries to figure the digest size of an X.509 digest algorithm ID.
 *
 * @returns The digest size in bytes, UINT32_MAX if unknown digest.
 * @param   pThis               The IPRT representation of a X.509 algorithm
 *                              identifier object.
 * @param   fPureDigestsOnly    Whether to only match IDs that only identify
 *                              digest algorithms, or whether to also include
 *                              IDs that mixes hash and encryption/whatever.
 */
RTDECL(uint32_t) RTCrX509AlgorithmIdentifier_GetDigestSize(PCRTCRX509ALGORITHMIDENTIFIER pThis, bool fPureDigestsOnly);

/**
 * Tries to get the encryption OID from the algorithm.
 *
 * @returns The encryption (cipher) OID  on success, NULL on failure.
 * @param   pThis               The IPRT representation of a X.509 algorithm
 *                              identifier object.
 * @param   fMustIncludeHash    Whether the algorithm ID represented by @a pThis
 *                              must include a hash (true) or whether it is
 *                              okay to accept pure encryption IDs as well
 *                              (false).
 */
RTDECL(const char *) RTCrX509AlgorithmIdentifier_GetEncryptionOid(PCRTCRX509ALGORITHMIDENTIFIER pThis, bool fMustIncludeHash);

/**
 * Tries to get the encryption OID from the given algorithm OID string.
 *
 * @returns The encryption (cipher) OID  on success, NULL on failure.
 * @param   pszAlgorithmOid     The IPRT representation of a X.509 algorithm
 *                              identifier object.
 * @param   fMustIncludeHash    Whether @a pszAlgorithmOid must include a hash
 *                              (true) or whether it is okay to accept pure
 *                              encryption IDs as well (false).
 */
RTDECL(const char *) RTCrX509AlgorithmIdentifier_GetEncryptionOidFromOid(const char *pszAlgorithmOid, bool fMustIncludeHash);

RTDECL(int) RTCrX509AlgorithmIdentifier_CompareWithString(PCRTCRX509ALGORITHMIDENTIFIER pThis, const char *pszObjId);

/**
 * Compares a digest with an encrypted digest algorithm, checking if they
 * specify the same digest.
 *
 * @returns 0 if same digest, -1 if the digest is unknown, 1 if the encrypted
 *          digest does not match.
 * @param   pDigest             The digest algorithm.
 * @param   pEncryptedDigest    The encrypted digest algorithm.
 */
RTDECL(int) RTCrX509AlgorithmIdentifier_CompareDigestAndEncryptedDigest(PCRTCRX509ALGORITHMIDENTIFIER pDigest,
                                                                        PCRTCRX509ALGORITHMIDENTIFIER pEncryptedDigest);
/**
 * Compares a digest OID with an encrypted digest algorithm OID, checking if
 * they specify the same digest.
 *
 * @returns 0 if same digest, -1 if the digest is unknown, 1 if the encrypted
 *          digest does not match.
 * @param   pszDigestOid            The digest algorithm OID.
 * @param   pszEncryptedDigestOid   The encrypted digest algorithm OID.
 */
RTDECL(int) RTCrX509AlgorithmIdentifier_CompareDigestOidAndEncryptedDigestOid(const char *pszDigestOid,
                                                                              const char *pszEncryptedDigestOid);


/**
 * Combine the encryption algorithm with the digest algorithm.
 *
 * @returns OID of encrypted digest algorithm.
 * @param   pEncryption         The encryption algorithm.  Will work if this is
 *                              the OID of an encrypted digest algorithm too, as
 *                              long as it matches @a pDigest.
 * @param   pDigest             The digest algorithm.  Will work if this is the
 *                              OID of an encrypted digest algorithm too, as
 *                              long as it matches @a pEncryption.
 */
RTDECL(const char *) RTCrX509AlgorithmIdentifier_CombineEncryptionAndDigest(PCRTCRX509ALGORITHMIDENTIFIER pEncryption,
                                                                            PCRTCRX509ALGORITHMIDENTIFIER pDigest);

/**
 * Combine the encryption algorithm OID with the digest algorithm OID.
 *
 * @returns OID of encrypted digest algorithm.
 * @param   pszEncryptionOid    The encryption algorithm.  Will work if this is
 *                              the OID of an encrypted digest algorithm too, as
 *                              long as it matches @a pszDigestOid.
 * @param   pszDigestOid        The digest algorithm.  Will work if this is the
 *                              OID of an encrypted digest algorithm too, as
 *                              long as it matches @a pszEncryptionOid.
 */
RTDECL(const char *) RTCrX509AlgorithmIdentifier_CombineEncryptionOidAndDigestOid(const char *pszEncryptionOid,
                                                                                  const char *pszDigestOid);


/** @name Typical Digest Algorithm OIDs.
 * @{ */
#define RTCRX509ALGORITHMIDENTIFIERID_MD2               "1.2.840.113549.2.2"
#define RTCRX509ALGORITHMIDENTIFIERID_MD4               "1.2.840.113549.2.4"
#define RTCRX509ALGORITHMIDENTIFIERID_MD5               "1.2.840.113549.2.5"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA0              "1.3.14.3.2.18"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA1              "1.3.14.3.2.26"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA256            "2.16.840.1.101.3.4.2.1"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA384            "2.16.840.1.101.3.4.2.2"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA512            "2.16.840.1.101.3.4.2.3"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA224            "2.16.840.1.101.3.4.2.4"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA512T224        "2.16.840.1.101.3.4.2.5"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA512T256        "2.16.840.1.101.3.4.2.6"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA3_224          "2.16.840.1.101.3.4.2.7"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA3_256          "2.16.840.1.101.3.4.2.8"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA3_384          "2.16.840.1.101.3.4.2.9"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA3_512          "2.16.840.1.101.3.4.2.10"
#define RTCRX509ALGORITHMIDENTIFIERID_WHIRLPOOL         "1.0.10118.3.0.55"
/** @} */

/** @name Encrypted Digest Algorithm OIDs.
 * @remarks The PKCS variants are the default ones, alternative OID are marked
 *          as such.
 * @{ */
#define RTCRX509ALGORITHMIDENTIFIERID_RSA                   "1.2.840.113549.1.1.1"
#define RTCRX509ALGORITHMIDENTIFIERID_MD2_WITH_RSA          "1.2.840.113549.1.1.2"
#define RTCRX509ALGORITHMIDENTIFIERID_MD4_WITH_RSA          "1.2.840.113549.1.1.3"
#define RTCRX509ALGORITHMIDENTIFIERID_MD5_WITH_RSA          "1.2.840.113549.1.1.4"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA1_WITH_RSA         "1.2.840.113549.1.1.5"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA256_WITH_RSA       "1.2.840.113549.1.1.11"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA384_WITH_RSA       "1.2.840.113549.1.1.12"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA512_WITH_RSA       "1.2.840.113549.1.1.13"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA224_WITH_RSA       "1.2.840.113549.1.1.14"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA512T224_WITH_RSA   "1.2.840.113549.1.1.15"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA512T256_WITH_RSA   "1.2.840.113549.1.1.16"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA3_224_WITH_RSA     "2.16.840.1.101.3.4.3.13"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA3_256_WITH_RSA     "2.16.840.1.101.3.4.3.14"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA3_384_WITH_RSA     "2.16.840.1.101.3.4.3.15"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA3_512_WITH_RSA     "2.16.840.1.101.3.4.3.16"
#define RTCRX509ALGORITHMIDENTIFIERID_ECDSA                 "1.2.840.10045.2.1"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA1_WITH_ECDSA       "1.2.840.10045.4.1"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA224_WITH_ECDSA     "1.2.840.10045.4.3.1"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA256_WITH_ECDSA     "1.2.840.10045.4.3.2"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA384_WITH_ECDSA     "1.2.840.10045.4.3.3"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA512_WITH_ECDSA     "1.2.840.10045.4.3.4"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA3_224_WITH_ECDSA   "2.16.840.1.101.3.4.3.9"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA3_256_WITH_ECDSA   "2.16.840.1.101.3.4.3.10"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA3_384_WITH_ECDSA   "2.16.840.1.101.3.4.3.11"
#define RTCRX509ALGORITHMIDENTIFIERID_SHA3_512_WITH_ECDSA   "2.16.840.1.101.3.4.3.12"
/** @} */




/**
 * One X.509 AttributeTypeAndValue (IPRT representation).
 */
typedef struct RTCRX509ATTRIBUTETYPEANDVALUE
{
    /** Sequence core. */
    RTASN1SEQUENCECORE                  SeqCore;
    /** The attribute type (object ID). */
    RTASN1OBJID                         Type;
    /** The attribute value (what it is is defined by Type). */
    RTASN1DYNTYPE                       Value;
} RTCRX509ATTRIBUTETYPEANDVALUE;
/** Pointer to a X.509 AttributeTypeAndValue (IPRT representation). */
typedef RTCRX509ATTRIBUTETYPEANDVALUE *PRTCRX509ATTRIBUTETYPEANDVALUE;
/** Pointer to a const X.509 AttributeTypeAndValue (IPRT representation). */
typedef RTCRX509ATTRIBUTETYPEANDVALUE const *PCRTCRX509ATTRIBUTETYPEANDVALUE;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509ATTRIBUTETYPEANDVALUE, RTDECL, RTCrX509AttributeTypeAndValue, SeqCore.Asn1Core);
RTASN1_IMPL_GEN_SET_OF_TYPEDEFS_AND_PROTOS(RTCRX509ATTRIBUTETYPEANDVALUES, RTCRX509ATTRIBUTETYPEANDVALUE, RTDECL, RTCrX509AttributeTypeAndValues);

RTASN1TYPE_ALIAS(RTCRX509RELATIVEDISTINGUISHEDNAME, RTCRX509ATTRIBUTETYPEANDVALUES, RTCrX509RelativeDistinguishedName, RTCrX509AttributeTypeAndValues);


RTASN1_IMPL_GEN_SEQ_OF_TYPEDEFS_AND_PROTOS(RTCRX509NAME, RTCRX509RELATIVEDISTINGUISHEDNAME, RTDECL, RTCrX509Name);
RTDECL(int) RTCrX509Name_CheckSanity(PCRTCRX509NAME pName, uint32_t fFlags, PRTERRINFO pErrInfo, const char *pszErrorTag);
RTDECL(bool) RTCrX509Name_MatchByRfc5280(PCRTCRX509NAME pLeft, PCRTCRX509NAME pRight);

/**
 * Name constraint matching (RFC-5280).
 *
 * @returns true on match, false on mismatch.
 * @param   pConstraint     The constraint name.
 * @param   pName           The name to match against the constraint.
 * @sa      RTCrX509GeneralName_ConstraintMatch,
 *          RTCrX509RelativeDistinguishedName_ConstraintMatch
 */
RTDECL(bool) RTCrX509Name_ConstraintMatch(PCRTCRX509NAME pConstraint, PCRTCRX509NAME pName);
RTDECL(int) RTCrX509Name_RecodeAsUtf8(PRTCRX509NAME pThis, PCRTASN1ALLOCATORVTABLE pAllocator);

/**
 * Matches the directory name against a comma separated list of the component
 * strings (case sensitive).
 *
 * @returns true if match, false if mismatch.
 * @param   pThis           The name object.
 * @param   pszString       The string to match against. For example:
 *                          "C=US, ST=California, L=Redwood Shores, O=Oracle Corporation"
 *
 * @remarks This is doing a straight compare, no extra effort is expended in
 *          dealing with different component order.  If the component order
 *          differs, there won't be any match.
 */
RTDECL(bool) RTCrX509Name_MatchWithString(PCRTCRX509NAME pThis, const char *pszString);

/**
 * Formats the name as a command separated list of components with type
 * prefixes.
 *
 * The output of this function is suitable for use with
 * RTCrX509Name_MatchWithString.
 *
 * @returns IPRT status code.
 * @param   pThis           The name object.
 * @param   pszBuf          The output buffer.
 * @param   cbBuf           The size of the output buffer.
 * @param   pcbActual       Where to return the number of bytes required for the
 *                          output, including the null terminator character.
 *                          Optional.
 */
RTDECL(int) RTCrX509Name_FormatAsString(PCRTCRX509NAME pThis, char *pszBuf, size_t cbBuf, size_t *pcbActual);


/**
 * Looks up the RDN ID and returns the short name for it, if found.
 *
 * @returns Short name (e.g. 'CN') or NULL.
 * @param   pRdnId          The RDN ID to look up.
 */
RTDECL(const char *) RTCrX509Name_GetShortRdn(PCRTASN1OBJID pRdnId);

/**
 * One X.509 OtherName (IPRT representation).
 */
typedef struct RTCRX509OTHERNAME
{
    /** The sequence core. */
    RTASN1SEQUENCECORE                  SeqCore;
    /** The name type identifier. */
    RTASN1OBJID                         TypeId;
    /** The name value (explicit tag 0). */
    RTASN1DYNTYPE                       Value;
} RTCRX509OTHERNAME;
/** Pointer to a X.509 OtherName (IPRT representation). */
typedef RTCRX509OTHERNAME *PRTCRX509OTHERNAME;
/** Pointer to a const X.509 OtherName (IPRT representation). */
typedef RTCRX509OTHERNAME const *PCRTCRX509OTHERNAME;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509OTHERNAME, RTDECL, RTCrX509OtherName, SeqCore.Asn1Core);


typedef enum RTCRX509GENERALNAMECHOICE
{
    RTCRX509GENERALNAMECHOICE_INVALID = 0,
    RTCRX509GENERALNAMECHOICE_OTHER_NAME,
    RTCRX509GENERALNAMECHOICE_RFC822_NAME,
    RTCRX509GENERALNAMECHOICE_DNS_NAME,
    RTCRX509GENERALNAMECHOICE_X400_ADDRESS,
    RTCRX509GENERALNAMECHOICE_DIRECTORY_NAME,
    RTCRX509GENERALNAMECHOICE_EDI_PARTY_NAME,
    RTCRX509GENERALNAMECHOICE_URI,
    RTCRX509GENERALNAMECHOICE_IP_ADDRESS,
    RTCRX509GENERALNAMECHOICE_REGISTERED_ID,
    RTCRX509GENERALNAMECHOICE_END,
    RTCRX509GENERALNAMECHOICE_32BIT_HACK = 0x7fffffff
} RTCRX509GENERALNAMECHOICE;

/**
 * One X.509 GeneralName (IPRT representation).
 *
 * This is represented as a union.  Use the RTCRX509GENERALNAME_IS_XXX predicate
 * macros to figure out which member is valid (Asn1Core is always valid).
 */
typedef struct RTCRX509GENERALNAME
{
    /** Dummy ASN.1 record, not encoded. */
    RTASN1DUMMY                         Dummy;
    /** The value allocation. */
    RTASN1ALLOCATION                    Allocation;
    /** The choice of value.   */
    RTCRX509GENERALNAMECHOICE           enmChoice;
    /** The value union. */
    union
    {
        /** Tag 0: Other Name.  */
        PRTCRX509OTHERNAME              pT0_OtherName;
        /** Tag 1: RFC-822 Name.  */
        PRTASN1STRING                   pT1_Rfc822;
        /** Tag 2: DNS name.  */
        PRTASN1STRING                   pT2_DnsName;
        /** Tag 3: X.400 Address.  */
        struct
        {
            /** Context tag 3. */
            RTASN1CONTEXTTAG3           CtxTag3;
            /** Later. */
            RTASN1DYNTYPE               X400Address;
        } *pT3;
        /** Tag 4: Directory Name.  */
        struct
        {
            /** Context tag 4. */
            RTASN1CONTEXTTAG4           CtxTag4;
            /** Directory name. */
            RTCRX509NAME                DirectoryName;
        } *pT4;
        /** Tag 5: EDI Party Name.  */
        struct
        {
            /** Context tag 5. */
            RTASN1CONTEXTTAG5           CtxTag5;
            /** Later. */
            RTASN1DYNTYPE               EdiPartyName;
        } *pT5;
        /** Tag 6: URI.  */
        PRTASN1STRING                   pT6_Uri;
        /** Tag 7: IP address. Either 4/8 (IPv4) or 16/32 (IPv16) octets long. */
        PRTASN1OCTETSTRING              pT7_IpAddress;
        /** Tag 8: Registered ID. */
        PRTASN1OBJID                    pT8_RegisteredId;
    } u;
} RTCRX509GENERALNAME;
/** Pointer to the IPRT representation of an X.509 general name. */
typedef RTCRX509GENERALNAME *PRTCRX509GENERALNAME;
/** Pointer to the const IPRT representation of an X.509 general name. */
typedef RTCRX509GENERALNAME const *PCRTCRX509GENERALNAME;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509GENERALNAME, RTDECL, RTCrX509GeneralName, Dummy.Asn1Core);

/** @name RTCRX509GENERALNAME tag predicates.
 * @{ */
#define RTCRX509GENERALNAME_IS_OTHER_NAME(a_GenName)        ((a_GenName)->enmChoice == RTCRX509GENERALNAMECHOICE_OTHER_NAME)
#define RTCRX509GENERALNAME_IS_RFC822_NAME(a_GenName)       ((a_GenName)->enmChoice == RTCRX509GENERALNAMECHOICE_RFC822_NAME)
#define RTCRX509GENERALNAME_IS_DNS_NAME(a_GenName)          ((a_GenName)->enmChoice == RTCRX509GENERALNAMECHOICE_DNS_NAME)
#define RTCRX509GENERALNAME_IS_X400_ADDRESS(a_GenName)      ((a_GenName)->enmChoice == RTCRX509GENERALNAMECHOICE_X400_ADDRESS)
#define RTCRX509GENERALNAME_IS_DIRECTORY_NAME(a_GenName)    ((a_GenName)->enmChoice == RTCRX509GENERALNAMECHOICE_DIRECTORY_NAME)
#define RTCRX509GENERALNAME_IS_EDI_PARTY_NAME(a_GenName)    ((a_GenName)->enmChoice == RTCRX509GENERALNAMECHOICE_EDI_PARTY_NAME)
#define RTCRX509GENERALNAME_IS_URI(a_GenName)               ((a_GenName)->enmChoice == RTCRX509GENERALNAMECHOICE_URI)
#define RTCRX509GENERALNAME_IS_IP_ADDRESS(a_GenName)        ((a_GenName)->enmChoice == RTCRX509GENERALNAMECHOICE_IP_ADDRESS)
#define RTCRX509GENERALNAME_IS_REGISTERED_ID(a_GenName)     ((a_GenName)->enmChoice == RTCRX509GENERALNAMECHOICE_REGISTERED_ID)
/** @} */


RTASN1_IMPL_GEN_SEQ_OF_TYPEDEFS_AND_PROTOS(RTCRX509GENERALNAMES, RTCRX509GENERALNAME, RTDECL, RTCrX509GeneralNames);
RTDECL(bool) RTCrX509GeneralName_ConstraintMatch(PCRTCRX509GENERALNAME pConstraint, PCRTCRX509GENERALNAME pName);


/**
 * X.509 Validity (IPRT representation).
 */
typedef struct RTCRX509VALIDITY
{
    /** Core sequence bits. */
    RTASN1SEQUENCECORE                  SeqCore;
    /** Effective starting. */
    RTASN1TIME                          NotBefore;
    /** Expires after. */
    RTASN1TIME                          NotAfter;
} RTCRX509VALIDITY;
/** Pointer to the IPRT representation of an X.509 validity sequence. */
typedef RTCRX509VALIDITY *PRTCRX509VALIDITY;
/** Pointer ot the const IPRT representation of an X.509 validity sequence. */
typedef RTCRX509VALIDITY const *PCRTCRX509VALIDITY;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509VALIDITY, RTDECL, RTCrX509Validity, SeqCore.Asn1Core);

RTDECL(bool) RTCrX509Validity_IsValidAtTimeSpec(PCRTCRX509VALIDITY pThis, PCRTTIMESPEC pTimeSpec);


#if 0
/**
 * X.509 UniqueIdentifier (IPRT representation).
 */
typedef struct RTCRX509UNIQUEIDENTIFIER
{
    /** Representation is a bit string. */
    RTASN1BITSTRING                     BitString;
} RTCRX509UNIQUEIDENTIFIER;
/** Pointer to the IPRT representation of an X.509 unique identifier. */
typedef RTCRX509UNIQUEIDENTIFIER *PRTCRX509UNIQUEIDENTIFIER;
/** Pointer to the const IPRT representation of an X.509 unique identifier. */
typedef RTCRX509UNIQUEIDENTIFIER const *PCRTCRX509UNIQUEIDENTIFIER;
RTASN1TYPE_STANDARD_PROTOTYPES_NO_GET_CORE(RTCRX509UNIQUEIDENTIFIER, RTDECL, RTCrX509UniqueIdentifier);
#endif
RTASN1TYPE_ALIAS(RTCRX509UNIQUEIDENTIFIER, RTASN1BITSTRING, RTCrX509UniqueIdentifier, RTAsn1BitString);


/**
 * X.509 SubjectPublicKeyInfo (IPRT representation).
 */
typedef struct RTCRX509SUBJECTPUBLICKEYINFO
{
    /** Core sequence bits. */
    RTASN1SEQUENCECORE                  SeqCore;
    /** The algorithm used with the public key. */
    RTCRX509ALGORITHMIDENTIFIER         Algorithm;
    /** A bit string containing the public key.
     *
     * For algorithms like rsaEncryption this is generally a sequence of two
     * integers, where the first one has lots of bits, and the second one being a
     * modulous value.  These are details specific to the algorithm and not relevant
     * when validating the certificate chain. */
    RTASN1BITSTRING                     SubjectPublicKey;
} RTCRX509SUBJECTPUBLICKEYINFO;
/** Pointer to the IPRT representation of an X.509 subject public key info
 *  sequence. */
typedef RTCRX509SUBJECTPUBLICKEYINFO *PRTCRX509SUBJECTPUBLICKEYINFO;
/** Pointer to the const IPRT representation of an X.509 subject public key info
 *  sequence. */
typedef RTCRX509SUBJECTPUBLICKEYINFO const *PCRTCRX509SUBJECTPUBLICKEYINFO;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509SUBJECTPUBLICKEYINFO, RTDECL, RTCrX509SubjectPublicKeyInfo, SeqCore.Asn1Core);


/**
 * One X.509 AuthorityKeyIdentifier (IPRT representation).
 */
typedef struct RTCRX509AUTHORITYKEYIDENTIFIER
{
    /** Sequence core. */
    RTASN1SEQUENCECORE              SeqCore;
    /** Tag 0, optional, implicit: Key identifier. */
    RTASN1OCTETSTRING               KeyIdentifier;
    /** Tag 1, optional, implicit: Issuer name. */
    RTCRX509GENERALNAMES            AuthorityCertIssuer;
    /** Tag 2, optional, implicit: Serial number of issuer. */
    RTASN1INTEGER                   AuthorityCertSerialNumber;
}  RTCRX509AUTHORITYKEYIDENTIFIER;
/** Pointer to the IPRT representation of an X.509 AuthorityKeyIdentifier
 * sequence. */
typedef RTCRX509AUTHORITYKEYIDENTIFIER *PRTCRX509AUTHORITYKEYIDENTIFIER;
/** Pointer to the const IPRT representation of an X.509 AuthorityKeyIdentifier
 * sequence. */
typedef RTCRX509AUTHORITYKEYIDENTIFIER const *PCRTCRX509AUTHORITYKEYIDENTIFIER;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509AUTHORITYKEYIDENTIFIER, RTDECL, RTCrX509AuthorityKeyIdentifier, SeqCore.Asn1Core);


/**
 * One X.509 OldAuthorityKeyIdentifier (IPRT representation).
 */
typedef struct RTCRX509OLDAUTHORITYKEYIDENTIFIER
{
    /** Sequence core. */
    RTASN1SEQUENCECORE              SeqCore;
    /** Tag 0, optional, implicit: Key identifier. */
    RTASN1OCTETSTRING               KeyIdentifier;
    struct
    {
        RTASN1CONTEXTTAG1           CtxTag1;
        /** Tag 1, optional, implicit: Issuer name. */
        RTCRX509NAME                AuthorityCertIssuer;
    } T1;
    /** Tag 2, optional, implicit: Serial number of issuer. */
    RTASN1INTEGER                   AuthorityCertSerialNumber;
}  RTCRX509OLDAUTHORITYKEYIDENTIFIER;
/** Pointer to the IPRT representation of an X.509 AuthorityKeyIdentifier
 * sequence. */
typedef RTCRX509OLDAUTHORITYKEYIDENTIFIER *PRTCRX509OLDAUTHORITYKEYIDENTIFIER;
/** Pointer to the const IPRT representation of an X.509 AuthorityKeyIdentifier
 * sequence. */
typedef RTCRX509OLDAUTHORITYKEYIDENTIFIER const *PCRTCRX509OLDAUTHORITYKEYIDENTIFIER;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509OLDAUTHORITYKEYIDENTIFIER, RTDECL, RTCrX509OldAuthorityKeyIdentifier, SeqCore.Asn1Core);


/**
 * One X.509 PolicyQualifierInfo (IPRT representation).
 */
typedef struct RTCRX509POLICYQUALIFIERINFO
{
    /** Core sequence bits. */
    RTASN1SEQUENCECORE                  SeqCore;
    /** The policy object ID. */
    RTASN1OBJID                         PolicyQualifierId;
    /** Anything defined by the policy qualifier id. */
    RTASN1DYNTYPE                       Qualifier;
} RTCRX509POLICYQUALIFIERINFO;
/** Pointer to the IPRT representation of an X.509 PolicyQualifierInfo
 * sequence. */
typedef RTCRX509POLICYQUALIFIERINFO *PRTCRX509POLICYQUALIFIERINFO;
/** Pointer to the const IPRT representation of an X.509 PolicyQualifierInfo
 * sequence. */
typedef RTCRX509POLICYQUALIFIERINFO const *PCRTCRX509POLICYQUALIFIERINFO;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509POLICYQUALIFIERINFO, RTDECL, RTCrX509PolicyQualifierInfo, SeqCore.Asn1Core);
RTASN1_IMPL_GEN_SEQ_OF_TYPEDEFS_AND_PROTOS(RTCRX509POLICYQUALIFIERINFOS, RTCRX509POLICYQUALIFIERINFO, RTDECL, RTCrX509PolicyQualifierInfos);


/**
 * One X.509 PolicyInformation (IPRT representation).
 */
typedef struct RTCRX509POLICYINFORMATION
{
    /** Core sequence bits. */
    RTASN1SEQUENCECORE                  SeqCore;
    /** The policy object ID. */
    RTASN1OBJID                         PolicyIdentifier;
    /** Optional sequence of policy qualifiers. */
    RTCRX509POLICYQUALIFIERINFOS        PolicyQualifiers;
} RTCRX509POLICYINFORMATION;
/** Pointer to the IPRT representation of an X.509 PolicyInformation
 * sequence. */
typedef RTCRX509POLICYINFORMATION *PRTCRX509POLICYINFORMATION;
/** Pointer to the const IPRT representation of an X.509 PolicyInformation
 * sequence. */
typedef RTCRX509POLICYINFORMATION const *PCRTCRX509POLICYINFORMATION;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509POLICYINFORMATION, RTDECL, RTCrX509PolicyInformation, SeqCore.Asn1Core);
RTASN1_IMPL_GEN_SEQ_OF_TYPEDEFS_AND_PROTOS(RTCRX509CERTIFICATEPOLICIES, RTCRX509POLICYINFORMATION, RTDECL, RTCrX509CertificatePolicies);

/** Sepcial policy object ID that matches any policy. */
#define RTCRX509_ID_CE_CP_ANY_POLICY_OID    "2.5.29.32.0"


/**
 * One X.509 PolicyMapping (IPRT representation).
 */
typedef struct RTCRX509POLICYMAPPING
{
    /** Core sequence bits. */
    RTASN1SEQUENCECORE                  SeqCore;
    /** Issuer policy ID. */
    RTASN1OBJID                         IssuerDomainPolicy;
    /** Subject policy ID. */
    RTASN1OBJID                         SubjectDomainPolicy;
} RTCRX509POLICYMAPPING;
/** Pointer to the IPRT representation of a sequence of X.509 PolicyMapping. */
typedef RTCRX509POLICYMAPPING *PRTCRX509POLICYMAPPING;
/** Pointer to the const IPRT representation of a sequence of X.509
 * PolicyMapping. */
typedef RTCRX509POLICYMAPPING const *PCRTCRX509POLICYMAPPING;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509POLICYMAPPING, RTDECL, RTCrX509PolicyMapping, SeqCore.Asn1Core);
RTASN1_IMPL_GEN_SEQ_OF_TYPEDEFS_AND_PROTOS(RTCRX509POLICYMAPPINGS, RTCRX509POLICYMAPPING, RTDECL, RTCrX509PolicyMappings);


/**
 * X.509 BasicConstraints (IPRT representation).
 */
typedef struct RTCRX509BASICCONSTRAINTS
{
    /** Core sequence bits. */
    RTASN1SEQUENCECORE                  SeqCore;
    /** Is this ia certficiate authority? Default to false. */
    RTASN1BOOLEAN                       CA;
    /** Path length constraint. */
    RTASN1INTEGER                       PathLenConstraint;
} RTCRX509BASICCONSTRAINTS;
/** Pointer to the IPRT representation of a sequence of X.509
 *  BasicConstraints. */
typedef RTCRX509BASICCONSTRAINTS *PRTCRX509BASICCONSTRAINTS;
/** Pointer to the const IPRT representation of a sequence of X.509
 * BasicConstraints. */
typedef RTCRX509BASICCONSTRAINTS const *PCRTCRX509BASICCONSTRAINTS;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509BASICCONSTRAINTS, RTDECL, RTCrX509BasicConstraints, SeqCore.Asn1Core);


/**
 * X.509 GeneralSubtree (IPRT representation).
 */
typedef struct RTCRX509GENERALSUBTREE
{
    /** Core sequence bits. */
    RTASN1SEQUENCECORE                  SeqCore;
    /** Base name. */
    RTCRX509GENERALNAME                 Base;
    /** Tag 0, optional: Minimum, default 0.  Fixed at 0 by RFC-5280. */
    RTASN1INTEGER                       Minimum;
    /** Tag 1, optional: Maximum. Fixed as not-present by RFC-5280. */
    RTASN1INTEGER                       Maximum;
} RTCRX509GENERALSUBTREE;
/** Pointer to the IPRT representation of a sequence of X.509 GeneralSubtree. */
typedef RTCRX509GENERALSUBTREE *PRTCRX509GENERALSUBTREE;
/** Pointer to the const IPRT representation of a sequence of X.509
 * GeneralSubtree. */
typedef RTCRX509GENERALSUBTREE const *PCRTCRX509GENERALSUBTREE;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509GENERALSUBTREE, RTDECL, RTCrX509GeneralSubtree, SeqCore.Asn1Core);

RTDECL(bool) RTCrX509GeneralSubtree_ConstraintMatch(PCRTCRX509GENERALSUBTREE pConstraint, PCRTCRX509GENERALSUBTREE pName);

RTASN1_IMPL_GEN_SEQ_OF_TYPEDEFS_AND_PROTOS(RTCRX509GENERALSUBTREES, RTCRX509GENERALSUBTREE, RTDECL, RTCrX509GeneralSubtrees);


/**
 * X.509 NameConstraints (IPRT representation).
 */
typedef struct RTCRX509NAMECONSTRAINTS
{
    /** Core sequence bits. */
    RTASN1SEQUENCECORE                  SeqCore;
    /** Tag 0, optional: Permitted subtrees. */
    struct
    {
        /** Context tag. */
        RTASN1CONTEXTTAG0               CtxTag0;
        /** The permitted subtrees. */
        RTCRX509GENERALSUBTREES         PermittedSubtrees;
    } T0;
    /** Tag 1, optional: Excluded subtrees. */
    struct
    {
        /** Context tag. */
        RTASN1CONTEXTTAG1               CtxTag1;
        /** The excluded subtrees. */
        RTCRX509GENERALSUBTREES         ExcludedSubtrees;
    } T1;
} RTCRX509NAMECONSTRAINTS;
/** Pointer to the IPRT representation of a sequence of X.509
 *  NameConstraints. */
typedef RTCRX509NAMECONSTRAINTS *PRTCRX509NAMECONSTRAINTS;
/** Pointer to the const IPRT representation of a sequence of X.509
 * NameConstraints. */
typedef RTCRX509NAMECONSTRAINTS const *PCRTCRX509NAMECONSTRAINTS;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509NAMECONSTRAINTS, RTDECL, RTCrX509NameConstraints, SeqCore.Asn1Core);


/**
 * X.509 PolicyConstraints (IPRT representation).
 */
typedef struct RTCRX509POLICYCONSTRAINTS
{
    /** Core sequence bits. */
    RTASN1SEQUENCECORE                  SeqCore;
    /** Tag 0, optional: Certificates before an explicit policy is required. */
    RTASN1INTEGER                       RequireExplicitPolicy;
    /** Tag 1, optional: Certificates before policy mapping is inhibited. */
    RTASN1INTEGER                       InhibitPolicyMapping;
} RTCRX509POLICYCONSTRAINTS;
/** Pointer to the IPRT representation of a sequence of X.509
 *  PolicyConstraints. */
typedef RTCRX509POLICYCONSTRAINTS *PRTCRX509POLICYCONSTRAINTS;
/** Pointer to the const IPRT representation of a sequence of X.509
 * PolicyConstraints. */
typedef RTCRX509POLICYCONSTRAINTS const *PCRTCRX509POLICYCONSTRAINTS;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509POLICYCONSTRAINTS, RTDECL, RTCrX509PolicyConstraints, SeqCore.Asn1Core);


/**
 * Indicates what an X.509 extension value encapsulates.
 */
typedef enum RTCRX509EXTENSIONVALUE
{
    RTCRX509EXTENSIONVALUE_INVALID = 0,
    /** Unknown, no decoding available just the octet string. */
    RTCRX509EXTENSIONVALUE_UNKNOWN,
    /** Unencapsulated (i.e. octet string). */
    RTCRX509EXTENSIONVALUE_NOT_ENCAPSULATED,

    /** Bit string (RTASN1BITSTRING). */
    RTCRX509EXTENSIONVALUE_BIT_STRING,
    /** Octet string (RTASN1OCTETSTRING). */
    RTCRX509EXTENSIONVALUE_OCTET_STRING,
    /** Integer string (RTASN1INTEGER). */
    RTCRX509EXTENSIONVALUE_INTEGER,
    /** Sequence of object identifiers (RTASN1SEQOFOBJIDS). */
    RTCRX509EXTENSIONVALUE_SEQ_OF_OBJ_IDS,

    /** Authority key identifier (RTCRX509AUTHORITYKEYIDENTIFIER). */
    RTCRX509EXTENSIONVALUE_AUTHORITY_KEY_IDENTIFIER,
    /** Old Authority key identifier (RTCRX509OLDAUTHORITYKEYIDENTIFIER). */
    RTCRX509EXTENSIONVALUE_OLD_AUTHORITY_KEY_IDENTIFIER,
    /** Certificate policies (RTCRX509CERTIFICATEPOLICIES). */
    RTCRX509EXTENSIONVALUE_CERTIFICATE_POLICIES,
    /** Sequence of policy mappings (RTCRX509POLICYMAPPINGS). */
    RTCRX509EXTENSIONVALUE_POLICY_MAPPINGS,
    /** Basic constraints (RTCRX509BASICCONSTRAINTS). */
    RTCRX509EXTENSIONVALUE_BASIC_CONSTRAINTS,
    /** Name constraints (RTCRX509NAMECONSTRAINTS). */
    RTCRX509EXTENSIONVALUE_NAME_CONSTRAINTS,
    /** Policy constraints (RTCRX509POLICYCONSTRAINTS). */
    RTCRX509EXTENSIONVALUE_POLICY_CONSTRAINTS,
    /** Sequence of general names (RTCRX509GENERALNAMES). */
    RTCRX509EXTENSIONVALUE_GENERAL_NAMES,

    /** Blow the type up to 32-bits. */
    RTCRX509EXTENSIONVALUE_32BIT_HACK = 0x7fffffff
} RTCRX509EXTENSIONVALUE;

/**
 * One X.509 Extension (IPRT representation).
 */
typedef struct RTCRX509EXTENSION
{
    /** Core sequence bits. */
    RTASN1SEQUENCECORE                  SeqCore;
    /** Extension ID. */
    RTASN1OBJID                         ExtnId;
    /** Whether this is critical (default @c false). */
    RTASN1BOOLEAN                       Critical;
    /** Indicates what ExtnValue.pEncapsulated points at. */
    RTCRX509EXTENSIONVALUE              enmValue;
    /** The value.
     * Contains extension specific data that we don't yet parse. */
    RTASN1OCTETSTRING                   ExtnValue;
} RTCRX509EXTENSION;
/** Pointer to the IPRT representation of one X.509 extensions. */
typedef RTCRX509EXTENSION *PRTCRX509EXTENSION;
/** Pointer to the const IPRT representation of one X.509 extension. */
typedef RTCRX509EXTENSION const *PCRTCRX509EXTENSION;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509EXTENSION, RTDECL, RTCrX509Extension, SeqCore.Asn1Core);
RTASN1_IMPL_GEN_SEQ_OF_TYPEDEFS_AND_PROTOS(RTCRX509EXTENSIONS, RTCRX509EXTENSION, RTDECL, RTCrX509Extensions);

RTDECL(int) RTCrX509Extension_ExtnValue_DecodeAsn1(PRTASN1CURSOR pCursor, uint32_t fFlags,
                                                   PRTCRX509EXTENSION pThis, const char *pszErrorTag);


/**
 * X.509 To-be-signed certificate information (IPRT representation).
 */
typedef struct RTCRX509TBSCERTIFICATE
{
    /** Sequence core. */
    RTASN1SEQUENCECORE                  SeqCore;
    /** Structure version. */
    struct
    {
        /** Context tag with value 0. */
        RTASN1CONTEXTTAG0               CtxTag0;
        /** The actual value (RTCRX509TBSCERTIFICATE_V1, ...). */
        RTASN1INTEGER                   Version;
    } T0;
    /** The serial number of the certificate. */
    RTASN1INTEGER                       SerialNumber;
    /** The signature algorithm. */
    RTCRX509ALGORITHMIDENTIFIER         Signature;
    /** The issuer name. */
    RTCRX509NAME                        Issuer;
    /** The certificate validity period. */
    RTCRX509VALIDITY                    Validity;
    /** The subject name. */
    RTCRX509NAME                        Subject;
    /** The public key for this certificate.  */
    RTCRX509SUBJECTPUBLICKEYINFO        SubjectPublicKeyInfo;
    /** Issuer unique identifier (optional, version >= v2).  */
    struct
    {
        /** Context tag with value 1. */
        RTASN1CONTEXTTAG1               CtxTag1;
        /** The unique identifier value. */
        RTCRX509UNIQUEIDENTIFIER        IssuerUniqueId;
    } T1;
    /** Subject unique identifier (optional, version >= v2).  */
    struct
    {
        /** Context tag with value 2. */
        RTASN1CONTEXTTAG2               CtxTag2;
        /** The unique identifier value. */
        RTCRX509UNIQUEIDENTIFIER        SubjectUniqueId;
    } T2;
    /** Extensions (optional, version >= v3).  */
    struct
    {
        /** Context tag with value 3. */
        RTASN1CONTEXTTAG3               CtxTag3;
        /** The unique identifier value. */
        RTCRX509EXTENSIONS              Extensions;
        /** Extensions summary flags (RTCRX509TBSCERTIFICATE_F_PRESENT_XXX). */
        uint32_t                        fFlags;
        /** Key usage flags (RTCRX509CERT_KEY_USAGE_F_XXX). */
        uint32_t                        fKeyUsage;
        /** Extended key usage flags (RTCRX509CERT_EKU_F_XXX). */
        uint64_t                        fExtKeyUsage;

        /** Pointer to the authority key ID extension if present. */
        PCRTCRX509AUTHORITYKEYIDENTIFIER pAuthorityKeyIdentifier;
        /** Pointer to the OLD authority key ID extension if present. */
        PCRTCRX509OLDAUTHORITYKEYIDENTIFIER pOldAuthorityKeyIdentifier;
        /** Pointer to the subject key ID extension if present. */
        PCRTASN1OCTETSTRING             pSubjectKeyIdentifier;
        /** Pointer to the alternative subject name extension if present. */
        PCRTCRX509GENERALNAMES          pAltSubjectName;
        /** Pointer to the alternative issuer name extension if present. */
        PCRTCRX509GENERALNAMES          pAltIssuerName;
        /** Pointer to the certificate policies extension if present. */
        PCRTCRX509CERTIFICATEPOLICIES   pCertificatePolicies;
        /** Pointer to the policy mappings extension if present. */
        PCRTCRX509POLICYMAPPINGS        pPolicyMappings;
        /** Pointer to the basic constraints extension if present. */
        PCRTCRX509BASICCONSTRAINTS      pBasicConstraints;
        /** Pointer to the name constraints extension if present. */
        PCRTCRX509NAMECONSTRAINTS       pNameConstraints;
        /** Pointer to the policy constraints extension if present. */
        PCRTCRX509POLICYCONSTRAINTS     pPolicyConstraints;
        /** Pointer to the inhibit anyPolicy extension if present. */
        PCRTASN1INTEGER                 pInhibitAnyPolicy;
    } T3;
} RTCRX509TBSCERTIFICATE;
/** Pointer to the IPRT representation of a X.509 TBSCertificate. */
typedef RTCRX509TBSCERTIFICATE *PRTCRX509TBSCERTIFICATE;
/** Pointer to the const IPRT representation of a X.509 TBSCertificate. */
typedef RTCRX509TBSCERTIFICATE const *PCRTCRX509TBSCERTIFICATE;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509TBSCERTIFICATE, RTDECL, RTCrX509TbsCertificate, SeqCore.Asn1Core);

/** @name RTCRX509TBSCERTIFICATE::T0.Version values.
 * @{ */
#define RTCRX509TBSCERTIFICATE_V1   0
#define RTCRX509TBSCERTIFICATE_V2   1
#define RTCRX509TBSCERTIFICATE_V3   2
/** @} */

/** @name RTCRX509TBSCERTIFICATE::T3.fFlags values.
 * @{ */
#define RTCRX509TBSCERTIFICATE_F_PRESENT_KEY_USAGE                      RT_BIT_32(0)
#define RTCRX509TBSCERTIFICATE_F_PRESENT_EXT_KEY_USAGE                  RT_BIT_32(1)
#define RTCRX509TBSCERTIFICATE_F_PRESENT_SUBJECT_KEY_IDENTIFIER         RT_BIT_32(2)
#define RTCRX509TBSCERTIFICATE_F_PRESENT_SUBJECT_ALT_NAME               RT_BIT_32(3)
#define RTCRX509TBSCERTIFICATE_F_PRESENT_ISSUER_ALT_NAME                RT_BIT_32(4)
#define RTCRX509TBSCERTIFICATE_F_PRESENT_CERTIFICATE_POLICIES           RT_BIT_32(5)
#define RTCRX509TBSCERTIFICATE_F_PRESENT_POLICY_MAPPINGS                RT_BIT_32(6)
#define RTCRX509TBSCERTIFICATE_F_PRESENT_BASIC_CONSTRAINTS              RT_BIT_32(7)
#define RTCRX509TBSCERTIFICATE_F_PRESENT_NAME_CONSTRAINTS               RT_BIT_32(8)
#define RTCRX509TBSCERTIFICATE_F_PRESENT_POLICY_CONSTRAINTS             RT_BIT_32(9)
#define RTCRX509TBSCERTIFICATE_F_PRESENT_AUTHORITY_KEY_IDENTIFIER       RT_BIT_32(10)
#define RTCRX509TBSCERTIFICATE_F_PRESENT_OLD_AUTHORITY_KEY_IDENTIFIER   RT_BIT_32(11)
#define RTCRX509TBSCERTIFICATE_F_PRESENT_ACCEPTABLE_CERT_POLICIES       RT_BIT_32(12)
#define RTCRX509TBSCERTIFICATE_F_PRESENT_INHIBIT_ANY_POLICY             RT_BIT_32(13)
#define RTCRX509TBSCERTIFICATE_F_PRESENT_OTHER                          RT_BIT_32(22) /**< Other unknown extension present. */
#define RTCRX509TBSCERTIFICATE_F_PRESENT_NONE                           RT_BIT_32(23) /**< No extensions present. */
/** @} */

/** @name X.509 Key Usage flags. (RFC-5280 section 4.2.1.3.)
 * @{ */
#define RTCRX509CERT_KEY_USAGE_F_DIGITAL_SIGNATURE_BIT      0
#define RTCRX509CERT_KEY_USAGE_F_DIGITAL_SIGNATURE          RT_BIT_32(0)
#define RTCRX509CERT_KEY_USAGE_F_CONTENT_COMMITTMENT_BIT    1
#define RTCRX509CERT_KEY_USAGE_F_CONTENT_COMMITTMENT        RT_BIT_32(1)
#define RTCRX509CERT_KEY_USAGE_F_KEY_ENCIPHERMENT_BIT       2
#define RTCRX509CERT_KEY_USAGE_F_KEY_ENCIPHERMENT           RT_BIT_32(2)
#define RTCRX509CERT_KEY_USAGE_F_DATA_ENCIPHERMENT_BIT      3
#define RTCRX509CERT_KEY_USAGE_F_DATA_ENCIPHERMENT          RT_BIT_32(3)
#define RTCRX509CERT_KEY_USAGE_F_KEY_AGREEMENT_BIT          4
#define RTCRX509CERT_KEY_USAGE_F_KEY_AGREEMENT              RT_BIT_32(4)
#define RTCRX509CERT_KEY_USAGE_F_KEY_CERT_SIGN_BIT          5
#define RTCRX509CERT_KEY_USAGE_F_KEY_CERT_SIGN              RT_BIT_32(5)
#define RTCRX509CERT_KEY_USAGE_F_CRL_SIGN_BIT               6
#define RTCRX509CERT_KEY_USAGE_F_CRL_SIGN                   RT_BIT_32(6)
#define RTCRX509CERT_KEY_USAGE_F_ENCIPHERMENT_ONLY_BIT      7
#define RTCRX509CERT_KEY_USAGE_F_ENCIPHERMENT_ONLY          RT_BIT_32(7)
#define RTCRX509CERT_KEY_USAGE_F_DECIPHERMENT_ONLY_BIT      8
#define RTCRX509CERT_KEY_USAGE_F_DECIPHERMENT_ONLY          RT_BIT_32(8)
/** @} */

/** @name X.509 Extended Key Usage flags. (RFC-5280 section 4.2.1.12, ++.)
 * @remarks Needless to say, these flags doesn't cover all possible extended key
 *          usages, because there is a potential unlimited number of them.  Only
 *          ones relevant to IPRT and it's users are covered.
 * @{ */
#define RTCRX509CERT_EKU_F_ANY                              RT_BIT_64(0)
#define RTCRX509CERT_EKU_F_SERVER_AUTH                      RT_BIT_64(1)
#define RTCRX509CERT_EKU_F_CLIENT_AUTH                      RT_BIT_64(2)
#define RTCRX509CERT_EKU_F_CODE_SIGNING                     RT_BIT_64(3)
#define RTCRX509CERT_EKU_F_EMAIL_PROTECTION                 RT_BIT_64(4)
#define RTCRX509CERT_EKU_F_IPSEC_END_SYSTEM                 RT_BIT_64(5)
#define RTCRX509CERT_EKU_F_IPSEC_TUNNEL                     RT_BIT_64(6)
#define RTCRX509CERT_EKU_F_IPSEC_USER                       RT_BIT_64(7)
#define RTCRX509CERT_EKU_F_TIMESTAMPING                     RT_BIT_64(8)
#define RTCRX509CERT_EKU_F_OCSP_SIGNING                     RT_BIT_64(9)
#define RTCRX509CERT_EKU_F_DVCS                             RT_BIT_64(10)
#define RTCRX509CERT_EKU_F_SBGP_CERT_AA_SERVICE_AUTH        RT_BIT_64(11)
#define RTCRX509CERT_EKU_F_EAP_OVER_PPP                     RT_BIT_64(12)
#define RTCRX509CERT_EKU_F_EAP_OVER_LAN                     RT_BIT_64(13)
#define RTCRX509CERT_EKU_F_OTHER                            RT_BIT_64(16) /**< Other unknown extended key usage present. */
#define RTCRX509CERT_EKU_F_APPLE_CODE_SIGNING               RT_BIT_64(24)
#define RTCRX509CERT_EKU_F_APPLE_CODE_SIGNING_DEVELOPMENT   RT_BIT_64(25)
#define RTCRX509CERT_EKU_F_APPLE_SOFTWARE_UPDATE_SIGNING    RT_BIT_64(26)
#define RTCRX509CERT_EKU_F_APPLE_CODE_SIGNING_THIRD_PARTY   RT_BIT_64(27)
#define RTCRX509CERT_EKU_F_APPLE_RESOURCE_SIGNING           RT_BIT_64(28)
#define RTCRX509CERT_EKU_F_APPLE_SYSTEM_IDENTITY            RT_BIT_64(29)
#define RTCRX509CERT_EKU_F_MS_TIMESTAMP_SIGNING             RT_BIT_64(32)
#define RTCRX509CERT_EKU_F_MS_NT5_CRYPTO                    RT_BIT_64(33)
#define RTCRX509CERT_EKU_F_MS_OEM_WHQL_CRYPTO               RT_BIT_64(34)
#define RTCRX509CERT_EKU_F_MS_EMBEDDED_NT_CRYPTO            RT_BIT_64(35)
#define RTCRX509CERT_EKU_F_MS_KERNEL_MODE_CODE_SIGNING      RT_BIT_64(36)
#define RTCRX509CERT_EKU_F_MS_LIFETIME_SIGNING              RT_BIT_64(37)
#define RTCRX509CERT_EKU_F_MS_DRM                           RT_BIT_64(38)
#define RTCRX509CERT_EKU_F_MS_DRM_INDIVIDUALIZATION         RT_BIT_64(39)
#define RTCRX509CERT_EKU_F_MS_WHQL_CRYPTO                   RT_BIT_64(40)
#define RTCRX509CERT_EKU_F_MS_ATTEST_WHQL_CRYPTO            RT_BIT_64(41)
/** @} */

/** @name Key purpose OIDs (extKeyUsage)
 * @{ */
#define RTCRX509_ANY_EXTENDED_KEY_USAGE_OID                 "2.5.29.37.0"
#define RTCRX509_ID_KP_OID                                  "1.3.6.1.5.5.7.3"
#define RTCRX509_ID_KP_SERVER_AUTH_OID                      "1.3.6.1.5.5.7.3.1"
#define RTCRX509_ID_KP_CLIENT_AUTH_OID                      "1.3.6.1.5.5.7.3.2"
#define RTCRX509_ID_KP_CODE_SIGNING_OID                     "1.3.6.1.5.5.7.3.3"
#define RTCRX509_ID_KP_EMAIL_PROTECTION_OID                 "1.3.6.1.5.5.7.3.4"
#define RTCRX509_ID_KP_IPSEC_END_SYSTEM_OID                 "1.3.6.1.5.5.7.3.5"
#define RTCRX509_ID_KP_IPSEC_TUNNEL_OID                     "1.3.6.1.5.5.7.3.6"
#define RTCRX509_ID_KP_IPSEC_USER_OID                       "1.3.6.1.5.5.7.3.7"
#define RTCRX509_ID_KP_TIMESTAMPING_OID                     "1.3.6.1.5.5.7.3.8"
#define RTCRX509_ID_KP_OCSP_SIGNING_OID                     "1.3.6.1.5.5.7.3.9"
#define RTCRX509_ID_KP_DVCS_OID                             "1.3.6.1.5.5.7.3.10"
#define RTCRX509_ID_KP_SBGP_CERT_AA_SERVICE_AUTH_OID        "1.3.6.1.5.5.7.3.11"
#define RTCRX509_ID_KP_EAP_OVER_PPP_OID                     "1.3.6.1.5.5.7.3.13"
#define RTCRX509_ID_KP_EAP_OVER_LAN_OID                     "1.3.6.1.5.5.7.3.14"
/** @} */

/** @name Microsoft extended key usage OIDs
 * @{ */
#define RTCRX509_MS_EKU_CERT_TRUST_LIST_SIGNING_OID         "1.3.6.1.4.1.311.10.3.1"
#define RTCRX509_MS_EKU_TIMESTAMP_SIGNING_OID               "1.3.6.1.4.1.311.10.3.2"
#define RTCRX509_MS_EKU_SERVER_GATED_CRYPTO_OID             "1.3.6.1.4.1.311.10.3.3"
#define RTCRX509_MS_EKU_SGC_SERIALIZED_OID                  "1.3.6.1.4.1.311.10.3.3.1"
#define RTCRX509_MS_EKU_ENCRYPTED_FILE_SYSTEM_OID           "1.3.6.1.4.1.311.10.3.4"
#define RTCRX509_MS_EKU_WHQL_CRYPTO_OID                     "1.3.6.1.4.1.311.10.3.5"
#define RTCRX509_MS_EKU_ATTEST_WHQL_CRYPTO_OID              "1.3.6.1.4.1.311.10.3.5.1"
#define RTCRX509_MS_EKU_NT5_CRYPTO_OID                      "1.3.6.1.4.1.311.10.3.6"
#define RTCRX509_MS_EKU_OEM_WHQL_CRYPTO_OID                 "1.3.6.1.4.1.311.10.3.7"
#define RTCRX509_MS_EKU_EMBEDDED_NT_CRYPTO_OID              "1.3.6.1.4.1.311.10.3.8"
#define RTCRX509_MS_EKU_ROOT_LIST_SIGNER_OID                "1.3.6.1.4.1.311.10.3.9"
#define RTCRX509_MS_EKU_QUALIFIED_SUBORDINATE_OID           "1.3.6.1.4.1.311.10.3.10"
#define RTCRX509_MS_EKU_KEY_RECOVERY_3_OID                  "1.3.6.1.4.1.311.10.3.11"
#define RTCRX509_MS_EKU_DOCUMENT_SIGNING_OID                "1.3.6.1.4.1.311.10.3.12"
#define RTCRX509_MS_EKU_LIFETIME_SIGNING_OID                "1.3.6.1.4.1.311.10.3.13"
#define RTCRX509_MS_EKU_MOBILE_DEVICE_SOFTWARE_OID          "1.3.6.1.4.1.311.10.3.14"
#define RTCRX509_MS_EKU_SMART_DISPLAY_OID                   "1.3.6.1.4.1.311.10.3.15"
#define RTCRX509_MS_EKU_CSP_SIGNATURE_OID                   "1.3.6.1.4.1.311.10.3.16"
#define RTCRX509_MS_EKU_EFS_RECOVERY_OID                    "1.3.6.1.4.1.311.10.3.4.1"
#define RTCRX509_MS_EKU_DRM_OID                             "1.3.6.1.4.1.311.10.5.1"
#define RTCRX509_MS_EKU_DRM_INDIVIDUALIZATION_OID           "1.3.6.1.4.1.311.10.5.2"
#define RTCRX509_MS_EKU_LICENSES_OID                        "1.3.6.1.4.1.311.10.5.3"
#define RTCRX509_MS_EKU_LICENSE_SERVER_OID                  "1.3.6.1.4.1.311.10.5.4"
#define RTCRX509_MS_EKU_ENROLLMENT_AGENT_OID                "1.3.6.1.4.1.311.20.2.1"
#define RTCRX509_MS_EKU_SMARTCARD_LOGON_OID                 "1.3.6.1.4.1.311.20.2.2"
#define RTCRX509_MS_EKU_CA_EXCHANGE_OID                     "1.3.6.1.4.1.311.21.5"
#define RTCRX509_MS_EKU_KEY_RECOVERY_21_OID                 "1.3.6.1.4.1.311.21.6"
#define RTCRX509_MS_EKU_SYSTEM_HEALTH_OID                   "1.3.6.1.4.1.311.47.1.1"
#define RTCRX509_MS_EKU_SYSTEM_HEALTH_LOOPHOLE_OID          "1.3.6.1.4.1.311.47.1.3"
#define RTCRX509_MS_EKU_KERNEL_MODE_CODE_SIGNING_OID        "1.3.6.1.4.1.311.61.1.1"
/** @} */

/** @name Apple extended key usage OIDs
 * @{ */
#define RTCRX509_APPLE_EKU_APPLE_EXTENDED_KEY_USAGE_OID     "1.2.840.113635.100.4"
#define RTCRX509_APPLE_EKU_CODE_SIGNING_OID                 "1.2.840.113635.100.4.1"
#define RTCRX509_APPLE_EKU_CODE_SIGNING_DEVELOPMENT_OID     "1.2.840.113635.100.4.1.1"
#define RTCRX509_APPLE_EKU_SOFTWARE_UPDATE_SIGNING_OID      "1.2.840.113635.100.4.1.2"
#define RTCRX509_APPLE_EKU_CODE_SIGNING_THRID_PARTY_OID     "1.2.840.113635.100.4.1.3"
#define RTCRX509_APPLE_EKU_RESOURCE_SIGNING_OID             "1.2.840.113635.100.4.1.4"
#define RTCRX509_APPLE_EKU_ICHAT_SIGNING_OID                "1.2.840.113635.100.4.2"
#define RTCRX509_APPLE_EKU_ICHAT_ENCRYPTION_OID             "1.2.840.113635.100.4.3"
#define RTCRX509_APPLE_EKU_SYSTEM_IDENTITY_OID              "1.2.840.113635.100.4.4"
#define RTCRX509_APPLE_EKU_CRYPTO_ENV_OID                   "1.2.840.113635.100.4.5"
#define RTCRX509_APPLE_EKU_CRYPTO_PRODUCTION_ENV_OID        "1.2.840.113635.100.4.5.1"
#define RTCRX509_APPLE_EKU_CRYPTO_MAINTENANCE_ENV_OID       "1.2.840.113635.100.4.5.2"
#define RTCRX509_APPLE_EKU_CRYPTO_TEST_ENV_OID              "1.2.840.113635.100.4.5.3"
#define RTCRX509_APPLE_EKU_CRYPTO_DEVELOPMENT_ENV_OID       "1.2.840.113635.100.4.5.4"
#define RTCRX509_APPLE_EKU_CRYPTO_QOS_OID                   "1.2.840.113635.100.4.6"
#define RTCRX509_APPLE_EKU_CRYPTO_TIER0_QOS_OID             "1.2.840.113635.100.4.6.1"
#define RTCRX509_APPLE_EKU_CRYPTO_TIER1_QOS_OID             "1.2.840.113635.100.4.6.2"
#define RTCRX509_APPLE_EKU_CRYPTO_TIER2_QOS_OID             "1.2.840.113635.100.4.6.3"
#define RTCRX509_APPLE_EKU_CRYPTO_TIER3_QOS_OID             "1.2.840.113635.100.4.6.4"
/** @} */

/**
 * Use this to update derived values after changing the certificate
 * extensions.
 *
 * @returns IPRT status code
 * @param   pThis       The certificate.
 * @param   pErrInfo    Where to return additional error information. Optional.
 */
RTDECL(int) RTCrX509TbsCertificate_ReprocessExtensions(PRTCRX509TBSCERTIFICATE pThis, PRTERRINFO pErrInfo);


/**
 * One X.509 Certificate (IPRT representation).
 */
typedef struct RTCRX509CERTIFICATE
{
    /** Sequence core. */
    RTASN1SEQUENCECORE                  SeqCore;
    /** The to-be-signed certificate information. */
    RTCRX509TBSCERTIFICATE              TbsCertificate;
    /** The signature algorithm (must match TbsCertificate.Signature). */
    RTCRX509ALGORITHMIDENTIFIER         SignatureAlgorithm;
    /** The signature value. */
    RTASN1BITSTRING                     SignatureValue;
} RTCRX509CERTIFICATE;
/** Pointer to the IPRT representation of one X.509 certificate. */
typedef RTCRX509CERTIFICATE *PRTCRX509CERTIFICATE;
/** Pointer to the const IPRT representation of one X.509 certificate. */
typedef RTCRX509CERTIFICATE const *PCRTCRX509CERTIFICATE;
RTASN1TYPE_STANDARD_PROTOTYPES(RTCRX509CERTIFICATE, RTDECL, RTCrX509Certificate, SeqCore.Asn1Core);

/**
 * Checks if a certificate matches a given issuer name and serial number.
 *
 * @returns True / false.
 * @param   pCertificate    The X.509 certificat.
 * @param   pIssuer         The issuer name to match against.
 * @param   pSerialNumber   The serial number to match against.
 */
RTDECL(bool) RTCrX509Certificate_MatchIssuerAndSerialNumber(PCRTCRX509CERTIFICATE pCertificate,
                                                            PCRTCRX509NAME pIssuer, PCRTASN1INTEGER pSerialNumber);

RTDECL(bool) RTCrX509Certificate_MatchSubjectOrAltSubjectByRfc5280(PCRTCRX509CERTIFICATE pThis, PCRTCRX509NAME pName);
RTDECL(bool) RTCrX509Certificate_IsSelfSigned(PCRTCRX509CERTIFICATE pCertificate);

RTDECL(int) RTCrX509Certificate_VerifySignature(PCRTCRX509CERTIFICATE pThis, PCRTASN1OBJID pAlgorithm,
                                                PCRTASN1DYNTYPE pParameters, PCRTASN1BITSTRING pPublicKey,
                                                PRTERRINFO pErrInfo);
RTDECL(int) RTCrX509Certificate_VerifySignatureSelfSigned(PCRTCRX509CERTIFICATE pThis, PRTERRINFO pErrInfo);
RTDECL(int) RTCrX509Certificate_ReadFromFile(PRTCRX509CERTIFICATE pCertificate, const char *pszFilename, uint32_t fFlags,
                                             PCRTASN1ALLOCATORVTABLE pAllocator, PRTERRINFO pErrInfo);
RTDECL(int) RTCrX509Certificate_ReadFromBuffer(PRTCRX509CERTIFICATE pCertificate, const void *pvBuf, size_t cbBuf,
                                               uint32_t fFlags, PCRTASN1ALLOCATORVTABLE pAllocator,
                                               PRTERRINFO pErrInfo, const char *pszErrorTag);
/** @name Flags for RTCrX509Certificate_ReadFromFile and
 *        RTCrX509Certificate_ReadFromBuffer
 * @{ */
/** Only allow PEM certificates, not binary ones.
 * @sa RTCRPEMREADFILE_F_ONLY_PEM  */
#define RTCRX509CERT_READ_F_PEM_ONLY        RT_BIT(1)
/** @} */

/** X509 Certificate markers for RTCrPemFindFirstSectionInContent et al. */
extern RTDATADECL(RTCRPEMMARKER const)  g_aRTCrX509CertificateMarkers[];
/** Number of entries in g_aRTCrX509CertificateMarkers. */
extern RTDATADECL(uint32_t const)       g_cRTCrX509CertificateMarkers;


/** Wrapper around RTCrPemWriteAsn1ToVfsIoStrm().  */
DECLINLINE(ssize_t) RTCrX509Certificate_WriteToVfsIoStrm(RTVFSIOSTREAM hVfsIos, PRTCRX509CERTIFICATE pCertificate,
                                                         PRTERRINFO pErrInfo)
{
    return RTCrPemWriteAsn1ToVfsIoStrm(hVfsIos, &pCertificate->SeqCore.Asn1Core, 0 /*fFlags*/,
                                       g_aRTCrX509CertificateMarkers[0].paWords[0].pszWord, pErrInfo);
}

/** Wrapper around RTCrPemWriteAsn1ToVfsFile().  */
DECLINLINE(ssize_t) RTCrX509Certificate_WriteToVfsFile(RTVFSFILE hVfsFile, PRTCRX509CERTIFICATE pCertificate,
                                                       PRTERRINFO pErrInfo)
{
    return RTCrPemWriteAsn1ToVfsFile(hVfsFile, &pCertificate->SeqCore.Asn1Core, 0 /*fFlags*/,
                                     g_aRTCrX509CertificateMarkers[0].paWords[0].pszWord, pErrInfo);
}

/** @name X.509 Certificate Extensions
 * @{ */
/** Old AuthorityKeyIdentifier OID. */
#define RTCRX509_ID_CE_OLD_AUTHORITY_KEY_IDENTIFIER_OID         "2.5.29.1"
/** Old CertificatePolicies extension OID. */
#define RTCRX509_ID_CE_OLD_CERTIFICATE_POLICIES_OID             "2.5.29.3"
/** Old SubjectAltName extension OID. */
#define RTCRX509_ID_CE_OLD_SUBJECT_ALT_NAME_OID                 "2.5.29.7"
/** Old IssuerAltName extension OID. */
#define RTCRX509_ID_CE_OLD_ISSUER_ALT_NAME_OID                  "2.5.29.8"
/** Old BasicContraints extension OID. */
#define RTCRX509_ID_CE_OLD_BASIC_CONSTRAINTS_OID                "2.5.29.10"
/** SubjectKeyIdentifier OID. */
#define RTCRX509_ID_CE_SUBJECT_KEY_IDENTIFIER_OID               "2.5.29.14"
/** KeyUsage OID. */
#define RTCRX509_ID_CE_KEY_USAGE_OID                            "2.5.29.15"
/** PrivateKeyUsagePeriod OID. */
#define RTCRX509_ID_CE_PRIVATE_KEY_USAGE_PERIOD_OID             "2.5.29.16"
/** SubjectAltName extension OID. */
#define RTCRX509_ID_CE_SUBJECT_ALT_NAME_OID                     "2.5.29.17"
/** IssuerAltName extension OID. */
#define RTCRX509_ID_CE_ISSUER_ALT_NAME_OID                      "2.5.29.18"
/** BasicContraints extension OID. */
#define RTCRX509_ID_CE_BASIC_CONSTRAINTS_OID                    "2.5.29.19"
/** NameContraints extension OID. */
#define RTCRX509_ID_CE_NAME_CONSTRAINTS_OID                     "2.5.29.30"
/** CertificatePolicies extension OID. */
#define RTCRX509_ID_CE_CERTIFICATE_POLICIES_OID                 "2.5.29.32"
/** PolicyMappings extension OID. */
#define RTCRX509_ID_CE_POLICY_MAPPINGS_OID                      "2.5.29.33"
/** AuthorityKeyIdentifier OID. */
#define RTCRX509_ID_CE_AUTHORITY_KEY_IDENTIFIER_OID             "2.5.29.35"
/** PolicyContraints extension OID. */
#define RTCRX509_ID_CE_POLICY_CONSTRAINTS_OID                   "2.5.29.36"
/** ExtKeyUsage (extended key usage) extension OID. */
#define RTCRX509_ID_CE_EXT_KEY_USAGE_OID                        "2.5.29.37"
/** ExtKeyUsage: OID for permitting any unspecified key usage. */
#define RTCRX509_ID_CE_ANY_EXTENDED_KEY_USAGE_OID               "2.5.29.37.0"
/** AuthorityAttributeIdentifier OID. */
#define RTCRX509_ID_CE_AUTHORITY_ATTRIBUTE_IDENTIFIER_OID       "2.5.29.38"
/** AcceptableCertPolicies OID. */
#define RTCRX509_ID_CE_ACCEPTABLE_CERT_POLICIES_OID             "2.5.29.52"
/** InhibitAnyPolicy OID. */
#define RTCRX509_ID_CE_INHIBIT_ANY_POLICY_OID                   "2.5.29.54"
/** @} */


/*
 * Sequence of X.509 Certifcates (IPRT representation).
 */
RTASN1_IMPL_GEN_SET_OF_TYPEDEFS_AND_PROTOS(RTCRX509CERTIFICATES, RTCRX509CERTIFICATE, RTDECL, RTCrX509Certificates);

/**
 * Looks up a certificate by issuer name and serial number.
 *
 * @returns Pointer to the given certificate if found, NULL if not.
 * @param   pCertificates   The X.509 certificate set to search.
 * @param   pIssuer         The issuer name of the wanted certificate.
 * @param   pSerialNumber   The serial number of the wanted certificate.
 */
RTDECL(PCRTCRX509CERTIFICATE) RTCrX509Certificates_FindByIssuerAndSerialNumber(PCRTCRX509CERTIFICATES pCertificates,
                                                                               PCRTCRX509NAME pIssuer,
                                                                               PCRTASN1INTEGER pSerialNumber);



RTDECL(int) RTCrX509CertPathsCreate(PRTCRX509CERTPATHS phCertPaths, PCRTCRX509CERTIFICATE pTarget);
RTDECL(uint32_t) RTCrX509CertPathsRetain(RTCRX509CERTPATHS hCertPaths);
RTDECL(uint32_t) RTCrX509CertPathsRelease(RTCRX509CERTPATHS hCertPaths);
RTDECL(int) RTCrX509CertPathsSetTrustedStore(RTCRX509CERTPATHS hCertPaths, RTCRSTORE hTrustedStore);
RTDECL(int) RTCrX509CertPathsSetUntrustedStore(RTCRX509CERTPATHS hCertPaths, RTCRSTORE hUntrustedStore);
RTDECL(int) RTCrX509CertPathsSetUntrustedArray(RTCRX509CERTPATHS hCertPaths, PCRTCRX509CERTIFICATE paCerts, uint32_t cCerts);
RTDECL(int) RTCrX509CertPathsSetUntrustedSet(RTCRX509CERTPATHS hCertPaths, struct RTCRPKCS7SETOFCERTS const *pSetOfCerts);
RTDECL(int) RTCrX509CertPathsSetValidTime(RTCRX509CERTPATHS hCertPaths, PCRTTIME pTime);
RTDECL(int) RTCrX509CertPathsSetValidTimeSpec(RTCRX509CERTPATHS hCertPaths, PCRTTIMESPEC pTimeSpec);
RTDECL(int) RTCrX509CertPathsSetTrustAnchorChecks(RTCRX509CERTPATHS hCertPaths, bool fEnable);
RTDECL(int) RTCrX509CertPathsCreateEx(PRTCRX509CERTPATHS phCertPaths, PCRTCRX509CERTIFICATE pTarget, RTCRSTORE hTrustedStore,
                                      RTCRSTORE hUntrustedStore, PCRTCRX509CERTIFICATE paUntrustedCerts, uint32_t cUntrustedCerts,
                                      PCRTTIMESPEC pValidTime);
RTDECL(int) RTCrX509CertPathsBuild(RTCRX509CERTPATHS hCertPaths, PRTERRINFO pErrInfo);
RTDECL(int) RTCrX509CertPathsDumpOne(RTCRX509CERTPATHS hCertPaths, uint32_t iPath, uint32_t uVerbosity,
                                     PFNRTDUMPPRINTFV pfnPrintfV, void *pvUser);
RTDECL(int) RTCrX509CertPathsDumpAll(RTCRX509CERTPATHS hCertPaths, uint32_t uVerbosity,
                                     PFNRTDUMPPRINTFV pfnPrintfV, void *pvUser);

RTDECL(int) RTCrX509CertPathsValidateOne(RTCRX509CERTPATHS hCertPaths, uint32_t iPath, PRTERRINFO pErrInfo);
RTDECL(int) RTCrX509CertPathsValidateAll(RTCRX509CERTPATHS hCertPaths, uint32_t *pcValidPaths, PRTERRINFO pErrInfo);

RTDECL(uint32_t) RTCrX509CertPathsGetPathCount(RTCRX509CERTPATHS hCertPaths);
RTDECL(int) RTCrX509CertPathsQueryPathInfo(RTCRX509CERTPATHS hCertPaths, uint32_t iPath,
                                           bool *pfTrusted, uint32_t *pcNodes, PCRTCRX509NAME *ppSubject,
                                           PCRTCRX509SUBJECTPUBLICKEYINFO *ppPublicKeyInfo,
                                           PCRTCRX509CERTIFICATE *ppCert, PCRTCRCERTCTX *ppCertCtx, int *prcVerify);
RTDECL(uint32_t) RTCrX509CertPathsGetPathLength(RTCRX509CERTPATHS hCertPaths, uint32_t iPath);
RTDECL(int) RTCrX509CertPathsGetPathVerifyResult(RTCRX509CERTPATHS hCertPaths, uint32_t iPath);
RTDECL(PCRTCRX509CERTIFICATE) RTCrX509CertPathsGetPathNodeCert(RTCRX509CERTPATHS hCertPaths, uint32_t iPath, uint32_t iNode);

/**
 * Generates a self-signed RSA certificate, storing the result as two file.
 *
 * @note    The private key will be stored unencrypted!
 *
 * @returns IPRT status code.
 * @param   enmDigestType       The digest type to use when signing.
 * @param   cBits               The private key size (in bits).
 * @param   cSecsValidFor       Number of seconds the certificate should be
 *                              valid for (starting now).
 * @param   fKeyUsage           Key usage mask: RTCRX509CERT_KEY_USAGE_F_XXX.
 * @param   fExtKeyUsage        Extended key usage mask: RTCRX509CERT_EKU_F_XXX.
 * @param   pszSubject          Subject name.
 * @param   pszCertFile         Where to store the certificate (PEM formatting).
 * @param   pszPrivateKeyFile   Where to store the unencrypted private key (PEM
 *                              formatting).
 * @param   pErrInfo            Where to return extended error information.
 *                              Optional.
 */
RTDECL(int) RTCrX509Certificate_GenerateSelfSignedRsa(RTDIGESTTYPE enmDigestType, uint32_t cBits, uint32_t cSecsValidFor,
                                                      uint32_t fKeyUsage, uint64_t fExtKeyUsage, const char *pszSubject,
                                                      const char *pszCertFile, const char *pszPrivateKeyFile, PRTERRINFO pErrInfo);

RT_C_DECLS_END

/** @} */

/** @} */

#endif /* !IPRT_INCLUDED_crypto_x509_h */
