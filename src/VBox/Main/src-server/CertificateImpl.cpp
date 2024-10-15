/* $Id: CertificateImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * ICertificate COM class implementations.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define LOG_GROUP LOG_GROUP_MAIN_CERTIFICATE
#include <iprt/err.h>
#include <iprt/path.h>
#include <iprt/cpp/utils.h>
#include <VBox/com/array.h>
#include <iprt/crypto/x509.h>

#include "ProgressImpl.h"
#include "CertificateImpl.h"
#include "AutoCaller.h"
#include "Global.h"
#include "LoggingNew.h"

using namespace std;


/**
 * Private instance data for the #Certificate class.
 * @see Certificate::m
 */
struct Certificate::Data
{
    Data()
        : fTrusted(false)
        , fExpired(false)
        , fValidX509(false)
    {
        RT_ZERO(X509);
    }

    ~Data()
    {
        if (fValidX509)
        {
            RTCrX509Certificate_Delete(&X509);
            RT_ZERO(X509);
            fValidX509 = false;
        }
    }

    /** Whether the certificate is trusted.  */
    bool fTrusted;
    /** Whether the certificate is trusted.  */
    bool fExpired;
    /** Valid data in mX509. */
    bool fValidX509;
    /** Clone of the X.509 certificate. */
    RTCRX509CERTIFICATE X509;

private:
    Data(const Certificate::Data &rTodo) { AssertFailed(); NOREF(rTodo); }
    Data &operator=(const Certificate::Data &rTodo) { AssertFailed(); NOREF(rTodo); return *this; }
};


///////////////////////////////////////////////////////////////////////////////////
//
// Certificate constructor / destructor
//
// ////////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(Certificate)

HRESULT Certificate::FinalConstruct()
{
    return BaseFinalConstruct();
}

void Certificate::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

/**
 * Initializes a certificate instance.
 *
 * @returns COM status code.
 * @param   a_pCert         The certificate.
 * @param   a_fTrusted      Whether the caller trusts the certificate or not.
 * @param   a_fExpired      Whether the caller consideres the certificate to be
 *                          expired.
 */
HRESULT Certificate::initCertificate(PCRTCRX509CERTIFICATE a_pCert, bool a_fTrusted, bool a_fExpired)
{
    HRESULT hrc = S_OK;
    LogFlowThisFuncEnter();

    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    int vrc = RTCrX509Certificate_Clone(&m->X509, a_pCert, &g_RTAsn1DefaultAllocator);
    if (RT_SUCCESS(vrc))
    {
        m->fValidX509 = true;
        m->fTrusted   = a_fTrusted;
        m->fExpired   = a_fExpired;
        autoInitSpan.setSucceeded();
    }
    else
        hrc = Global::vboxStatusCodeToCOM(vrc);

    LogFlowThisFunc(("returns hrc=%Rhrc\n", hrc));
    return hrc;
}

void Certificate::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    delete m;
    m = NULL;
}


/** @name Wrapped ICertificate properties
 * @{
 */

HRESULT Certificate::getVersionNumber(CertificateVersion_T *aVersionNumber)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Assert(m->fValidX509);
    switch (m->X509.TbsCertificate.T0.Version.uValue.u)
    {
        case RTCRX509TBSCERTIFICATE_V1: *aVersionNumber = CertificateVersion_V1; break;
        case RTCRX509TBSCERTIFICATE_V2: *aVersionNumber = CertificateVersion_V2; break;
        case RTCRX509TBSCERTIFICATE_V3: *aVersionNumber = CertificateVersion_V3; break;
        default: AssertFailed();        *aVersionNumber = CertificateVersion_Unknown; break;
    }
    return S_OK;
}

HRESULT Certificate::getSerialNumber(com::Utf8Str &aSerialNumber)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Assert(m->fValidX509);

    char szTmp[_2K];
    int vrc = RTAsn1Integer_ToString(&m->X509.TbsCertificate.SerialNumber, szTmp, sizeof(szTmp), 0, NULL);
    if (RT_SUCCESS(vrc))
        aSerialNumber = szTmp;
    else
        return Global::vboxStatusCodeToCOM(vrc);

    return S_OK;
}

