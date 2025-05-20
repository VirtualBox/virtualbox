/* $Id: VBoxWindowsAdditions.cpp 168939 2025-05-20 10:23:36Z knut.osmundsen@oracle.com $ */
/** @file
 * VBoxWindowsAdditions - The Windows Guest Additions Loader.
 *
 * This is STUB which select whether to install 32-bit or 64-bit additions.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define UNICODE                     /* For resource related macros. */
#include <iprt/cdefs.h>
#include <iprt/win/windows.h>
#include <Wintrust.h>
#include <Softpub.h>
#ifndef ERROR_ELEVATION_REQUIRED    /* Windows Vista and later. */
# define ERROR_ELEVATION_REQUIRED  740
#endif

#include <iprt/string.h>
#include <iprt/utf16.h>

#include "NoCrtOutput.h"

#ifdef VBOX_SIGNING_MODE
# include "BuildCerts.h"
# include "TimestampRootCerts.h"
#endif

#ifdef VBOX_SIGNING_MODE
# if 1 /* Whether to use IPRT or Windows to verify the executable signatures. */
#  include <iprt/err.h>
#  include <iprt/initterm.h>
#  include <iprt/ldr.h>
#  include <iprt/message.h>
#  include <iprt/stream.h>
#  include <iprt/crypto/pkcs7.h>
#  include <iprt/crypto/store.h>
# endif
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef BOOL (WINAPI *PFNISWOW64PROCESS)(HANDLE, PBOOL);
typedef BOOL (WINAPI *PFNISWOW64PROCESS2)(HANDLE, USHORT *, USHORT *);


#ifdef VBOX_SIGNING_MODE
# if 1 /* Whether to use IPRT or Windows to verify the executable signatures. */
/* This section is borrowed from RTSignTool.cpp */

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
    uint32_t    cTrustedCerts;
    const char *pszFilename;
    RTTIMESPEC  ValidationTime;

    VERIFYEXESTATE()
        : fKernel(false)
        , cVerbose(0)
        , enmSignType(kSignType_Windows)
        , enmLdrArch(RTLDRARCH_WHATEVER)
        , cBad(0)
        , cOkay(0)
        , cTrustedCerts(0)
        , pszFilename(NULL)
    {
        RTTimeSpecSetSeconds(&ValidationTime, 0);
    }
} VERIFYEXESTATE;

/**
 * @callback_method_impl{FNRTCRPKCS7VERIFYCERTCALLBACK,
 * Standard code signing.  Use this for Microsoft SPC.}
 */
static DECLCALLBACK(int) VerifyExecCertVerifyCallback(PCRTCRX509CERTIFICATE pCert, RTCRX509CERTPATHS hCertPaths, uint32_t fFlags,
                                                      void *pvUser, PRTERRINFO pErrInfo)
{
    VERIFYEXESTATE * const pState = (VERIFYEXESTATE *)pvUser;

    /* We don't set RTCRPKCS7VERIFY_SD_F_TRUST_ALL_CERTS, so it won't be NIL! */
    Assert(hCertPaths != NIL_RTCRX509CERTPATHS);

#  if 0 /* for debugging */
    uint32_t const cPaths = RTCrX509CertPathsGetPathCount(hCertPaths);
    RTMsgInfo("%s Path%s (pCert=%p hCertPaths=%p fFlags=%#x cPath=%d):",
              fFlags & RTCRPKCS7VCC_F_TIMESTAMP ? "Timestamp" : "Signature", cPaths == 1 ? "" : "s",
              pCert, hCertPaths, fFlags, cPaths);
    for (uint32_t iPath = 0; iPath < cPaths; iPath++)
    {
        RTCrX509CertPathsDumpOne(hCertPaths, iPath, pState->cVerbose, RTStrmDumpPrintfV, g_pStdOut);
        *pErrInfo->pszMsg = '\0';
    }
#  endif

    /*
     * Standard code signing capabilites required.
     *
     * Note! You may have to fix your test signing cert / releax this one to pass this.
     */
    int rc = RTCrPkcs7VerifyCertCallbackCodeSigning(pCert, hCertPaths, fFlags, NULL, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        /*
         * Check if the signing certificate is a trusted one, i.e. one of the
         * build certificates.
         */
        if (!(fFlags & RTCRPKCS7VCC_F_TIMESTAMP))
            if (RTCrX509CertPathsGetPathCount(hCertPaths) == 1)
                if (RTCrX509CertPathsGetPathLength(hCertPaths, 0) == 1)
                {
                    RTMsgInfo("Signed by trusted certificate.\n");
                    pState->cTrustedCerts++;
                }
    }
    else
        RTMsgError("RTCrPkcs7VerifyCertCallbackCodeSigning(,,%#x,,) failed: %Rrc%#RTeim", fFlags, pErrInfo);
    return rc;
}

