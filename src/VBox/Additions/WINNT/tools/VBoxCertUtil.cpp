/* $Id: VBoxCertUtil.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxCertUtil - VBox Certificate Utility - Windows Only.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/win/windows.h>
#include <Wincrypt.h>

#include <iprt/buildconfig.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/utf16.h>

#include <package-generated.h>
#include "product-generated.h"

#include <VBox/version.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define VCU_COMMON_OPTION_DEFINITIONS() \
        { "--verbose",                  'v',                                RTGETOPT_REQ_NOTHING }, \
        { "--quiet",                    'q',                                RTGETOPT_REQ_NOTHING }

#define VCU_COMMON_OPTION_HANDLING() \
            case 'v': \
                g_cVerbosityLevel++; \
                break; \
            \
            case 'q': \
                if (g_cVerbosityLevel > 0) \
                    g_cVerbosityLevel--; \
                break; \
            \
            case 'V': \
                return displayVersion()



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The verbosity level. */
static unsigned  g_cVerbosityLevel = 1;


static const char *errorToString(DWORD dwErr)
{
    switch (dwErr)
    {
#define MY_CASE(a_uConst)       case a_uConst: return #a_uConst;
        MY_CASE(CRYPT_E_MSG_ERROR);
        MY_CASE(CRYPT_E_UNKNOWN_ALGO);
        MY_CASE(CRYPT_E_OID_FORMAT);
        MY_CASE(CRYPT_E_INVALID_MSG_TYPE);
        MY_CASE(CRYPT_E_UNEXPECTED_ENCODING);
        MY_CASE(CRYPT_E_AUTH_ATTR_MISSING);
        MY_CASE(CRYPT_E_HASH_VALUE);
        MY_CASE(CRYPT_E_INVALID_INDEX);
        MY_CASE(CRYPT_E_ALREADY_DECRYPTED);
        MY_CASE(CRYPT_E_NOT_DECRYPTED);
        MY_CASE(CRYPT_E_RECIPIENT_NOT_FOUND);
        MY_CASE(CRYPT_E_CONTROL_TYPE);
        MY_CASE(CRYPT_E_ISSUER_SERIALNUMBER);
        MY_CASE(CRYPT_E_SIGNER_NOT_FOUND);
        MY_CASE(CRYPT_E_ATTRIBUTES_MISSING);
        MY_CASE(CRYPT_E_STREAM_MSG_NOT_READY);
        MY_CASE(CRYPT_E_STREAM_INSUFFICIENT_DATA);
        MY_CASE(CRYPT_I_NEW_PROTECTION_REQUIRED);
        MY_CASE(CRYPT_E_BAD_LEN);
        MY_CASE(CRYPT_E_BAD_ENCODE);
        MY_CASE(CRYPT_E_FILE_ERROR);
        MY_CASE(CRYPT_E_NOT_FOUND);
        MY_CASE(CRYPT_E_EXISTS);
        MY_CASE(CRYPT_E_NO_PROVIDER);
        MY_CASE(CRYPT_E_SELF_SIGNED);
        MY_CASE(CRYPT_E_DELETED_PREV);
        MY_CASE(CRYPT_E_NO_MATCH);
        MY_CASE(CRYPT_E_UNEXPECTED_MSG_TYPE);
        MY_CASE(CRYPT_E_NO_KEY_PROPERTY);
        MY_CASE(CRYPT_E_NO_DECRYPT_CERT);
        MY_CASE(CRYPT_E_BAD_MSG);
        MY_CASE(CRYPT_E_NO_SIGNER);
        MY_CASE(CRYPT_E_PENDING_CLOSE);
        MY_CASE(CRYPT_E_REVOKED);
        MY_CASE(CRYPT_E_NO_REVOCATION_DLL);
        MY_CASE(CRYPT_E_NO_REVOCATION_CHECK);
        MY_CASE(CRYPT_E_REVOCATION_OFFLINE);
        MY_CASE(CRYPT_E_NOT_IN_REVOCATION_DATABASE);
        MY_CASE(CRYPT_E_INVALID_NUMERIC_STRING);
        MY_CASE(CRYPT_E_INVALID_PRINTABLE_STRING);
        MY_CASE(CRYPT_E_INVALID_IA5_STRING);
        MY_CASE(CRYPT_E_INVALID_X500_STRING);
        MY_CASE(CRYPT_E_NOT_CHAR_STRING);
        MY_CASE(CRYPT_E_FILERESIZED);
        MY_CASE(CRYPT_E_SECURITY_SETTINGS);
        MY_CASE(CRYPT_E_NO_VERIFY_USAGE_DLL);
        MY_CASE(CRYPT_E_NO_VERIFY_USAGE_CHECK);
        MY_CASE(CRYPT_E_VERIFY_USAGE_OFFLINE);
        MY_CASE(CRYPT_E_NOT_IN_CTL);
        MY_CASE(CRYPT_E_NO_TRUSTED_SIGNER);
        MY_CASE(CRYPT_E_MISSING_PUBKEY_PARA);
        MY_CASE(CRYPT_E_OSS_ERROR);
        default:
        {
            static char s_szErr[80];
            if (RTErrWinQueryDefine(dwErr, s_szErr, sizeof(s_szErr), true /*fFailIfUnknown*/) == VERR_NOT_FOUND)
                RTStrPrintf(s_szErr, sizeof(s_szErr), "%#x (%d)", dwErr, dwErr);
            return s_szErr;
        }
    }
}