HRESULT Certificate::getSignatureAlgorithmOID(com::Utf8Str &aSignatureAlgorithmOID)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Assert(m->fValidX509);
    aSignatureAlgorithmOID = m->X509.TbsCertificate.Signature.Algorithm.szObjId;

    return S_OK;
}

HRESULT Certificate::getSignatureAlgorithmName(com::Utf8Str &aSignatureAlgorithmName)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Assert(m->fValidX509);
    return i_getAlgorithmName(&m->X509.TbsCertificate.Signature, aSignatureAlgorithmName);
}

HRESULT Certificate::getIssuerName(std::vector<com::Utf8Str> &aIssuerName)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Assert(m->fValidX509);
    return i_getX509Name(&m->X509.TbsCertificate.Issuer, aIssuerName);
}

HRESULT Certificate::getSubjectName(std::vector<com::Utf8Str> &aSubjectName)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Assert(m->fValidX509);
    return i_getX509Name(&m->X509.TbsCertificate.Subject, aSubjectName);
}

HRESULT Certificate::getFriendlyName(com::Utf8Str &aFriendlyName)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Assert(m->fValidX509);

    PCRTCRX509NAME pName = &m->X509.TbsCertificate.Subject;

    /*
     * Enumerate the subject name and pick interesting attributes we can use to
     * form a name more friendly than the RTCrX509Name_FormatAsString output.
     */
    const char *pszOrg       = NULL;
    const char *pszOrgUnit   = NULL;
    const char *pszGivenName = NULL;
    const char *pszSurname   = NULL;
    const char *pszEmail     = NULL;
    for (uint32_t i = 0; i < pName->cItems; i++)
    {
        PCRTCRX509RELATIVEDISTINGUISHEDNAME pRdn = pName->papItems[i];
        for (uint32_t j = 0; j < pRdn->cItems; j++)
        {
            PCRTCRX509ATTRIBUTETYPEANDVALUE pComponent = pRdn->papItems[j];
            AssertContinue(pComponent->Value.enmType == RTASN1TYPE_STRING);

            /* Select interesting components based on the short RDN prefix
               string (easier to read and write than OIDs, for now). */
            const char *pszPrefix = RTCrX509Name_GetShortRdn(&pComponent->Type);
            if (pszPrefix)
            {
                const char *pszUtf8;
                int vrc = RTAsn1String_QueryUtf8(&pComponent->Value.u.String, &pszUtf8, NULL);
                if (RT_SUCCESS(vrc) && *pszUtf8)
                {
                    if (!strcmp(pszPrefix, "Email"))
                        pszEmail = pszUtf8;
                    else if (!strcmp(pszPrefix, "O"))
                        pszOrg = pszUtf8;
                    else if (!strcmp(pszPrefix, "OU"))
                        pszOrgUnit = pszUtf8;
                    else if (!strcmp(pszPrefix, "S"))
                        pszSurname = pszUtf8;
                    else if (!strcmp(pszPrefix, "G"))
                        pszGivenName = pszUtf8;
                }
            }
        }
    }

    if (pszGivenName && pszSurname)
    {
        if (pszEmail)
            aFriendlyName = Utf8StrFmt("%s, %s <%s>", pszSurname, pszGivenName, pszEmail);
        else if (pszOrg)
            aFriendlyName = Utf8StrFmt("%s, %s (%s)", pszSurname, pszGivenName, pszOrg);
        else if (pszOrgUnit)
            aFriendlyName = Utf8StrFmt("%s, %s (%s)", pszSurname, pszGivenName, pszOrgUnit);
        else
            aFriendlyName = Utf8StrFmt("%s, %s", pszSurname, pszGivenName);
    }
    else if (pszOrg && pszOrgUnit)
        aFriendlyName = Utf8StrFmt("%s, %s", pszOrg, pszOrgUnit);
    else if (pszOrg)
        aFriendlyName = Utf8StrFmt("%s", pszOrg);
    else if (pszOrgUnit)
        aFriendlyName = Utf8StrFmt("%s", pszOrgUnit);
    else
    {
        /*
         * Fall back on unfriendly but accurate.
         */
        char szTmp[_8K];
        RT_ZERO(szTmp);
        RTCrX509Name_FormatAsString(pName, szTmp, sizeof(szTmp) - 1, NULL);
        aFriendlyName = szTmp;
    }

    return S_OK;
}