/** @callback_method_impl{FNRTLDRVALIDATESIGNEDDATA}  */
static DECLCALLBACK(int) VerifyExeCallback(RTLDRMOD hLdrMod, PCRTLDRSIGNATUREINFO pInfo, PRTERRINFO pErrInfo, void *pvUser)
{
    VERIFYEXESTATE * const pState = (VERIFYEXESTATE *)pvUser;
    RT_NOREF_PV(hLdrMod);

    switch (pInfo->enmType)
    {
        case RTLDRSIGNATURETYPE_PKCS7_SIGNED_DATA:
        {
            PCRTCRPKCS7CONTENTINFO pContentInfo = (PCRTCRPKCS7CONTENTINFO)pInfo->pvSignature;

            if (pState->cVerbose > 0)
                RTMsgInfo("(%RU64) Verifying '%s' signature #%u ...\n", RTTimeProgramMilliTS(),
                          pState->pszFilename, pInfo->iSignature + 1);

#  if 0
            /*
             * Dump the signed data if so requested and it's the first one, assuming that
             * additional signatures in contained wihtin the same ContentInfo structure.
             */
            if (pState->cVerbose > 1 && pInfo->iSignature == 0)
                RTAsn1Dump(&pContentInfo->SeqCore.Asn1Core, 0, 0, RTStrmDumpPrintfV, g_pStdOut);
#  endif

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
            Assert(!pInfo->pvExternalData);
            for (unsigned iTime = 0; iTime < cTimes; iTime++)
            {
                RTTIMESPEC TimeSpec = aTimes[iTime].TimeSpec;
                rc = RTCrPkcs7VerifySignedData(pContentInfo,
                                               RTCRPKCS7VERIFY_SD_F_COUNTER_SIGNATURE_SIGNING_TIME_ONLY
                                               | RTCRPKCS7VERIFY_SD_F_ALWAYS_USE_SIGNING_TIME_IF_PRESENT
                                               | RTCRPKCS7VERIFY_SD_F_ALWAYS_USE_MS_TIMESTAMP_IF_PRESENT
                                               | RTCRPKCS7VERIFY_SD_F_CHECK_TRUST_ANCHORS
                                               | RTCRPKCS7VERIFY_SD_F_UPDATE_VALIDATION_TIME,
                                               pState->AdditionalStore.m_hStore, pState->RootStore.m_hStore,
                                               &TimeSpec, VerifyExecCertVerifyCallback, pState, pErrInfo);
                if (RT_SUCCESS(rc))
                {
                    Assert(rc == VINF_SUCCESS || rc == VINF_CR_DIGEST_DEPRECATED);
                    const char * const pszNote = rc == VINF_CR_DIGEST_DEPRECATED ? " (deprecated digest)" : "";
                    const char * const pszTime =    iTime == 0
                                                 && RTTimeSpecCompare(&TimeSpec, &aTimes[iTime].TimeSpec) != 0
                                               ? "at signing time" : aTimes[iTime].pszDesc;
                    if (pInfo->cSignatures == 1)
                        RTMsgInfo("(%RU64) '%s' is valid %s%s.\n", RTTimeProgramMilliTS(), pState->pszFilename, pszTime, pszNote);
                    else
                        RTMsgInfo("(%RU64) '%s' signature #%u is valid %s%s.\n", RTTimeProgramMilliTS(),
                                  pState->pszFilename, pInfo->iSignature + 1, pszTime, pszNote);
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
 * Uses IPRT functionality to check that the executable is signed with a
 * certiicate known to us.
 *
 * @returns 0 on success, non-zero exit code on failure.
 */
static int CheckFileSignatureIprt(wchar_t const *pwszExePath)
{
    RTR3InitExeNoArguments(RTR3INIT_FLAGS_STANDALONE_APP);
    RTMsgInfo("(%RU64) Signing checking of '%ls'...\n", RTTimeProgramMilliTS(), pwszExePath);

    /* Initialize the state. */
    VERIFYEXESTATE State;
    int rc = RTCrStoreCreateInMem(&State.RootStore.m_hStore, 0);
    if (RT_SUCCESS(rc))
        rc = RTCrStoreCreateInMem(&State.KernelRootStore.m_hStore, 0);
    if (RT_SUCCESS(rc))
        rc = RTCrStoreCreateInMem(&State.AdditionalStore.m_hStore, 0);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error creating in-memory certificate store: %Rrc", rc);

    /* Add the build certificates to the root store. */
    RTERRINFOSTATIC StaticErrInfo;
    for (uint32_t i = 0; i < g_cBuildCerts; i++)
    {
        rc = RTCrStoreCertAddEncoded(State.RootStore.m_hStore, RTCRCERTCTX_F_ENC_X509_DER,
                                     g_aBuildCerts[i].pbCert, g_aBuildCerts[i].cbCert, RTErrInfoInitStatic(&StaticErrInfo));
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to add build cert #%u to root store: %Rrc%#RTeim",
                                  i, rc, &StaticErrInfo.Core);
    }
    uint32_t i = g_cTimestampRootCerts; /* avoids warning if g_cTimestampRootCerts is 0. */
    while (i-- > 0)
    {
        rc = RTCrStoreCertAddEncoded(State.RootStore.m_hStore, RTCRCERTCTX_F_ENC_X509_DER,
                                     g_aTimestampRootCerts[i].pbCert, g_aTimestampRootCerts[i].cbCert,
                                     RTErrInfoInitStatic(&StaticErrInfo));
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to add build timestamp root cert #%u to root store: %Rrc%#RTeim",
                                  i, rc, &StaticErrInfo.Core);
    }

    /*
     * Open the executable image and verify it.
     */
    char *pszExePath = NULL;
    rc = RTUtf16ToUtf8(pwszExePath, &pszExePath);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTLDRMOD hLdrMod;
    rc = RTLdrOpen(pszExePath, RTLDR_O_FOR_VALIDATION, RTLDRARCH_WHATEVER, &hLdrMod);
    if (RT_SUCCESS(rc))
    {
        State.pszFilename = pszExePath;

        rc = RTLdrVerifySignature(hLdrMod, VerifyExeCallback, &State, RTErrInfoInitStatic(&StaticErrInfo));
        if (RT_FAILURE(rc))
            RTMsgError("RTLdrVerifySignature failed on '%s': %Rrc%#RTeim\n", pszExePath, rc, &StaticErrInfo.Core);

        RTStrFree(pszExePath);
        RTLdrClose(hLdrMod);

        if (RT_FAILURE(rc))
            return rc != VERR_LDRVI_NOT_SIGNED ? RTEXITCODE_FAILURE : RTEXITCODE_SKIPPED;
        if (State.cOkay > 0)
        {
            if (State.cTrustedCerts > 0)
                return RTEXITCODE_SUCCESS;
            RTMsgError("None of the build certificates were used for signing '%ls'!", pwszExePath);
        }
        return RTEXITCODE_FAILURE;
    }
    RTStrFree(pszExePath);
    return RTEXITCODE_FAILURE;
}

# else /* Using Windows APIs */

/**
 * Checks the file signatures of both this stub program and the actual installer
 * binary, making sure they use the same certificate as at build time and that
 * the signature verifies correctly.
 *
 * @returns 0 on success, non-zero exit code on failure.
 */
static int CheckFileSignatures(wchar_t const *pwszExePath, HANDLE hFileExe, wchar_t const *pwszSelfPath, HANDLE hFileSelf)
{
    /*
     * Check the OS version (bypassing shims).
     *
     * The RtlGetVersion API was added in windows 2000, so it's precense is a
     * provides a minimum OS version indicator already.
     */
    LONG (__stdcall *pfnRtlGetVersion)(OSVERSIONINFOEXW *);
    *(FARPROC *)&pfnRtlGetVersion = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    if (!pfnRtlGetVersion)
    {
        /* double check it. */
        DWORD const dwVersion = GetVersion();
        if ((dwVersion & 0xff) < 5)
            return 0;
        return ErrorMsgRcSUS(40, "RtlGetVersion not present while Windows version is 5.0 or higher (", dwVersion, ")");
    }
    OSVERSIONINFOEXW WinOsInfoEx = { sizeof(WinOsInfoEx) };
    NTSTATUS const   rcNt        = pfnRtlGetVersion(&WinOsInfoEx);
    if (!RT_SUCCESS(rcNt))
        return ErrorMsgRcSU(41, "RtlGetVersion failed: ", rcNt);

    /* Skip both of these checks if pre-XP. */
    if (   (WinOsInfoEx.dwMajorVersion == 5 && WinOsInfoEx.dwMinorVersion < 1)
        || WinOsInfoEx.dwMajorVersion < 4)
        return 0;

    /*
     * We need to find the system32 directory to load the WinVerifyTrust API.
     */
    static wchar_t const s_wszSlashWinTrustDll[] = L"\\Wintrust.dll";

    /* Call GetSystemWindowsDirectoryW/GetSystemDirectoryW. */
    wchar_t wszSysDll[MAX_PATH + sizeof(s_wszSlashWinTrustDll)] = { 0 };
    UINT const cwcSystem32 = GetSystemDirectoryW(wszSysDll, MAX_PATH);
    if (!cwcSystem32 || cwcSystem32 >= MAX_PATH)
        return ErrorMsgRc(42, "GetSystemDirectoryW failed");

    /* Load it: */
    memcpy(&wszSysDll[cwcSystem32], s_wszSlashWinTrustDll, sizeof(s_wszSlashWinTrustDll));
    DWORD   fLoadFlags      = LOAD_LIBRARY_SEARCH_SYSTEM32;
    HMODULE hModWinTrustDll = LoadLibraryExW(wszSysDll, NULL, fLoadFlags);
    if (!hModWinTrustDll && GetLastError() == ERROR_INVALID_PARAMETER)
    {
        fLoadFlags = 0;
        hModWinTrustDll = LoadLibraryExW(wszSysDll, NULL, fLoadFlags);
    }
    if (!hModWinTrustDll)
        return ErrorMsgRcSWSU(43, "Failed to load '", wszSysDll, "': ", GetLastError());

    /* Resolve API: */
    decltype(WinVerifyTrust) * const pfnWinVerifyTrust
        = (decltype(WinVerifyTrust) *)GetProcAddress(hModWinTrustDll, "WinVerifyTrust");
    if (!pfnWinVerifyTrust)
        return ErrorMsgRc(44, "WinVerifyTrust not found");

    /*
     * We also need the Crypt32.dll for CryptQueryObject and CryptMsgGetParam.
     */
    /* Load it: */
    static wchar_t const s_wszSlashCrypt32Dll[] = L"\\Crypt32.dll";
    AssertCompile(sizeof(s_wszSlashCrypt32Dll) <= sizeof(s_wszSlashWinTrustDll));
    memcpy(&wszSysDll[cwcSystem32], s_wszSlashCrypt32Dll, sizeof(s_wszSlashCrypt32Dll));
    HMODULE const hModCrypt32Dll = LoadLibraryExW(wszSysDll, NULL, fLoadFlags);
    if (!hModCrypt32Dll)
        return ErrorMsgRcSWSU(45, "Failed to load '", wszSysDll, "': ", GetLastError());

    /* Resolve APIs: */
    decltype(CryptQueryObject) * const pfnCryptQueryObject
        = (decltype(CryptQueryObject) *)GetProcAddress(hModCrypt32Dll, "CryptQueryObject");
    if (!pfnCryptQueryObject)
        return ErrorMsgRc(46, "CryptQueryObject not found");

    decltype(CryptMsgClose) * const    pfnCryptMsgClose
        = (decltype(CryptMsgClose) *)GetProcAddress(hModCrypt32Dll, "CryptMsgClose");
    if (!pfnCryptQueryObject)
        return ErrorMsgRc(47, "CryptMsgClose not found");

    decltype(CryptMsgGetParam) * const pfnCryptMsgGetParam
        = (decltype(CryptMsgGetParam) *)GetProcAddress(hModCrypt32Dll, "CryptMsgGetParam");
    if (!pfnCryptQueryObject)
        return ErrorMsgRc(48, "CryptMsgGetParam not found");

    /*
     * We'll verify the primary signer certificate first as that's something that
     * should work even if SHA-256 isn't supported by the Windows crypto code.
     */
    struct
    {
        HANDLE          hFile;
        wchar_t const  *pwszFile;

        DWORD           fEncoding;
        DWORD           dwContentType;
        DWORD           dwFormatType;
        HCERTSTORE      hCertStore;
        HCRYPTMSG       hMsg;

        DWORD           cbCert;
        uint8_t        *pbCert;
    } aExes[] =
    {
        { hFileSelf, pwszSelfPath,   0, 0, 0, NULL, NULL,   0, NULL },
        { hFileExe,  pwszExePath,    0, 0, 0, NULL, NULL,   0, NULL },
    };

    HANDLE const hHeap  = GetProcessHeap();
    int          rcExit = 0;
    for (unsigned i = 0; i < RT_ELEMENTS(aExes) && rcExit == 0; i++)
    {
        if (!pfnCryptQueryObject(CERT_QUERY_OBJECT_FILE,
                                 aExes[i].pwszFile,
                                 CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                                 CERT_QUERY_FORMAT_FLAG_BINARY,
                                 0 /*fFlags*/,
                                 &aExes[i].fEncoding,
                                 &aExes[i].dwContentType,
                                 &aExes[i].dwFormatType,
                                 &aExes[i].hCertStore,
                                 &aExes[i].hMsg,
                                 NULL /*ppvContext*/))
            rcExit = ErrorMsgRcSWSU(50 + i*4, "CryptQueryObject/FILE on '", aExes[i].pwszFile, "': ", GetLastError());
/** @todo this isn't getting us what we want. It's just accessing the
 *        certificates shipped with the signature. Sigh. */
        else if (!pfnCryptMsgGetParam(aExes[i].hMsg, CMSG_CERT_PARAM, 0, NULL, &aExes[i].cbCert))
            rcExit = ErrorMsgRcSWSU(51 + i*4, "CryptMsgGetParam/CMSG_CERT_PARAM/size failed on '",
                                    aExes[i].pwszFile, "': ", GetLastError());
        else
        {
            DWORD const cbCert = aExes[i].cbCert;
            aExes[i].pbCert = (uint8_t *)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, cbCert);
            if (!aExes[i].pbCert)
                rcExit = ErrorMsgRcSUS(52 + i*4, "Out of memory (", cbCert, " bytes) for signing certificate information");
            else if (!pfnCryptMsgGetParam(aExes[i].hMsg, CMSG_CERT_PARAM, 0, aExes[i].pbCert, &aExes[i].cbCert))
                rcExit = ErrorMsgRcSWSU(53 + i*4, "CryptMsgGetParam/CMSG_CERT_PARAM failed on '", aExes[i].pwszFile, "': ",
                                        GetLastError());
        }
    }

    if (rcExit == 0)
    {
        /* Do the two match? */
        if (   aExes[0].cbCert != aExes[1].cbCert
            || memcmp(aExes[0].pbCert, aExes[1].pbCert, aExes[0].cbCert) != 0)
            rcExit = ErrorMsgRcSWS(58, "The certificate used to sign '", pwszExePath, "' does not match.");
        /* The two match, now do they match the one we're expecting to use? */
        else if (   aExes[0].cbCert != g_cbBuildCert
                 || memcmp(aExes[0].pbCert, g_abBuildCert, g_cbBuildCert) != 0)
            rcExit = ErrorMsgRcSWS(59, "The signing certificate of '", pwszExePath, "' differs from the build certificate");
        /* else: it all looks fine */
    }

    /* cleanup */
    for (unsigned i = 0; i < RT_ELEMENTS(aExes); i++)
    {
        if (aExes[i].pbCert)
        {
            HeapFree(hHeap, 0, aExes[i].pbCert);
            aExes[i].pbCert = NULL;
        }
        if (aExes[i].hMsg)
        {
            pfnCryptMsgClose(aExes[i].hMsg);
            aExes[i].hMsg = NULL;
        }
    }
    if (rcExit != 0)
        return rcExit;

    /*
     * ASSUMING we're using SHA-256 for signing, we do a windows OS cutoff at Windows 7.
     * For Windows Vista and older we skip this step.
     */
    if (   (WinOsInfoEx.dwMajorVersion == 6 && WinOsInfoEx.dwMinorVersion == 0)
        || WinOsInfoEx.dwMajorVersion < 6)
        return 0;

    /*
     * Construct input WinVerifyTrust parameters and call it on each of the executables in turn.
     * This code was borrowed from SUPHardNt.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(aExes); i++)
    {
        WINTRUST_FILE_INFO FileInfo = { 0 };
        FileInfo.cbStruct      = sizeof(FileInfo);
        FileInfo.pcwszFilePath = aExes[i].pwszFile;
        FileInfo.hFile         = aExes[i].hFile;

        GUID PolicyActionGuid = WINTRUST_ACTION_GENERIC_VERIFY_V2;

        WINTRUST_DATA TrustData = { 0 };
        TrustData.cbStruct            = sizeof(TrustData);
        TrustData.fdwRevocationChecks = WTD_REVOKE_NONE;  /* Keep simple for now. */
        TrustData.dwStateAction       = WTD_STATEACTION_VERIFY;
        TrustData.dwUIChoice          = WTD_UI_NONE;
        TrustData.dwProvFlags         = 0;
        if (WinOsInfoEx.dwMajorVersion >= 6)
            TrustData.dwProvFlags     = WTD_CACHE_ONLY_URL_RETRIEVAL;
        else
            TrustData.dwProvFlags     = WTD_REVOCATION_CHECK_NONE;
        TrustData.dwUnionChoice       = WTD_CHOICE_FILE;
        TrustData.pFile               = &FileInfo;

        HRESULT hrc = pfnWinVerifyTrust(NULL /*hwnd*/, &PolicyActionGuid, &TrustData);
        if (hrc != S_OK)
        {
            /* Translate the eror constant */
            const char *pszErrConst = NULL;
            switch (hrc)
            {
                case TRUST_E_SYSTEM_ERROR:            pszErrConst = "TRUST_E_SYSTEM_ERROR";         break;
                case TRUST_E_NO_SIGNER_CERT:          pszErrConst = "TRUST_E_NO_SIGNER_CERT";       break;
                case TRUST_E_COUNTER_SIGNER:          pszErrConst = "TRUST_E_COUNTER_SIGNER";       break;
                case TRUST_E_CERT_SIGNATURE:          pszErrConst = "TRUST_E_CERT_SIGNATURE";       break;
                case TRUST_E_TIME_STAMP:              pszErrConst = "TRUST_E_TIME_STAMP";           break;
                case TRUST_E_BAD_DIGEST:              pszErrConst = "TRUST_E_BAD_DIGEST";           break;
                case TRUST_E_BASIC_CONSTRAINTS:       pszErrConst = "TRUST_E_BASIC_CONSTRAINTS";    break;
                case TRUST_E_FINANCIAL_CRITERIA:      pszErrConst = "TRUST_E_FINANCIAL_CRITERIA";   break;
                case TRUST_E_PROVIDER_UNKNOWN:        pszErrConst = "TRUST_E_PROVIDER_UNKNOWN";     break;
                case TRUST_E_ACTION_UNKNOWN:          pszErrConst = "TRUST_E_ACTION_UNKNOWN";       break;
                case TRUST_E_SUBJECT_FORM_UNKNOWN:    pszErrConst = "TRUST_E_SUBJECT_FORM_UNKNOWN"; break;
                case TRUST_E_SUBJECT_NOT_TRUSTED:     pszErrConst = "TRUST_E_SUBJECT_NOT_TRUSTED";  break;
                case TRUST_E_NOSIGNATURE:             pszErrConst = "TRUST_E_NOSIGNATURE";          break;
                case TRUST_E_FAIL:                    pszErrConst = "TRUST_E_FAIL";                 break;
                case TRUST_E_EXPLICIT_DISTRUST:       pszErrConst = "TRUST_E_EXPLICIT_DISTRUST";    break;
                case CERT_E_EXPIRED:                  pszErrConst = "CERT_E_EXPIRED";               break;
                case CERT_E_VALIDITYPERIODNESTING:    pszErrConst = "CERT_E_VALIDITYPERIODNESTING"; break;
                case CERT_E_ROLE:                     pszErrConst = "CERT_E_ROLE";                  break;
                case CERT_E_PATHLENCONST:             pszErrConst = "CERT_E_PATHLENCONST";          break;
                case CERT_E_CRITICAL:                 pszErrConst = "CERT_E_CRITICAL";              break;
                case CERT_E_PURPOSE:                  pszErrConst = "CERT_E_PURPOSE";               break;
                case CERT_E_ISSUERCHAINING:           pszErrConst = "CERT_E_ISSUERCHAINING";        break;
                case CERT_E_MALFORMED:                pszErrConst = "CERT_E_MALFORMED";             break;
                case CERT_E_UNTRUSTEDROOT:            pszErrConst = "CERT_E_UNTRUSTEDROOT";         break;
                case CERT_E_CHAINING:                 pszErrConst = "CERT_E_CHAINING";              break;
                case CERT_E_REVOKED:                  pszErrConst = "CERT_E_REVOKED";               break;
                case CERT_E_UNTRUSTEDTESTROOT:        pszErrConst = "CERT_E_UNTRUSTEDTESTROOT";     break;
                case CERT_E_REVOCATION_FAILURE:       pszErrConst = "CERT_E_REVOCATION_FAILURE";    break;
                case CERT_E_CN_NO_MATCH:              pszErrConst = "CERT_E_CN_NO_MATCH";           break;
                case CERT_E_WRONG_USAGE:              pszErrConst = "CERT_E_WRONG_USAGE";           break;
                case CERT_E_UNTRUSTEDCA:              pszErrConst = "CERT_E_UNTRUSTEDCA";           break;
                case CERT_E_INVALID_POLICY:           pszErrConst = "CERT_E_INVALID_POLICY";        break;
                case CERT_E_INVALID_NAME:             pszErrConst = "CERT_E_INVALID_NAME";          break;
                case CRYPT_E_FILE_ERROR:              pszErrConst = "CRYPT_E_FILE_ERROR";           break;
                case CRYPT_E_REVOKED:                 pszErrConst = "CRYPT_E_REVOKED";              break;
            }
            if (pszErrConst)
                rcExit = ErrorMsgRcSWSS(60 + i, "WinVerifyTrust failed on '", pwszExePath, "': ", pszErrConst);
            else
                rcExit = ErrorMsgRcSWSX(60 + i, "WinVerifyTrust failed on '", pwszExePath, "': ", hrc);
        }

        /* clean up state data. */
        TrustData.dwStateAction = WTD_STATEACTION_CLOSE;
        FileInfo.hFile          = NULL;
        hrc = pfnWinVerifyTrust(NULL /*hwnd*/, &PolicyActionGuid, &TrustData);
    }

    return rcExit;
}

# endif /* Using Windows APIs. */
#endif /* VBOX_SIGNING_MODE */

/**
 * strstr for haystacks w/o null termination.
 */
static const char *MyStrStrN(const char *pchHaystack, size_t cbHaystack, const char *pszNeedle)
{
    size_t const cchNeedle = strlen(pszNeedle);
    char const   chFirst   = *pszNeedle;
    if (cbHaystack >= cchNeedle)
    {
        cbHaystack -= cchNeedle - 1;
        while (cbHaystack > 0)
        {
            const char *pchHit = (const char *)memchr(pchHaystack, chFirst, cbHaystack);
            if (pchHit)
            {
                if (memcmp(pchHit, pszNeedle, cchNeedle) == 0)
                    return pchHit;
                pchHit++;
                cbHaystack -= pchHit - pchHaystack;
                pchHaystack = pchHit;
            }
            else
                break;
        }
    }
    return NULL;
}

/**
 * Check that the executable file is "related" the one for the current process.
 */
static int CheckThatFileIsRelated(wchar_t const *pwszExePath, wchar_t const *pwszSelfPath)
{
    /*
     * Start by checking version info.
     */
    /*
     * Query the version info for the files:
     */
    DWORD const cbExeVerInfo  = GetFileVersionInfoSizeW(pwszExePath, NULL);
    if (!cbExeVerInfo)
        return ErrorMsgRcSWSU(20, "GetFileVersionInfoSizeW failed on '", pwszExePath, "': ", GetLastError());

    DWORD const cbSelfVerInfo = GetFileVersionInfoSizeW(pwszSelfPath, NULL);
    if (!cbSelfVerInfo)
        return ErrorMsgRcSWSU(21, "GetFileVersionInfoSizeW failed on '", pwszSelfPath, "': ", GetLastError());

    HANDLE const hHeap         = GetProcessHeap();
    DWORD const  cbBothVerInfo = RT_ALIGN_32(cbExeVerInfo, 64) + RT_ALIGN_32(cbSelfVerInfo, 64);
    void * const pvExeVerInfo  = HeapAlloc(hHeap, HEAP_ZERO_MEMORY, cbBothVerInfo);
    void * const pvSelfVerInfo = (uint8_t *)pvExeVerInfo + RT_ALIGN_32(cbExeVerInfo, 64);
    if (!pvExeVerInfo)
        return ErrorMsgRcSUS(22, "Out of memory (", cbBothVerInfo, " bytes) for version info");

    int rcExit = 0;
    if (!GetFileVersionInfoW(pwszExePath, 0, cbExeVerInfo, pvExeVerInfo))
        rcExit = ErrorMsgRcSWSU(23, "GetFileVersionInfoW failed on '", pwszExePath, "': ", GetLastError());
    else if (!GetFileVersionInfoW(pwszSelfPath, 0, cbSelfVerInfo, pvSelfVerInfo))
        rcExit = ErrorMsgRcSWSU(24, "GetFileVersionInfoW failed on '", pwszSelfPath, "': ", GetLastError());

    /*
     * Compare the product and company strings, which should be identical.
     */
    static struct
    {
        wchar_t const *pwszQueryItem;
        const char    *pszQueryErrorMsg1;
        const char    *pszCompareErrorMsg1;
    } const s_aIdenticalItems[] =
    {
        { L"\\StringFileInfo\\040904b0\\ProductName", "VerQueryValueW/ProductName failed on '", "Product string of '" },
        { L"\\StringFileInfo\\040904b0\\CompanyName", "VerQueryValueW/CompanyName failed on '", "Company string of '" },
    };

    for (unsigned i = 0; i < RT_ELEMENTS(s_aIdenticalItems) && rcExit == 0; i++)
    {
        void *pvExeInfoItem  = NULL;
        UINT  cbExeInfoItem  = 0;
        void *pvSelfInfoItem = NULL;
        UINT  cbSelfInfoItem = 0;
        if (!VerQueryValueW(pvExeVerInfo, s_aIdenticalItems[i].pwszQueryItem, &pvExeInfoItem, &cbExeInfoItem))
            rcExit = ErrorMsgRcSWSU(25 + i*3, s_aIdenticalItems[i].pszQueryErrorMsg1 , pwszExePath, "': ", GetLastError());
        else  if (!VerQueryValueW(pvSelfVerInfo, s_aIdenticalItems[i].pwszQueryItem, &pvSelfInfoItem, &cbSelfInfoItem))
            rcExit = ErrorMsgRcSWSU(26 + i*3, s_aIdenticalItems[i].pszQueryErrorMsg1, pwszSelfPath, "': ", GetLastError());
        else if (   cbExeInfoItem != cbSelfInfoItem
                 || memcmp(pvExeInfoItem, pvSelfInfoItem, cbSelfInfoItem) != 0)
            rcExit = ErrorMsgRcSWS(27 + i*3, s_aIdenticalItems[i].pszCompareErrorMsg1, pwszExePath, "' does not match");
    }

    HeapFree(hHeap, 0, pvExeVerInfo);

    /*
     * Check that the file has a manifest that looks like it may belong to
     * an NSIS installer.
     */
    if (rcExit == 0)
    {
        HMODULE hMod = LoadLibraryExW(pwszExePath, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if (!hMod && GetLastError() == ERROR_INVALID_PARAMETER)
            hMod     = LoadLibraryExW(pwszExePath, NULL, LOAD_LIBRARY_AS_DATAFILE);
        if (hMod)
        {
            HRSRC const hRsrcMt = FindResourceExW(hMod, RT_MANIFEST, MAKEINTRESOURCEW(1),
                                                  MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
            if (hRsrcMt)
            {
                DWORD const   cbManifest = SizeofResource(hMod, hRsrcMt);
                HGLOBAL const hGlobalMt  = LoadResource(hMod, hRsrcMt);
                if (hGlobalMt)
                {
                    const char * const pchManifest = (const char *)LockResource(hGlobalMt);
                    if (pchManifest)
                    {
                        /* Just look for a few strings we expect to always find the manifest. */
                        if (!MyStrStrN(pchManifest, cbManifest, "Nullsoft.NSIS.exehead"))
                            rcExit = 36;
                        else if (!MyStrStrN(pchManifest, cbManifest, "requestedPrivileges"))
                            rcExit = 37;
                        else if (!MyStrStrN(pchManifest, cbManifest, "highestAvailable"))
                            rcExit = 38;
                        if (rcExit)
                            rcExit = ErrorMsgRcSWSU(rcExit, "Manifest check of '", pwszExePath, "' failed: ", rcExit);
                    }
                    else
                        rcExit = ErrorMsgRc(35, "LockResource/Manifest failed");
                }
                else
                    rcExit = ErrorMsgRcSU(34, "LoadResource/Manifest failed: ", GetLastError());
            }
            else
                rcExit = ErrorMsgRcSU(33, "FindResourceExW/Manifest failed: ", GetLastError());
        }
        else
            rcExit = ErrorMsgRcSWSU(32, "LoadLibraryExW of '", pwszExePath, "' as datafile failed: ", GetLastError());
    }

    return rcExit;
}

static BOOL IsWow64(void)
{
    BOOL fIsWow64 = FALSE;
    typedef BOOL (WINAPI *LPFN_ISWOW64PROCESS)(HANDLE, PBOOL);
    LPFN_ISWOW64PROCESS fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress(GetModuleHandleW(L"kernel32"), "IsWow64Process");
    if (fnIsWow64Process != NULL)
    {
        if (!fnIsWow64Process(GetCurrentProcess(), &fIsWow64))
        {
            ErrorMsgLastErr("Unable to determine the process type!");

            /* Error in retrieving process type - assume that we're running on 32bit. */
            fIsWow64 = FALSE;
        }
    }
    return fIsWow64;
}

/**
 * Detects the native host platform and returns the appropriate installer
 * executable suffix for it.
 *
 * @returns Architecture stuff string.
 */
static const wchar_t *GetNativeArchInstallerSuffix(void)
{
    HMODULE const      hModKernel32 = GetModuleHandleW(L"kernel32.dll");
    PFNISWOW64PROCESS2 pfnIsWow64Process2 = (PFNISWOW64PROCESS2)GetProcAddress(hModKernel32, "IsWow64Process2");
    if (pfnIsWow64Process2)
    {
        USHORT usWowMachine  = IMAGE_FILE_MACHINE_UNKNOWN;
        USHORT usHostMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        if (pfnIsWow64Process2(GetCurrentProcess(), &usWowMachine, &usHostMachine))
        {
            if (usHostMachine == IMAGE_FILE_MACHINE_AMD64)
                return L"-amd64.exe";
            if (usHostMachine == IMAGE_FILE_MACHINE_ARM64)
                return L"-arm64.exe";
            if (usHostMachine == IMAGE_FILE_MACHINE_I386)
                return L"-x86.exe";
            ErrorMsgSU("IsWow64Process2 return unknown host machine value: ", usHostMachine);
        }
        else
            ErrorMsgLastErr("Unable to determine the process type! (#2)");
    }
    else
    {
        PFNISWOW64PROCESS pfnIsWow64Process = (PFNISWOW64PROCESS)GetProcAddress(hModKernel32, "IsWow64Process");
        if (pfnIsWow64Process)
        {
            BOOL fIsWow64 = TRUE;
            if (pfnIsWow64Process(GetCurrentProcess(), &fIsWow64))
            {
                if (fIsWow64)
                    return L"-amd64.exe";
            }
            else
                ErrorMsgLastErr("Unable to determine the process type!");
        }
    }

#ifdef RT_ARCH_X86
    return L"-x86.exe";
#elif defined(RT_ARCH_AMD64)
    return L"-amd64.exe";
#elif defined(RT_ARCH_ARM64)
    return L"-arm64.exe";
#else
# error "port me"
#endif
}

static int WaitForProcess2(HANDLE hProcess)
{
    /*
     * Wait for the process, make sure the deal with messages.
     */
    for (;;)
    {
        DWORD dwRc = MsgWaitForMultipleObjects(1, &hProcess, FALSE, 5000/*ms*/, QS_ALLEVENTS);

        MSG Msg;
        while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Msg);
            DispatchMessageW(&Msg);
        }

        if (dwRc == WAIT_OBJECT_0)
            break;
        if (   dwRc != WAIT_TIMEOUT
            && dwRc != WAIT_OBJECT_0 + 1)
        {
            ErrorMsgLastErrSUR("MsgWaitForMultipleObjects failed: ", dwRc);
            break;
        }
    }

    /*
     * Collect the process info.
     */
    DWORD dwExitCode;
    if (GetExitCodeProcess(hProcess, &dwExitCode))
        return (int)dwExitCode;
    return ErrorMsgRcLastErr(16, "GetExitCodeProcess failed");
}

static int WaitForProcess(HANDLE hProcess)
{
    DWORD WaitRc = WaitForSingleObjectEx(hProcess, INFINITE, TRUE);
    while (   WaitRc == WAIT_IO_COMPLETION
           || WaitRc == WAIT_TIMEOUT)
        WaitRc = WaitForSingleObjectEx(hProcess, INFINITE, TRUE);
    if (WaitRc == WAIT_OBJECT_0)
    {
        DWORD dwExitCode;
        if (GetExitCodeProcess(hProcess, &dwExitCode))
            return (int)dwExitCode;
        return ErrorMsgRcLastErr(16, "GetExitCodeProcess failed");
    }
    return ErrorMsgRcLastErrSUR(16, "MsgWaitForMultipleObjects failed: ", WaitRc);
}

#ifndef IPRT_NO_CRT
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main()
#endif
{
#ifndef IPRT_NO_CRT
    RT_NOREF(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
#endif

    /*
     * Gather the parameters of the real installer program.
     */
    SetLastError(NO_ERROR);
    WCHAR wszCurDir[MAX_PATH] = { 0 };
    DWORD cwcCurDir = GetCurrentDirectoryW(sizeof(wszCurDir), wszCurDir);
    if (cwcCurDir == 0 || cwcCurDir >= sizeof(wszCurDir))
        return ErrorMsgRcLastErrSUR(12, "GetCurrentDirectoryW failed: ", cwcCurDir);

    SetLastError(NO_ERROR);
    WCHAR wszExePath[MAX_PATH] = { 0 };
    size_t cwcExePath = GetModuleFileNameW(NULL, wszExePath, sizeof(wszExePath));
    if (cwcExePath == 0 || cwcExePath >= sizeof(wszExePath))
        return ErrorMsgRcLastErrSUR(13, "GetModuleFileNameW failed: ", cwcExePath);

    WCHAR wszSelfPath[MAX_PATH];
    memcpy(wszSelfPath, wszExePath, sizeof(wszSelfPath));

    /*
    * Strip the extension off the module name and construct the arch specific
    * one of the real installer program.
    */
    size_t off = cwcExePath - 1;
    while (   off > 0
           && (   wszExePath[off] != '/'
               && wszExePath[off] != '\\'
               && wszExePath[off] != ':'))
    {
        if (wszExePath[off] == '.')
        {
            wszExePath[off] = '\0';
            cwcExePath = off;
            break;
        }
        off--;
    }

    WCHAR const * const pwszSuff = GetNativeArchInstallerSuffix();
    int rc = RTUtf16Copy(&wszExePath[cwcExePath], RT_ELEMENTS(wszExePath) - cwcExePath, pwszSuff);
    if (RT_FAILURE(rc))
        return ErrorMsgRc(14, "Real installer name is too long!");
    cwcExePath += RTUtf16Len(&wszExePath[cwcExePath]);

    /*
     * Replace the first argument of the argument list.
     */
    PWCHAR  pwszNewCmdLine = NULL;
    LPCWSTR pwszOrgCmdLine = GetCommandLineW();
    if (pwszOrgCmdLine) /* Dunno if this can be NULL, but whatever. */
    {
        /* Skip the first argument in the original. */
        /** @todo Is there some ISBLANK or ISSPACE macro/function in Win32 that we could
         *        use here, if it's correct wrt. command line conventions? */
        WCHAR wch;
        while ((wch = *pwszOrgCmdLine) == L' ' || wch == L'\t')
            pwszOrgCmdLine++;
        if (wch == L'"')
        {
            pwszOrgCmdLine++;
            while ((wch = *pwszOrgCmdLine) != L'\0')
            {
                pwszOrgCmdLine++;
                if (wch == L'"')
                    break;
            }
        }
        else
        {
            while ((wch = *pwszOrgCmdLine) != L'\0')
            {
                pwszOrgCmdLine++;
                if (wch == L' ' || wch == L'\t')
                    break;
            }
        }
        while ((wch = *pwszOrgCmdLine) == L' ' || wch == L'\t')
            pwszOrgCmdLine++;

        /* Join up "wszExePath" with the remainder of the original command line. */
        size_t cwcOrgCmdLine = RTUtf16Len(pwszOrgCmdLine);
        size_t cwcNewCmdLine = 1 + cwcExePath + 1 + 1 + cwcOrgCmdLine + 1;
        PWCHAR pwsz = pwszNewCmdLine = (PWCHAR)LocalAlloc(LPTR, cwcNewCmdLine * sizeof(WCHAR));
        if (!pwsz)
            return ErrorMsgRcSUS(15, "Out of memory (", cwcNewCmdLine * sizeof(WCHAR), " bytes)");
        *pwsz++ = L'"';
        memcpy(pwsz, wszExePath, cwcExePath * sizeof(pwsz[0]));
        pwsz += cwcExePath;
        *pwsz++ = L'"';
        if (cwcOrgCmdLine)
        {
            *pwsz++ = L' ';
            memcpy(pwsz, pwszOrgCmdLine, cwcOrgCmdLine * sizeof(pwsz[0]));
        }
        else
        {
            *pwsz = L'\0';
            pwszOrgCmdLine = NULL;
        }
    }

    /*
     * Open the executable for this process.
     */
    HANDLE hFileSelf = CreateFileW(wszSelfPath, GENERIC_READ, FILE_SHARE_READ, NULL /*pSecAttr*/, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (hFileSelf == INVALID_HANDLE_VALUE && GetLastError() == ERROR_INVALID_PARAMETER)
        hFileSelf    = CreateFileW(wszSelfPath, GENERIC_READ, FILE_SHARE_READ, NULL /*pSecAttr*/, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFileSelf == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
            return ErrorMsgRcSW(17, "File not found: ", wszSelfPath);
        return ErrorMsgRcSWSU(17, "Error opening '", wszSelfPath, "' for reading: ", GetLastError());
    }


    /*
     * Open the file we're about to execute.
     */
    HANDLE hFileExe = CreateFileW(wszExePath, GENERIC_READ, FILE_SHARE_READ, NULL /*pSecAttr*/, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (hFileExe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_INVALID_PARAMETER)
        hFileExe    = CreateFileW(wszExePath, GENERIC_READ, FILE_SHARE_READ, NULL /*pSecAttr*/, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFileExe == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
            return ErrorMsgRcSW(18, "File not found: ", wszExePath);
        return ErrorMsgRcSWSU(18, "Error opening '", wszExePath, "' for reading: ", GetLastError());
    }

    /*
     * Check that the file we're about to launch is related to us and safe to start.
     */
    int rcExit = CheckThatFileIsRelated(wszExePath, wszSelfPath);
    if (rcExit != 0)
        return rcExit;

#ifdef VBOX_SIGNING_MODE
# if 1 /* Use the IPRT code as it it will work on all windows versions without trouble.
          Added some 800KB to the executable, but so what. */
    rcExit = CheckFileSignatureIprt(wszExePath);
# else
    rcExit = CheckFileSignatures(wszExePath, hFileExe, wszSelfPath, hFileSelf);
# endif
    if (rcExit != 0)
        return rcExit;
#endif

    /*
     * Start the process.
     */
    STARTUPINFOW        StartupInfo = { sizeof(StartupInfo), 0 };
    PROCESS_INFORMATION ProcInfo    = { 0 };
    SetLastError(740);
    BOOL fOk = CreateProcessW(wszExePath,
                              pwszNewCmdLine,
                              NULL /*pProcessAttributes*/,
                              NULL /*pThreadAttributes*/,
                              TRUE /*fInheritHandles*/,
                              0    /*dwCreationFlags*/,
                              NULL /*pEnvironment*/,
                              NULL /*pCurrentDirectory*/,
                              &StartupInfo,
                              &ProcInfo);
    if (fOk)
    {
        /* Wait for the process to finish. */
        CloseHandle(ProcInfo.hThread);
        rcExit = WaitForProcess(ProcInfo.hProcess);
        CloseHandle(ProcInfo.hProcess);
    }
    else if (GetLastError() == ERROR_ELEVATION_REQUIRED)
    {
        /*
         * Elevation is required. That can be accomplished via ShellExecuteEx
         * and the runas atom.
         */
        MSG Msg;
        PeekMessage(&Msg, NULL, 0, 0, PM_NOREMOVE);
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        SHELLEXECUTEINFOW ShExecInfo = { 0 };
        ShExecInfo.cbSize       = sizeof(SHELLEXECUTEINFOW);
        ShExecInfo.fMask        = SEE_MASK_NOCLOSEPROCESS;
        ShExecInfo.hwnd         = NULL;
        ShExecInfo.lpVerb       = L"runas" ;
        ShExecInfo.lpFile       = wszExePath;
        ShExecInfo.lpParameters = pwszOrgCmdLine; /* pass only args here!!! */
        ShExecInfo.lpDirectory  = wszCurDir;
        ShExecInfo.nShow        = SW_NORMAL;
        ShExecInfo.hProcess     = INVALID_HANDLE_VALUE;
        if (ShellExecuteExW(&ShExecInfo))
        {
            if (ShExecInfo.hProcess != INVALID_HANDLE_VALUE)
            {
                rcExit = WaitForProcess2(ShExecInfo.hProcess);
                CloseHandle(ShExecInfo.hProcess);
            }
            else
                rcExit = ErrorMsgRc(1, "ShellExecuteExW did not return a valid process handle!");
        }
        else
            rcExit = ErrorMsgRcLastErrSWSR(9, "Failed to execute '", wszExePath, "' via ShellExecuteExW!");
    }
    else
        rcExit = ErrorMsgRcLastErrSWSR(8, "Failed to execute '", wszExePath, "' via CreateProcessW!");

    if (pwszNewCmdLine)
        LocalFree(pwszNewCmdLine);

    return rcExit;
}