/**
 * Deals with -V and --version.
 */
static RTEXITCODE displayVersion(void)
{
    RTPrintf("%sr%d\n", RTBldCfgVersion(), RTBldCfgRevision());
    return RTEXITCODE_SUCCESS;
}


#if 0 /* hacking */
static RTEXITCODE addToStore(const char *pszFilename, PCRTUTF16 pwszStore)
{
    /*
     * Open the source.
     */
    void   *pvFile;
    size_t  cbFile;
    int rc = RTFileReadAll(pszFilename, &pvFile, &cbFile);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTFileReadAll failed on '%s': %Rrc", pszFilename, rc);

    RTEXITCODE rcExit = RTEXITCODE_FAILURE;

    PCCERT_CONTEXT pCertCtx = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                           (PBYTE)pvFile,
                                                           (DWORD)cbFile);
    if (pCertCtx)
    {
        /*
         * Open the destination.
         */
        HCERTSTORE hDstStore = CertOpenStore(CERT_STORE_PROV_SYSTEM_W,
                                             PKCS_7_ASN_ENCODING | X509_ASN_ENCODING,
                                             NULL /* hCryptProv = default */,
                                             /*CERT_SYSTEM_STORE_LOCAL_MACHINE*/ CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_OPEN_EXISTING_FLAG,
                                             pwszStore);
        if (hDstStore != NULL)
        {
#if 0
            DWORD dwContextType;
            if (CertAddSerializedElementToStore(hDstStore,
                                                pCertCtx->pbCertEncoded,
                                                pCertCtx->cbCertEncoded,
                                                CERT_STORE_ADD_NEW,
                                                0 /* dwFlags (reserved) */,
                                                CERT_STORE_ALL_CONTEXT_FLAG,
                                                &dwContextType,
                                                NULL))
            {
                RTMsgInfo("Successfully added '%s' to the '%ls' store (ctx type %u)", pszFilename, pwszStore, dwContextType);
                rcExit = RTEXITCODE_SUCCESS;
            }
            else
                RTMsgError("CertAddSerializedElementToStore returned %s", errorToString(GetLastError()));
#else
            if (CertAddCertificateContextToStore(hDstStore, pCertCtx, CERT_STORE_ADD_NEW, NULL))
            {
                RTMsgInfo("Successfully added '%s' to the '%ls' store", pszFilename, pwszStore);
                rcExit = RTEXITCODE_SUCCESS;
            }
            else
                RTMsgError("CertAddCertificateContextToStore returned %s", errorToString(GetLastError()));
#endif

            CertCloseStore(hDstStore, CERT_CLOSE_STORE_CHECK_FLAG);
        }
        else
            RTMsgError("CertOpenStore returned %s", errorToString(GetLastError()));
        CertFreeCertificateContext(pCertCtx);
    }
    else
        RTMsgError("CertCreateCertificateContext returned %s", errorToString(GetLastError()));
    RTFileReadAllFree(pvFile, cbFile);
    return rcExit;

#if 0

    CRYPT_DATA_BLOB Blob;
    Blob.cbData = (DWORD)cbData;
    Blob.pbData = (PBYTE)pvData;
    HCERTSTORE hSrcStore = PFXImportCertStore(&Blob, L"", )

#endif
}
#endif /* hacking */


/**
 * Reads a certificate from a file, returning a context or a the handle to a
 * temporary memory store.
 *
 * @returns true on success, false on failure (error message written).
 * @param   pszCertFile         The name of the file containing the
 *                              certificates.
 * @param   ppOutCtx            Where to return the certificate context.
 * @param   phSrcStore          Where to return the handle to the temporary
 *                              memory store.
 */
static bool readCertFile(const char *pszCertFile, PCCERT_CONTEXT *ppOutCtx, HCERTSTORE *phSrcStore)
{
    *ppOutCtx   = NULL;
    *phSrcStore = NULL;

    bool    fRc = false;
    void   *pvFile;
    size_t  cbFile;
    int rc = RTFileReadAll(pszCertFile, &pvFile, &cbFile);
    if (RT_SUCCESS(rc))
    {
        *ppOutCtx = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                 (PBYTE)pvFile, (DWORD)cbFile);
        if (*ppOutCtx)
            fRc = true;
        else
        {
            /** @todo figure out if it's some other format... */
            RTMsgError("CertCreateCertificateContext returned %s parsing the content of '%s'",
                       errorToString(GetLastError()), pszCertFile);
        }
        RTFileReadAllFree(pvFile, cbFile);
    }
    else
        RTMsgError("RTFileReadAll failed on '%s': %Rrc", pszCertFile, rc);
    return fRc;
}