HRESULT Certificate::getValidityPeriodNotBefore(com::Utf8Str &aValidityPeriodNotBefore)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Assert(m->fValidX509);
    return i_getTime(&m->X509.TbsCertificate.Validity.NotBefore, aValidityPeriodNotBefore);
}

HRESULT Certificate::getValidityPeriodNotAfter(com::Utf8Str &aValidityPeriodNotAfter)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Assert(m->fValidX509);
    return i_getTime(&m->X509.TbsCertificate.Validity.NotAfter, aValidityPeriodNotAfter);
}

HRESULT Certificate::getPublicKeyAlgorithmOID(com::Utf8Str &aPublicKeyAlgorithmOID)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Assert(m->fValidX509);
    aPublicKeyAlgorithmOID = m->X509.TbsCertificate.SubjectPublicKeyInfo.Algorithm.Algorithm.szObjId;
    return S_OK;
}

HRESULT Certificate::getPublicKeyAlgorithm(com::Utf8Str &aPublicKeyAlgorithm)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Assert(m->fValidX509);
    return i_getAlgorithmName(&m->X509.TbsCertificate.SubjectPublicKeyInfo.Algorithm, aPublicKeyAlgorithm);
}

HRESULT Certificate::getSubjectPublicKey(std::vector<BYTE> &aSubjectPublicKey)
{

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS); /* Getting encoded ASN.1 bytes may make changes to X509. */
    return i_getEncodedBytes(&m->X509.TbsCertificate.SubjectPublicKeyInfo.SubjectPublicKey.Asn1Core, aSubjectPublicKey);
}

HRESULT Certificate::getIssuerUniqueIdentifier(com::Utf8Str &aIssuerUniqueIdentifier)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    return i_getUniqueIdentifier(&m->X509.TbsCertificate.T1.IssuerUniqueId, aIssuerUniqueIdentifier);
}

HRESULT Certificate::getSubjectUniqueIdentifier(com::Utf8Str &aSubjectUniqueIdentifier)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    return i_getUniqueIdentifier(&m->X509.TbsCertificate.T2.SubjectUniqueId, aSubjectUniqueIdentifier);
}

HRESULT Certificate::getCertificateAuthority(BOOL *aCertificateAuthority)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aCertificateAuthority = m->X509.TbsCertificate.T3.pBasicConstraints
                          && m->X509.TbsCertificate.T3.pBasicConstraints->CA.fValue;

    return S_OK;
}

HRESULT Certificate::getKeyUsage(ULONG *aKeyUsage)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aKeyUsage = m->X509.TbsCertificate.T3.fKeyUsage;
    return S_OK;
}

HRESULT Certificate::getExtendedKeyUsage(std::vector<com::Utf8Str> &aExtendedKeyUsage)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    NOREF(aExtendedKeyUsage);
    return E_NOTIMPL;
}

HRESULT Certificate::getRawCertData(std::vector<BYTE> &aRawCertData)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS); /* Getting encoded ASN.1 bytes may make changes to X509. */
    return i_getEncodedBytes(&m->X509.SeqCore.Asn1Core, aRawCertData);
}

HRESULT Certificate::getSelfSigned(BOOL *aSelfSigned)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Assert(m->fValidX509);
    *aSelfSigned = RTCrX509Certificate_IsSelfSigned(&m->X509);

    return S_OK;
}

HRESULT Certificate::getTrusted(BOOL *aTrusted)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Assert(m->fValidX509);
    *aTrusted = m->fTrusted;

    return S_OK;
}

HRESULT Certificate::getExpired(BOOL *aExpired)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    Assert(m->fValidX509);
    *aExpired = m->fExpired;
    return S_OK;
}

/** @} */

/** @name Wrapped ICertificate methods
 * @{
 */

HRESULT Certificate::isCurrentlyExpired(BOOL *aResult)
{
    AssertReturnStmt(m->fValidX509, *aResult = TRUE, E_UNEXPECTED);
    RTTIMESPEC Now;
    *aResult = RTCrX509Validity_IsValidAtTimeSpec(&m->X509.TbsCertificate.Validity, RTTimeNow(&Now)) ? FALSE : TRUE;
    return S_OK;
}