/**
 * Opens a certificate store.
 *
 * @returns true on success, false on failure (error message written).
 * @param   dwDst           The destination, like
 *                          CERT_SYSTEM_STORE_LOCAL_MACHINE or
 *                          CERT_SYSTEM_STORE_CURRENT_USER.
 * @param   pszStoreNm      The store name.
 */
static HCERTSTORE openCertStore(DWORD dwDst, const char *pszStoreNm)
{
    HCERTSTORE hStore = NULL;
    PRTUTF16   pwszStoreNm;
    int rc = RTStrToUtf16(pszStoreNm, &pwszStoreNm);
    if (RT_SUCCESS(rc))
    {
        if (g_cVerbosityLevel > 1)
            RTMsgInfo("Opening store %#x:'%s'", dwDst, pszStoreNm);

        /*
         * Make sure CERT_STORE_OPEN_EXISTING_FLAG is not set. This causes Windows XP
         * to return ACCESS_DENIED when installing TrustedPublisher certificates via
         * CertAddCertificateContextToStore() if the TrustedPublisher store never has
         * been used (through certmgr.exe and friends) yet.
         *
         * According to MSDN, if neither CERT_STORE_OPEN_EXISTING_FLAG nor
         * CERT_STORE_CREATE_NEW_FLAG is set, the store will be either opened or
         * created accordingly.
         */
        dwDst &= ~CERT_STORE_OPEN_EXISTING_FLAG;

        hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM_W,
                               PKCS_7_ASN_ENCODING | X509_ASN_ENCODING,
                               NULL /* hCryptProv = default */,
                               dwDst,
                               pwszStoreNm);
        if (hStore == NULL)
            RTMsgError("CertOpenStore failed opening %#x:'%s': %s",
                       dwDst, pszStoreNm, errorToString(GetLastError()));

        RTUtf16Free(pwszStoreNm);
    }
    return hStore;
}


/**
 * Worker for 'root-exists', searching by exact relative distinguished name.
 */
static RTEXITCODE checkIfCertExistsInStoreByRdn(DWORD dwStore, const char *pszStoreNm, const char *pszStoreDesc,
                                                const char *pszName, RTEXITCODE rcExit, uint32_t *pcFound)
{
    /*
     * Convert the name into something that can be searched for.
     */
    PRTUTF16 pwszName = NULL;
    int rc = RTStrToUtf16(pszName, &pwszName);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTStrToUtf16 failed: %Rrc", rc);


    BYTE            abNameBuf[16384]; /* this should be more than sufficient... */
    CERT_NAME_BLOB  NameBlob = { sizeof(abNameBuf), abNameBuf };
    PCRTUTF16       pwszErr  = NULL;
    if (CertStrToNameW(X509_ASN_ENCODING, pwszName, CERT_X500_NAME_STR | CERT_NAME_STR_SEMICOLON_FLAG, NULL /*pvReserved*/,
                       NameBlob.pbData, &NameBlob.cbData, &pwszErr))
    {
        /*
         * Now perform the search.
         */
        HCERTSTORE hDstStore = openCertStore(dwStore, pszStoreNm);
        if (hDstStore)
        {
            uint32_t        cFound  = 0;
            uint32_t        idxCur  = 0;
            PCCERT_CONTEXT  pCurCtx = NULL;
            while ((pCurCtx = CertEnumCertificatesInStore(hDstStore, pCurCtx)) != NULL)
            {
                if (pCurCtx->pCertInfo)
                {
                    if (g_cVerbosityLevel > 1)
                    {
                        WCHAR wszCurName[1024];
                        if (CertNameToStrW(X509_ASN_ENCODING, &pCurCtx->pCertInfo->Subject,
                                           CERT_X500_NAME_STR | CERT_NAME_STR_SEMICOLON_FLAG,
                                           wszCurName, sizeof(wszCurName)))
                            RTMsgInfo("Considering #%u: '%ls' ...", idxCur, wszCurName);
                        else
                            RTMsgInfo("Considering #%u: CertNameToStrW -> %u ...", idxCur, GetLastError());
                    }

                    if (CertCompareCertificateName(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, &pCurCtx->pCertInfo->Subject, &NameBlob))
                    {
                        if (g_cVerbosityLevel > 0)
                            RTMsgInfo("Found '%ls' in the %s store...", pwszName, pszStoreDesc);
                        cFound++;
                    }
                }
                idxCur++;
            }

            *pcFound += cFound;
            if (!cFound && g_cVerbosityLevel > 0)
                RTMsgInfo("Certificate with subject '%ls' was _NOT_ found in the %s store.", pwszName, pszStoreDesc);

            CertCloseStore(hDstStore, CERT_CLOSE_STORE_CHECK_FLAG);
        }
        else
            rcExit = RTEXITCODE_FAILURE;
    }
    else
        rcExit = RTMsgErrorExitFailure("CertStrToNameW failed at position %zu: %s\n"
                                       " '%ls'\n"
                                       "  %*s",
                                       pwszErr - pwszName, errorToString(GetLastError()), pwszName, pwszErr - pwszName, "^");
    RTUtf16Free(pwszName);
    return rcExit;
}


/**
 * Removes a certificate, given by file, from a store
 *
 * @returns Command exit code.
 * @param   dwDst           The destination, like
 *                          CERT_SYSTEM_STORE_LOCAL_MACHINE or
 *                          CERT_SYSTEM_STORE_CURRENT_USER.
 * @param   pszStoreNm      The store name.
 * @param   pszStoreDesc    The store descriptor (all lower case).
 * @param   pszCertFile     The file containing the certificate to add.
 * @param   rcExit          Incoming exit status.
 */
static RTEXITCODE removeCertFromStoreByFile(DWORD dwDst, const char *pszStoreNm, const char *pszStoreDesc,
                                            const char *pszCertFile, RTEXITCODE rcExit)
{
    /*
     * Read the certificate file first and get the certificate name from it.
     */
    PCCERT_CONTEXT  pSrcCtx   = NULL;
    HCERTSTORE      hSrcStore = NULL;
    if (!readCertFile(pszCertFile, &pSrcCtx, &hSrcStore))
        return RTEXITCODE_FAILURE;

    WCHAR wszName[1024];
    if (!CertGetNameStringW(pSrcCtx, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0 /*dwFlags*/, NULL /*pvTypePara*/,
                            wszName, sizeof(wszName)))
    {
        RTMsgError("CertGetNameStringW(Subject) failed: %s\n", errorToString(GetLastError()));
        wszName[0] = '\0';
    }

    /*
     * Open the destination store.
     */
    HCERTSTORE hDstStore = openCertStore(dwDst, pszStoreNm);
    if (hDstStore)
    {
        if (pSrcCtx)
        {
            unsigned        cDeleted = 0;
            PCCERT_CONTEXT  pCurCtx  = NULL;
            while ((pCurCtx = CertEnumCertificatesInStore(hDstStore, pCurCtx)) != NULL)
            {
                if (CertCompareCertificate(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, pCurCtx->pCertInfo, pSrcCtx->pCertInfo))
                {
                    if (g_cVerbosityLevel > 1)
                        RTMsgInfo("Removing '%ls'...", wszName);
                    PCCERT_CONTEXT pDeleteCtx = CertDuplicateCertificateContext(pCurCtx);
                    if (pDeleteCtx)
                    {
                        if (CertDeleteCertificateFromStore(pDeleteCtx))
                        {
                            cDeleted++;
                            if (g_cVerbosityLevel > 0)
                                RTMsgInfo("Successfully removed '%s' ('%ls') from the %s store", pszCertFile, wszName, pszStoreDesc);
                        }
                        else
                            rcExit = RTMsgErrorExitFailure("CertDeleteFromStore('%ls') failed: %s\n",
                                                           wszName, errorToString(GetLastError()));
                    }
                    else
                        rcExit = RTMsgErrorExitFailure("CertDuplicateCertificateContext('%ls') failed: %s\n",
                                                       wszName, errorToString(GetLastError()));
                }
            }

            if (!cDeleted)
                RTMsgInfo("Found no matching certificates to remove.");
        }
        else
            rcExit = RTMsgErrorExitFailure("Code path not implemented at line %d\n",  __LINE__);


        CertCloseStore(hDstStore, CERT_CLOSE_STORE_CHECK_FLAG);
    }
    else
        rcExit = RTEXITCODE_FAILURE;
    if (pSrcCtx)
        CertFreeCertificateContext(pSrcCtx);
    if (hSrcStore)
        CertCloseStore(hSrcStore, CERT_CLOSE_STORE_CHECK_FLAG);
    return rcExit;
}


/**
 * Adds a certificate to a store.
 *
 * @returns true on success, false on failure (error message written).
 * @param   dwDst           The destination, like
 *                          CERT_SYSTEM_STORE_LOCAL_MACHINE or
 *                          CERT_SYSTEM_STORE_CURRENT_USER.
 * @param   pszStoreNm      The store name.
 * @param   pszCertFile     The file containing the certificate to add.
 * @param   dwDisposition   The disposition towards existing certificates when
 *                          adding it.  CERT_STORE_ADD_NEW is a safe one.
 * @param   pfAlreadyExists Where to indicate whether the certificate was
 *                          already present and not replaced.
 */
static bool addCertToStoreByFile(DWORD dwDst, const char *pszStoreNm, const char *pszCertFile, DWORD dwDisposition,
                                 bool *pfAlreadyExists)
{
    *pfAlreadyExists = false;

    /*
     * Read the certificate file first.
     */
    PCCERT_CONTEXT  pSrcCtx   = NULL;
    HCERTSTORE      hSrcStore = NULL;
    if (!readCertFile(pszCertFile, &pSrcCtx, &hSrcStore))
        return false;

    /*
     * Open the destination store.
     */
    bool fRc = false;
    HCERTSTORE hDstStore = openCertStore(dwDst, pszStoreNm);
    if (hDstStore)
    {
        if (pSrcCtx)
        {
            if (g_cVerbosityLevel > 1)
                RTMsgInfo("Adding '%s' to %#x:'%s'... (disp %d)", pszCertFile, dwDst, pszStoreNm, dwDisposition);

            if (CertAddCertificateContextToStore(hDstStore, pSrcCtx, dwDisposition, NULL))
                fRc = true;
            else
            {
                DWORD const dwErr = GetLastError();
                *pfAlreadyExists = fRc = dwErr == CRYPT_E_EXISTS;
                if (!fRc)
                    RTMsgError("CertAddCertificateContextToStore returned %s", errorToString(dwErr));
            }
        }
        else
            RTMsgError("Code path not implemented at line %d\n",  __LINE__);

        CertCloseStore(hDstStore, CERT_CLOSE_STORE_CHECK_FLAG);
    }
    if (pSrcCtx)
        CertFreeCertificateContext(pSrcCtx);
    if (hSrcStore)
        CertCloseStore(hSrcStore, CERT_CLOSE_STORE_CHECK_FLAG);
    return fRc;
}