HRESULT Certificate::queryInfo(LONG aWhat, com::Utf8Str &aResult)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    /* Insurance. */
    NOREF(aResult);
    return setError(E_FAIL, tr("Unknown item %u"), aWhat);
}

/** @} */


/** @name Methods extracting COM data from the certificate object
 * @{
 */

/**
 * Translates an algorithm OID into a human readable string, if possible.
 *
 * @returns S_OK.
 * @param   a_pAlgId    The algorithm.
 * @param   a_rReturn   The return string value.
 * @throws  std::bad_alloc
 */
HRESULT Certificate::i_getAlgorithmName(PCRTCRX509ALGORITHMIDENTIFIER a_pAlgId, com::Utf8Str &a_rReturn)
{
    const char *pszOid = a_pAlgId->Algorithm.szObjId;
    const char *pszName;
    if (!pszOid)    pszName = "";
    else if (strcmp(pszOid, RTCRX509ALGORITHMIDENTIFIERID_RSA))                 pszName = "rsaEncryption";
    else if (strcmp(pszOid, RTCRX509ALGORITHMIDENTIFIERID_MD2_WITH_RSA))        pszName = "md2WithRSAEncryption";
    else if (strcmp(pszOid, RTCRX509ALGORITHMIDENTIFIERID_MD4_WITH_RSA))        pszName = "md4WithRSAEncryption";
    else if (strcmp(pszOid, RTCRX509ALGORITHMIDENTIFIERID_MD5_WITH_RSA))        pszName = "md5WithRSAEncryption";
    else if (strcmp(pszOid, RTCRX509ALGORITHMIDENTIFIERID_SHA1_WITH_RSA))       pszName = "sha1WithRSAEncryption";
    else if (strcmp(pszOid, RTCRX509ALGORITHMIDENTIFIERID_SHA224_WITH_RSA))     pszName = "sha224WithRSAEncryption";
    else if (strcmp(pszOid, RTCRX509ALGORITHMIDENTIFIERID_SHA256_WITH_RSA))     pszName = "sha256WithRSAEncryption";
    else if (strcmp(pszOid, RTCRX509ALGORITHMIDENTIFIERID_SHA384_WITH_RSA))     pszName = "sha384WithRSAEncryption";
    else if (strcmp(pszOid, RTCRX509ALGORITHMIDENTIFIERID_SHA512_WITH_RSA))     pszName = "sha512WithRSAEncryption";
    else if (strcmp(pszOid, RTCRX509ALGORITHMIDENTIFIERID_SHA512T224_WITH_RSA)) pszName = "sha512-224WithRSAEncryption";
    else if (strcmp(pszOid, RTCRX509ALGORITHMIDENTIFIERID_SHA512T256_WITH_RSA)) pszName = "sha512-256WithRSAEncryption";
    else
        pszName = pszOid;
    a_rReturn = pszName;
    return S_OK;
}

/**
 * Formats a X.509 name into a string array.
 *
 * The name is prefix with a short hand of the relative distinguished name
 * type followed by an equal sign.
 *
 * @returns S_OK.
 * @param   a_pName     The X.509 name.
 * @param   a_rReturn   The return string array.
 * @throws  std::bad_alloc
 */
HRESULT Certificate::i_getX509Name(PCRTCRX509NAME a_pName, std::vector<com::Utf8Str> &a_rReturn)
{
    if (RTCrX509Name_IsPresent(a_pName))
    {
        for (uint32_t i = 0; i < a_pName->cItems; i++)
        {
            PCRTCRX509RELATIVEDISTINGUISHEDNAME pRdn = a_pName->papItems[i];
            for (uint32_t j = 0; j < pRdn->cItems; j++)
            {
                PCRTCRX509ATTRIBUTETYPEANDVALUE pComponent = pRdn->papItems[j];

                AssertReturn(pComponent->Value.enmType == RTASN1TYPE_STRING,
                             setErrorVrc(VERR_CR_X509_NAME_NOT_STRING, "VERR_CR_X509_NAME_NOT_STRING"));

                /* Get the prefix for this name component. */
                const char *pszPrefix = RTCrX509Name_GetShortRdn(&pComponent->Type);
                AssertStmt(pszPrefix, pszPrefix = pComponent->Type.szObjId);

                /* Get the string. */
                const char *pszUtf8;
                int vrc = RTAsn1String_QueryUtf8(&pComponent->Value.u.String, &pszUtf8, NULL /*pcch*/);
                AssertRCReturn(vrc, setErrorVrc(vrc, "RTAsn1String_QueryUtf8(%u/%u,,) -> %Rrc", i, j, vrc));

                a_rReturn.push_back(Utf8StrFmt("%s=%s", pszPrefix, pszUtf8));
            }
        }
    }
    return S_OK;
}