/**
 * Handle adding one or more certificates to a store.
 */
static RTEXITCODE addCertToStoreByFilePattern(DWORD dwDst, const char *pszStoreNm, const char *pszStoreDesc,
                                              const char *pszFilePattern, bool fForce, RTEXITCODE rcExit, uint32_t *pcImports)
{
    PCRTPATHGLOBENTRY pResultHead;
    int rc = RTPathGlob(pszFilePattern, RTPATHGLOB_F_NO_DIRS, &pResultHead, NULL);
    if (RT_SUCCESS(rc))
    {
        for (PCRTPATHGLOBENTRY pCur = pResultHead; pCur; pCur = pCur->pNext)
        {
            bool fAlreadyExists = false;
            if (addCertToStoreByFile(dwDst, pszStoreNm, pCur->szPath,
                                     !fForce ? CERT_STORE_ADD_NEW : CERT_STORE_ADD_REPLACE_EXISTING,
                                     &fAlreadyExists))
            {
                if (!fAlreadyExists)
                    RTMsgInfo("Successfully added '%s' to the %s store", pCur->szPath, pszStoreDesc);
                else
                    RTMsgInfo("Certificate '%s' is already present in the %s store and was not re-added or updated.",
                              pCur->szPath, pszStoreNm);
            }
            else
                rcExit = RTEXITCODE_FAILURE;
            *pcImports += 1;
        }
        RTPathGlobFree(pResultHead);
    }
    else
    {
        rcExit = RTMsgErrorExitFailure("glob failed on '%s': %Rrc", pszFilePattern, rc);
        *pcImports += 1;
    }
    return rcExit;
}


/**
 * Worker for cmdDisplayAll.
 */
static BOOL WINAPI displaySystemStoreCallback(const void *pvSystemStore, DWORD dwFlags, PCERT_SYSTEM_STORE_INFO pStoreInfo,
                                              void *pvReserved, void *pvArg) RT_NOTHROW_DEF
{
    RT_NOREF(pvArg);
    if (g_cVerbosityLevel > 1)
        RTPrintf("    pvSystemStore=%p dwFlags=%#x pStoreInfo=%p pvReserved=%p\n", pvSystemStore, dwFlags, pStoreInfo, pvReserved);
    LPCWSTR pwszStoreNm = NULL;
    if (dwFlags & CERT_SYSTEM_STORE_RELOCATE_FLAG)
    {
        const CERT_SYSTEM_STORE_RELOCATE_PARA *pRelPara = (const CERT_SYSTEM_STORE_RELOCATE_PARA *)pvSystemStore;
        pwszStoreNm = pRelPara->pwszSystemStore;
        RTPrintf("    %#010x '%ls' hKeyBase=%p\n", dwFlags, pwszStoreNm, pRelPara->hKeyBase);
    }
    else
    {
        pwszStoreNm = (LPCWSTR)pvSystemStore;
        RTPrintf("    %#010x '%ls'\n", dwFlags, pwszStoreNm);
    }

    /*
     * Open the store and list the certificates within.
     */
    DWORD      dwDst  = (dwFlags & CERT_SYSTEM_STORE_LOCATION_MASK);
    HCERTSTORE hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM_W,
                                      PKCS_7_ASN_ENCODING | X509_ASN_ENCODING,
                                      NULL /* hCryptProv = default */,
                                      dwDst | CERT_STORE_OPEN_EXISTING_FLAG,
                                      pwszStoreNm);
    if (hStore)
    {
        PCCERT_CONTEXT pCertCtx = NULL;
        while ((pCertCtx = CertEnumCertificatesInStore(hStore, pCertCtx)) != NULL)
        {
            if (g_cVerbosityLevel > 1)
                RTPrintf("        pCertCtx=%p dwCertEncodingType=%#x cbCertEncoded=%#x pCertInfo=%p\n",
                         pCertCtx, pCertCtx->dwCertEncodingType, pCertCtx->cbCertEncoded, pCertCtx->pCertInfo);
            WCHAR wszName[1024];
            if (CertGetNameStringW(pCertCtx, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0 /*dwFlags*/, NULL /*pvTypePara*/,
                                   wszName, sizeof(wszName)))
            {
                RTPrintf("        '%ls'\n", wszName);
                if (pCertCtx->pCertInfo)
                {
                    RTTIMESPEC TmpTS;
                    char  szNotBefore[80];
                    RTTimeSpecToString(RTTimeSpecSetNtFileTime(&TmpTS, &pCertCtx->pCertInfo->NotBefore),
                                       szNotBefore, sizeof(szNotBefore));
                    char  szNotAfter[80];
                    RTTimeSpecToString(RTTimeSpecSetNtFileTime(&TmpTS, &pCertCtx->pCertInfo->NotAfter),
                                       szNotAfter, sizeof(szNotAfter));

                    RTPrintf("            NotBefore='%s'\n", szNotBefore);
                    RTPrintf("            NotAfter ='%s'\n", szNotAfter);
                    if (pCertCtx->pCertInfo->Issuer.cbData)
                    {
                        if (CertGetNameStringW(pCertCtx, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG, NULL /*pvTypePara*/,
                                               wszName, sizeof(wszName)))
                            RTPrintf("            Issuer='%ls'\n", wszName);
                        else
                            RTMsgError("CertGetNameStringW(Issuer) failed: %s\n", errorToString(GetLastError()));
                    }
                }
            }
            else
                RTMsgError("CertGetNameStringW(Subject) failed: %s\n", errorToString(GetLastError()));

        }

        CertCloseStore(hStore, CERT_CLOSE_STORE_CHECK_FLAG);
    }
    else
        RTMsgError("CertOpenStore failed opening %#x:'%ls': %s\n", dwDst, pwszStoreNm, errorToString(GetLastError()));

    return TRUE;
}


/**
 * Worker for cmdDisplayAll.
 */
static BOOL WINAPI
displaySystemStoreLocation(LPCWSTR pwszStoreLocation, DWORD dwFlags, void *pvReserved, void *pvArg) RT_NOTHROW_DEF
{
    NOREF(pvReserved); NOREF(pvArg);
    RTPrintf("System store location: %#010x '%ls'\n", dwFlags, pwszStoreLocation);
    if (!CertEnumSystemStore(dwFlags, NULL, NULL /*pvArg*/, displaySystemStoreCallback))
        RTMsgError("CertEnumSystemStore failed on %#x:'%ls': %s\n",
                   dwFlags, pwszStoreLocation, errorToString(GetLastError()));

    return TRUE;
}


/**
 * Handler for the 'display-all' command.
 */
static RTEXITCODE cmdDisplayAll(int argc, char **argv)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        VCU_COMMON_OPTION_DEFINITIONS(),
    };

    int             rc;
    RTGETOPTUNION   ValueUnion;
    RTGETOPTSTATE   GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    while ((rc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (rc)
        {
            VCU_COMMON_OPTION_HANDLING();

            case 'h':
                RTStrmWrappedPrintf(g_pStdOut, RTSTRMWRAPPED_F_HANGING_INDENT,
                                    "Usage: VBoxCertUtil display-all [-v|--verbose] [-q|--quiet]\n");
                return RTEXITCODE_SUCCESS;

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    /*
     * Do the enumerating.
     */
    if (!CertEnumSystemStoreLocation(0, NULL /*pvArg*/, displaySystemStoreLocation))
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "CertEnumSystemStoreLocation failed: %s\n", errorToString(GetLastError()));
    return RTEXITCODE_SUCCESS;
}


/**
 * Handler for the 'root-exists' command.
 */
static RTEXITCODE cmdRootExists(int argc, char **argv)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        VCU_COMMON_OPTION_DEFINITIONS(),
    };

    RTEXITCODE      rcExit    = RTEXITCODE_SUCCESS;
    uint32_t        cFound    = 0;
    uint32_t        cSearched = 0;

    int             rc;
    RTGETOPTUNION   ValueUnion;
    RTGETOPTSTATE   GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    while ((rc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (rc)
        {
            VCU_COMMON_OPTION_HANDLING();

            case 'h':
                RTStrmWrappedPrintf(g_pStdOut, RTSTRMWRAPPED_F_HANGING_INDENT,
                                    "Usage: VBoxCertUtil root-exists <full-subject-name> [alternative-subject-name [...]]\n");
                RTStrmWrappedPrintf(g_pStdOut, 0,
                                    "\n"
                                    "Exit code: 10 if not found, 0 if found.\n"
                                    "\n"
                                    "The names are on the form 'C=US; O=Company; OU=some unit; CN=a cert name' "
                                    "where semi-colon is the X.500 attribute separator and spaces surrounding it "
                                    "the type (CN, OU, ) and '=' are generally ignored.\n"
                                    "\n"
                                    "At verbosity level 2, the full subject name of each certificate in the store "
                                    "will be listed as the search progresses.  These can be used as search input.\n"
                                    );
                return RTEXITCODE_SUCCESS;

            case VINF_GETOPT_NOT_OPTION:
                rcExit = checkIfCertExistsInStoreByRdn(CERT_SYSTEM_STORE_LOCAL_MACHINE, "Root", "root",
                                                       ValueUnion.psz, rcExit, &cFound);
                cSearched++;
                break;

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    if (!cSearched)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No certificate name specified.");
    return cFound ? RTEXITCODE_SUCCESS : rcExit == RTEXITCODE_SUCCESS ? (RTEXITCODE)10 : rcExit;
}



/**
 * Handler for the 'remove-root' command.
 */
static RTEXITCODE cmdRemoveRoot(int argc, char **argv)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        VCU_COMMON_OPTION_DEFINITIONS(),
    };

    RTEXITCODE      rcExit = RTEXITCODE_SUCCESS;
    uint32_t        cRemoved = 0;

    int             rc;
    RTGETOPTUNION   ValueUnion;
    RTGETOPTSTATE   GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    while ((rc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (rc)
        {
            VCU_COMMON_OPTION_HANDLING();

            case 'h':
                RTStrmWrappedPrintf(g_pStdOut, RTSTRMWRAPPED_F_HANGING_INDENT,
                                    "Usage: VBoxCertUtil remove-root <root-cert-file>\n");
                return RTEXITCODE_SUCCESS;

            case VINF_GETOPT_NOT_OPTION:
                rcExit = removeCertFromStoreByFile(CERT_SYSTEM_STORE_LOCAL_MACHINE, "Root", "root", ValueUnion.psz, rcExit);
                cRemoved++;
                break;

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }
    if (!cRemoved)
        return RTMsgErrorExit(RTEXITCODE_SUCCESS, "No certificate specified.");
    return rcExit;
}


/**
 * Handler for the 'remove-trusted-publisher' command.
 */
static RTEXITCODE cmdRemoveTrustedPublisher(int argc, char **argv)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--root",     'r',    RTGETOPT_REQ_STRING },
        VCU_COMMON_OPTION_DEFINITIONS(),
    };

    RTEXITCODE      rcExit = RTEXITCODE_SUCCESS;
    uint32_t        cRemoved = 0;

    int             rc;
    RTGETOPTUNION   ValueUnion;
    RTGETOPTSTATE   GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    while ((rc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (rc)
        {
            VCU_COMMON_OPTION_HANDLING();

            case 'h':
                RTStrmWrappedPrintf(g_pStdOut, RTSTRMWRAPPED_F_HANGING_INDENT,
                                    "Usage: VBoxCertUtil remove-trusted-publisher [--root <root-cert>] <trusted-cert>\n");
                return RTEXITCODE_SUCCESS;

            case 'r':
                rcExit = removeCertFromStoreByFile(CERT_SYSTEM_STORE_LOCAL_MACHINE, "Root", "root", ValueUnion.psz, rcExit);
                cRemoved++;
                break;

            case VINF_GETOPT_NOT_OPTION:
                rcExit = removeCertFromStoreByFile(CERT_SYSTEM_STORE_LOCAL_MACHINE, "TrustedPublisher", "trusted publisher",
                                                   ValueUnion.psz, rcExit);
                cRemoved++;
                break;

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }
    if (!cRemoved)
        return RTMsgErrorExit(RTEXITCODE_SUCCESS, "No certificate specified.");
    return rcExit;
}


/**
 * Handler for the 'add-root' command.
 */
static RTEXITCODE cmdAddRoot(int argc, char **argv)
{
    /*
     * Parse arguments and execute imports as we move along.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--add-if-new",   'a',    RTGETOPT_REQ_NOTHING },
        { "--force",        'f',    RTGETOPT_REQ_NOTHING },
        VCU_COMMON_OPTION_DEFINITIONS(),
    };

    RTEXITCODE          rcExit    = RTEXITCODE_SUCCESS;
    unsigned            cImports  = 0;
    bool                fForce    = false;
    RTGETOPTUNION       ValueUnion;
    RTGETOPTSTATE       GetState;
    int rc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    AssertRC(rc);
    while ((rc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (rc)
        {
            VCU_COMMON_OPTION_HANDLING();

            case 'a':
                fForce = false;
                break;

            case 'f':
                fForce = false;
                break;

            case 'h':
                RTStrmWrappedPrintf(g_pStdOut, RTSTRMWRAPPED_F_HANGING_INDENT,
                                    "Usage: VBoxCertUtil add-root [--force|--add-if-new] <root-cert>\n");
                return RTEXITCODE_SUCCESS;

            case VINF_GETOPT_NOT_OPTION:
                rcExit = addCertToStoreByFilePattern(CERT_SYSTEM_STORE_LOCAL_MACHINE, "Root", "root",
                                                     ValueUnion.psz, fForce, rcExit, &cImports);
                break;

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }
    if (cImports == 0)
        return RTMsgErrorExit(RTEXITCODE_SUCCESS, "No trusted or root certificates specified.");
    return rcExit;
}


/**
 * Handler for the 'add-trusted-publisher' command.
 */
static RTEXITCODE cmdAddTrustedPublisher(int argc, char **argv)
{
    /*
     * Parse arguments and execute imports as we move along.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--root",         'r',    RTGETOPT_REQ_STRING },
        { "--add-if-new",   'a',    RTGETOPT_REQ_NOTHING },
        { "--force",        'f',    RTGETOPT_REQ_NOTHING },
        VCU_COMMON_OPTION_DEFINITIONS(),
    };

    RTEXITCODE          rcExit    = RTEXITCODE_SUCCESS;
    bool                fForce    = false;
    unsigned            cImports  = 0;
    RTGETOPTUNION       ValueUnion;
    RTGETOPTSTATE       GetState;
    int rc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    AssertRC(rc);
    while ((rc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (rc)
        {
            VCU_COMMON_OPTION_HANDLING();

            case 'a':
                fForce = false;
                break;

            case 'f':
                fForce = false;
                break;

            case 'h':
                RTStrmWrappedPrintf(g_pStdOut, RTSTRMWRAPPED_F_HANGING_INDENT,
                                    "Usage: VBoxCertUtil add-trusted-publisher [--force|--add-if-new] "
                                    "[--root <root-cert>] <trusted-cert>\n");
                return RTEXITCODE_SUCCESS;

            case 'r':
                rcExit = addCertToStoreByFilePattern(CERT_SYSTEM_STORE_LOCAL_MACHINE, "Root", "root",
                                                     ValueUnion.psz, fForce, rcExit, &cImports);
                break;

            case VINF_GETOPT_NOT_OPTION:
                rcExit = addCertToStoreByFilePattern(CERT_SYSTEM_STORE_LOCAL_MACHINE, "TrustedPublisher", "trusted publisher",
                                                     ValueUnion.psz, fForce, rcExit, &cImports);
                break;

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }
    if (cImports == 0)
        return RTMsgErrorExit(RTEXITCODE_SUCCESS, "No trusted or root certificates specified.");
    return rcExit;
}


/**
 * Shows the logo.
 *
 * @param   pStream             Output stream to show logo on.
 */
static void showLogo(PRTSTREAM pStream)
{
    RTStrmPrintf(pStream, VBOX_PRODUCT " VBoxCertUtil Version " VBOX_VERSION_STRING " - r%s\n"
                 "Copyright (C) " VBOX_C_YEAR " " VBOX_VENDOR "\n\n", RTBldCfgRevisionStr());
}


/**
 * Displays the usage info.
 */
static void showUsage(void)
{
    const char * const pszShortNm = RTProcShortName();
    RTPrintf("Usage: %Rbn [-v[v]|--verbose] [-q[q]|--quiet] <command>\n"
             "   or  %Rbn <-V|--version>\n"
             "   or  %Rbn <-h|--help>\n"
             "\n"
             "Available commands:\n"
             "    add-trusted-publisher\n"
             "    add-root\n"
             "    remove-trusted-publisher\n"
             "    remove-root\n"
             "    display-all\n"
             , pszShortNm, pszShortNm, pszShortNm);
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Parse arguments up to the command and pass it on to the command handlers.
     */
    typedef enum
    {
        VCUACTION_ADD_TRUSTED_PUBLISHER = 1000,
        VCUACTION_ADD_ROOT,
        VCUACTION_REMOVE_TRUSTED_PUBLISHER,
        VCUACTION_REMOVE_ROOT,
        VCUACTION_ROOT_EXISTS,
        VCUACTION_DISPLAY_ALL,
        VCUACTION_END
    } VCUACTION;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "add-trusted-publisher",      VCUACTION_ADD_TRUSTED_PUBLISHER,    RTGETOPT_REQ_NOTHING },
        { "add-root",                   VCUACTION_ADD_ROOT,                 RTGETOPT_REQ_NOTHING },
        { "remove-trusted-publisher",   VCUACTION_REMOVE_TRUSTED_PUBLISHER, RTGETOPT_REQ_NOTHING },
        { "remove-root",                VCUACTION_REMOVE_ROOT,              RTGETOPT_REQ_NOTHING },
        { "root-exists",                VCUACTION_ROOT_EXISTS,              RTGETOPT_REQ_NOTHING },
        { "display-all",                VCUACTION_DISPLAY_ALL,              RTGETOPT_REQ_NOTHING },
        VCU_COMMON_OPTION_DEFINITIONS(),
    };

    showLogo(g_pStdOut);

    RTGETOPTUNION   ValueUnion;
    RTGETOPTSTATE   GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    while ((rc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (rc)
        {
            case VCUACTION_ADD_TRUSTED_PUBLISHER:
                return cmdAddTrustedPublisher(argc - GetState.iNext + 1, argv + GetState.iNext - 1);

            case VCUACTION_ADD_ROOT:
                return cmdAddRoot(argc - GetState.iNext + 1, argv + GetState.iNext - 1);

            case VCUACTION_REMOVE_TRUSTED_PUBLISHER:
                return cmdRemoveTrustedPublisher(argc - GetState.iNext + 1, argv + GetState.iNext - 1);

            case VCUACTION_REMOVE_ROOT:
                return cmdRemoveRoot(argc - GetState.iNext + 1, argv + GetState.iNext - 1);

            case VCUACTION_ROOT_EXISTS:
                return cmdRootExists(argc - GetState.iNext + 1, argv + GetState.iNext - 1);

            case VCUACTION_DISPLAY_ALL:
                return cmdDisplayAll(argc - GetState.iNext + 1, argv + GetState.iNext - 1);

            case 'h':
                showUsage();
                return RTEXITCODE_SUCCESS;

            VCU_COMMON_OPTION_HANDLING();

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    RTMsgError("Missing command...");
    showUsage();
    return RTEXITCODE_SYNTAX;
}