/**
 * Translates an ASN.1 timestamp into an ISO timestamp string.
 *
 * @returns S_OK.
 * @param   a_pTime     The timestamp
 * @param   a_rReturn   The return string value.
 * @throws  std::bad_alloc
 */
HRESULT Certificate::i_getTime(PCRTASN1TIME a_pTime, com::Utf8Str &a_rReturn)
{
    char szTmp[128];
    if (RTTimeToString(&a_pTime->Time, szTmp, sizeof(szTmp)))
    {
        a_rReturn = szTmp;
        return S_OK;
    }
    AssertFailed();
    return E_FAIL;
}

/**
 * Translates a X.509 unique identifier to a string.
 *
 * @returns S_OK.
 * @param   a_pUniqueId The unique identifier.
 * @param   a_rReturn   The return string value.
 * @throws  std::bad_alloc
 */
HRESULT Certificate::i_getUniqueIdentifier(PCRTCRX509UNIQUEIDENTIFIER a_pUniqueId, com::Utf8Str &a_rReturn)
{
    /* The a_pUniqueId may not be present! */
    if (RTCrX509UniqueIdentifier_IsPresent(a_pUniqueId))
    {
        void const   *pvData = RTASN1BITSTRING_GET_BIT0_PTR(a_pUniqueId);
        size_t const  cbData = RTASN1BITSTRING_GET_BYTE_SIZE(a_pUniqueId);
        size_t const  cbFormatted = cbData * 3 - 1 + 1;
        a_rReturn.reserve(cbFormatted); /* throws */
        int vrc = RTStrPrintHexBytes(a_rReturn.mutableRaw(), cbFormatted, pvData, cbData, RTSTRPRINTHEXBYTES_F_SEP_COLON);
        a_rReturn.jolt();
        AssertRCReturn(vrc, Global::vboxStatusCodeToCOM(vrc));
    }
    else
        Assert(a_rReturn.isEmpty());
    return S_OK;
}

/**
 * Translates any ASN.1 object into a (DER encoded) byte array.
 *
 * @returns S_OK.
 * @param   a_pAsn1Obj  The ASN.1 object to get the DER encoded bytes for.
 * @param   a_rReturn   The return byte vector.
 * @throws  std::bad_alloc
 */
HRESULT Certificate::i_getEncodedBytes(PRTASN1CORE a_pAsn1Obj, std::vector<BYTE> &a_rReturn)
{
    HRESULT hrc = S_OK;
    Assert(a_rReturn.size() == 0);
    if (RTAsn1Core_IsPresent(a_pAsn1Obj))
    {
        uint32_t cbEncoded;
        int vrc = RTAsn1EncodePrepare(a_pAsn1Obj, 0, &cbEncoded, NULL);
        if (RT_SUCCESS(vrc))
        {
            a_rReturn.resize(cbEncoded);
            Assert(a_rReturn.size() == cbEncoded);
            if (cbEncoded)
            {
                vrc = RTAsn1EncodeToBuffer(a_pAsn1Obj, 0, &a_rReturn.front(), a_rReturn.size(), NULL);
                if (RT_FAILURE(vrc))
                    hrc = setErrorVrc(vrc, tr("RTAsn1EncodeToBuffer failed with %Rrc"), vrc);
            }
        }
        else
            hrc = setErrorVrc(vrc, tr("RTAsn1EncodePrepare failed with %Rrc"), vrc);
    }
    return hrc;
}

/** @} */

