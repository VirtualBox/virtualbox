/* $Id: VBoxDrvInst.cpp 107166 2024-11-26 09:03:23Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxDrvInst - Driver and service installation helper for Windows guests.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#ifndef UNICODE
# define UNICODE
#endif

#include <iprt/alloca.h>
#include <VBox/version.h>

#include <iprt/win/windows.h>
#include <iprt/win/setupapi.h>
#include <newdev.h> /* For INSTALLFLAG_XXX. */
#include <cfgmgr32.h> /* For MAX_DEVICE_ID_LEN. */
#include <devguid.h>
#include <RegStr.h>
#ifdef RT_ARCH_X86
# include <wintrust.h>
# include <softpub.h>
#endif

#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/once.h>
#include <iprt/path.h>      /* RTPATH_IS_SEP */
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/utf16.h>

#include <VBox/GuestHost/VBoxWinDrvInst.h>

/* Exit codes */
#define EXIT_OK      (0)
#define EXIT_REBOOT  (1)
#define EXIT_FAIL    (2)
#define EXIT_USAGE   (3)

/* Must include after EXIT_FAIL was defined! Sorry for the mixing up of thing. */
#include "NoCrtOutput.h"


/*********************************************************************************************************************************
*   Defines                                                                                                                      *
*********************************************************************************************************************************/
/* Registry string list flags */
#define VBOX_REG_STRINGLIST_NONE                0x00000000        /**< No flags set. */
#define VBOX_REG_STRINGLIST_ALLOW_DUPLICATES    0x00000001        /**< Allows duplicates in list when adding a value. */

/** NT4: The video service name. */
#define VBOXGUEST_NT4_VIDEO_NAME                "VBoxVideo"
/** NT4: The video inf file name */
#define VBOXGUEST_NT4_VIDEO_INF_NAME            "VBoxVideoEarlyNT.inf"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static char *ArgToUtf8(wchar_t const *pwszString, const char *pszArgName)
{
    char *pszUtf8 = NULL;
    int rc = RTUtf16ToUtf8(pwszString, &pszUtf8);
    if (RT_SUCCESS(rc))
        return pszUtf8;
    ErrorMsgBegin("RTUtf16ToUtf8 failed on '");
    ErrorMsgStr(pszArgName);
    ErrorMsgStr("': ");
    ErrorMsgErrVal(rc, true);
    ErrorMsgEnd(NULL);
    return NULL;
}

/**
 * @returns false.
 * @note Frees pszValue
 */
static bool ErrorArtToNum(int rc, const char *pszArgName, char *pszValue)
{
    ErrorMsgBegin("Failed to convert the '");
    ErrorMsgStr(pszArgName);
    ErrorMsgStr("' value '");
    ErrorMsgStr(pszValue);
    ErrorMsgStr("' to a number: ");
    ErrorMsgErrVal(rc, true);
    ErrorMsgEnd(NULL);
    return false;
}


static bool ArgToUInt32Full(wchar_t const *pwszString, const char *pszArgName, uint32_t *puValue)
{
    char *pszValue = ArgToUtf8(pwszString, pszArgName);
    if (!pszValue)
        return false;
    int rc = RTStrToUInt32Full(pszValue, 0, puValue);
    if (RT_FAILURE(rc))
        return ErrorArtToNum(rc, pszArgName, pszValue);
    RTStrFree(pszValue);
    return true;
}


static bool ArgToUInt64Full(wchar_t const *pwszString, const char *pszArgName, uint64_t *puValue)
{
    char *pszValue = ArgToUtf8(pwszString, pszArgName);
    if (!pszValue)
        return false;
    int rc = RTStrToUInt64Full(pszValue, 0, puValue);
    if (rc != VINF_SUCCESS)
        return ErrorArtToNum(rc, pszArgName, pszValue);
    RTStrFree(pszValue);
    return true;
}



static bool GetErrorMsg(DWORD dwLastError, wchar_t *pwszMsg, DWORD cwcMsg)
{
    if (FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, dwLastError, 0, pwszMsg, cwcMsg, NULL) == 0)
        return false;
    wchar_t *pwc = RTUtf16Chr(pwszMsg, '\r');
    if (pwc)
        *pwc = '\0';
    return true;
}


#ifdef RT_ARCH_X86

/**
 * Interceptor WinVerifyTrust function for SetupApi.dll on Windows 2000, XP,
 * W2K3 and XP64.
 *
 * This crudely modifies the driver verification request from a WHQL/logo driver
 * check to a simple Authenticode check.
 */
static LONG WINAPI InterceptedWinVerifyTrust(HWND hwnd, GUID *pActionId, void *pvData)
{
    /*
     * Resolve the real WinVerifyTrust function.
     */
    static decltype(WinVerifyTrust) * volatile s_pfnRealWinVerifyTrust = NULL;
    decltype(WinVerifyTrust) *pfnRealWinVerifyTrust = s_pfnRealWinVerifyTrust;
    if (!pfnRealWinVerifyTrust)
    {
        HMODULE hmod = GetModuleHandleW(L"WINTRUST.DLL");
        if (!hmod)
            hmod = LoadLibraryW(L"WINTRUST.DLL");
        if (!hmod)
        {
            ErrorMsgLastErr("InterceptedWinVerifyTrust: Failed to load wintrust.dll");
            return TRUST_E_SYSTEM_ERROR;
        }
        pfnRealWinVerifyTrust = (decltype(WinVerifyTrust) *)GetProcAddress(hmod, "WinVerifyTrust");
        if (!pfnRealWinVerifyTrust)
        {
            ErrorMsg("InterceptedWinVerifyTrust: Failed to locate WinVerifyTrust in wintrust.dll");
            return TRUST_E_SYSTEM_ERROR;
        }
        s_pfnRealWinVerifyTrust = pfnRealWinVerifyTrust;
    }

    /*
     * Modify the ID if appropriate.
     */
    static const GUID s_GuidDriverActionVerify       = DRIVER_ACTION_VERIFY;
    static const GUID s_GuidActionGenericChainVerify = WINTRUST_ACTION_GENERIC_CHAIN_VERIFY;
    static const GUID s_GuidActionGenericVerify2     = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    if (pActionId)
    {
        if (memcmp(pActionId, &s_GuidDriverActionVerify, sizeof(*pActionId)) == 0)
        {
            /** @todo don't apply to obvious NT components... */
            PrintStr("DRIVER_ACTION_VERIFY: Changing it to WINTRUST_ACTION_GENERIC_VERIFY_V2\r\n");
            pActionId = (GUID *)&s_GuidActionGenericVerify2;
        }
        else if (memcmp(pActionId, &s_GuidActionGenericChainVerify, sizeof(*pActionId)) == 0)
            PrintStr("WINTRUST_ACTION_GENERIC_CHAIN_VERIFY\r\n");
        else if (memcmp(pActionId, &s_GuidActionGenericVerify2, sizeof(*pActionId)) == 0)
            PrintStr("WINTRUST_ACTION_GENERIC_VERIFY_V2\r\n");
        else
            PrintStr("WINTRUST_ACTION_UNKNOWN\r\n");
    }

    /*
     * Log the data.
     */
    if (pvData)
    {
        WINTRUST_DATA *pData = (WINTRUST_DATA *)pvData;
        PrintSXS("                  cbStruct = ", pData->cbStruct, "\r\n");
# ifdef DEBUG
        PrintSXS("                dwUIChoice = ", pData->dwUIChoice, "\r\n");
        PrintSXS("       fdwRevocationChecks = ", pData->fdwRevocationChecks, "\r\n");
        PrintSXS("             dwStateAction = ", pData->dwStateAction, "\r\n");
        PrintSXS("             hWVTStateData = ", (uintptr_t)pData->hWVTStateData, "\r\n");
# endif
        if (pData->cbStruct >= 7*sizeof(uint32_t))
        {
            switch (pData->dwUnionChoice)
            {
                case WTD_CHOICE_FILE:
                    PrintSXS("                     pFile = ", (uintptr_t)pData->pFile, "\r\n");
                    if (RT_VALID_PTR(pData->pFile))
                    {
                        PrintSXS("           pFile->cbStruct = ", pData->pFile->cbStruct, "\r\n");
# ifndef DEBUG
                        if (pData->pFile->hFile)
# endif
                            PrintSXS("              pFile->hFile = ", (uintptr_t)pData->pFile->hFile, "\r\n");
                        if (RT_VALID_PTR(pData->pFile->pcwszFilePath))
                            PrintSWS("      pFile->pcwszFilePath = '", pData->pFile->pcwszFilePath, "'\r\n");
# ifdef DEBUG
                        else
                            PrintSXS("      pFile->pcwszFilePath = ", (uintptr_t)pData->pFile->pcwszFilePath, "\r\n");
                        PrintSXS("     pFile->pgKnownSubject = ", (uintptr_t)pData->pFile->pgKnownSubject, "\r\n");
# endif
                    }
                    break;

                case WTD_CHOICE_CATALOG:
                    PrintSXS("                  pCatalog = ", (uintptr_t)pData->pCatalog, "\r\n");
                    if (RT_VALID_PTR(pData->pCatalog))
                    {
                        PrintSXS("            pCat->cbStruct = ", pData->pCatalog->cbStruct, "\r\n");
# ifdef DEBUG
                        PrintSXS("    pCat->dwCatalogVersion = ", pData->pCatalog->dwCatalogVersion, "\r\n");
# endif
                        if (RT_VALID_PTR(pData->pCatalog->pcwszCatalogFilePath))
                            PrintSWS("pCat->pcwszCatalogFilePath = '", pData->pCatalog->pcwszCatalogFilePath, "'\r\n");
# ifdef DEBUG
                        else
                            PrintSXS("pCat->pcwszCatalogFilePath = ", (uintptr_t)pData->pCatalog->pcwszCatalogFilePath, "\r\n");
# endif
                        if (RT_VALID_PTR(pData->pCatalog->pcwszMemberTag))
                            PrintSWS("      pCat->pcwszMemberTag = '", pData->pCatalog->pcwszMemberTag, "'\r\n");
# ifdef DEBUG
                        else
                            PrintSXS("      pCat->pcwszMemberTag = ", (uintptr_t)pData->pCatalog->pcwszMemberTag, "\r\n");
# endif
                        if (RT_VALID_PTR(pData->pCatalog->pcwszMemberFilePath))
                            PrintSWS(" pCat->pcwszMemberFilePath = '", pData->pCatalog->pcwszMemberFilePath, "'\r\n");
# ifdef DEBUG
                        else
                            PrintSXS(" pCat->pcwszMemberFilePath = ", (uintptr_t)pData->pCatalog->pcwszMemberFilePath, "\r\n");
# else
                        if (pData->pCatalog->hMemberFile)
# endif
                            PrintSXS("         pCat->hMemberFile = ", (uintptr_t)pData->pCatalog->hMemberFile, "\r\n");
# ifdef DEBUG
                        PrintSXS("pCat->pbCalculatedFileHash = ", (uintptr_t)pData->pCatalog->pbCalculatedFileHash, "\r\n");
                        PrintSXS("pCat->cbCalculatedFileHash = ", pData->pCatalog->cbCalculatedFileHash, "\r\n");
                        PrintSXS("    pCat->pcCatalogContext = ", (uintptr_t)pData->pCatalog->pcCatalogContext, "\r\n");
# endif
                    }
                    break;

                case WTD_CHOICE_BLOB:
                    PrintSXS("                     pBlob = ", (uintptr_t)pData->pBlob, "\r\n");
                    break;

                case WTD_CHOICE_SIGNER:
                    PrintSXS("                     pSgnr = ", (uintptr_t)pData->pSgnr, "\r\n");
                    break;

                case WTD_CHOICE_CERT:
                    PrintSXS("                     pCert = ", (uintptr_t)pData->pCert, "\r\n");
                    break;

                default:
                    PrintSXS("             dwUnionChoice = ", pData->dwUnionChoice, "\r\n");
                    break;
            }
        }
    }

    /*
     * Make the call.
     */
    PrintStr("Calling WinVerifyTrust ...\r\n");
    LONG iRet = pfnRealWinVerifyTrust(hwnd, pActionId, pvData);
    PrintSXS("WinVerifyTrust returns ", (ULONG)iRet, "\r\n");

    return iRet;
}


/**
 * Installs an WinVerifyTrust interceptor in setupapi.dll on Windows 2000, XP,
 * W2K3 and XP64.
 *
 * This is a very crude hack to lower the WHQL check to just require a valid
 * Authenticode signature by intercepting the verification call.
 *
 * @return Ignored, just a convenience for saving space in error paths.
 */
static int InstallWinVerifyTrustInterceptorInSetupApi(void)
{
    /* Check the version: */
    OSVERSIONINFOW VerInfo = { sizeof(VerInfo) };
    GetVersionExW(&VerInfo);
    if (VerInfo.dwMajorVersion != 5)
        return 1;

    /* The the target module: */
    HMODULE hModSetupApi = GetModuleHandleW(L"SETUPAPI.DLL");
    if (!hModSetupApi)
        return ErrorMsgLastErr("Failed to locate SETUPAPI.DLL in the process");

    /*
     * Find the delayed import table (at least that's how it's done in the RTM).
     */
    IMAGE_DOS_HEADER const *pDosHdr = (IMAGE_DOS_HEADER const *)hModSetupApi;
    IMAGE_NT_HEADERS const *pNtHdrs = (IMAGE_NT_HEADERS const *)(  (uintptr_t)hModSetupApi
                                                                 + (  pDosHdr->e_magic == IMAGE_DOS_SIGNATURE
                                                                    ? pDosHdr->e_lfanew : 0));
    if (pNtHdrs->Signature != IMAGE_NT_SIGNATURE)
        return ErrorMsgSU("Failed to parse SETUPAPI.DLL for WinVerifyTrust interception: #", 1);
    if (pNtHdrs->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC)
        return ErrorMsgSU("Failed to parse SETUPAPI.DLL for WinVerifyTrust interception: #", 2);
    if (pNtHdrs->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT)
        return ErrorMsgSU("Failed to parse SETUPAPI.DLL for WinVerifyTrust interception: #", 3);

    uint32_t const cbDir = pNtHdrs->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size;
    if (cbDir < sizeof(IMAGE_DELAYLOAD_DESCRIPTOR))
        return ErrorMsgSU("Failed to parse SETUPAPI.DLL for WinVerifyTrust interception: #", 4);
    uint32_t const cbImages = pNtHdrs->OptionalHeader.SizeOfImage;
    if (cbDir >= cbImages)
        return ErrorMsgSU("Failed to parse SETUPAPI.DLL for WinVerifyTrust interception: #", 5);
    uint32_t const offDir = pNtHdrs->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress;
    if (offDir > cbImages - cbDir)
        return ErrorMsgSU("Failed to parse SETUPAPI.DLL for WinVerifyTrust interception: #", 6);

    /*
     * Scan the entries looking for wintrust.dll.
     */
    IMAGE_DELAYLOAD_DESCRIPTOR const * const paEntries = (IMAGE_DELAYLOAD_DESCRIPTOR const *)((uintptr_t)hModSetupApi + offDir);
    uint32_t const                           cEntries  = cbDir / sizeof(paEntries[0]);
    for (uint32_t iImp = 0; iImp < cEntries; iImp++)
    {
        const char * const pchRva2Ptr = paEntries[iImp].Attributes.RvaBased ? (const char *)hModSetupApi : (const char *)0;
        const char * const pszDllName = &pchRva2Ptr[paEntries[iImp].DllNameRVA];
        if (RTStrICmpAscii(pszDllName, "WINTRUST.DLL") == 0)
        {
            /*
             * Scan the symbol names.
             */
            uint32_t const    cbHdrs      = pNtHdrs->OptionalHeader.SizeOfHeaders;
            uint32_t * const  pauNameRvas = (uint32_t  *)&pchRva2Ptr[paEntries[iImp].ImportNameTableRVA];
            uintptr_t * const paIat       = (uintptr_t *)&pchRva2Ptr[paEntries[iImp].ImportAddressTableRVA];
            for (uint32_t iSym = 0; pauNameRvas[iSym] != NULL; iSym++)
            {
                IMAGE_IMPORT_BY_NAME const * const pName = (IMAGE_IMPORT_BY_NAME const *)&pchRva2Ptr[pauNameRvas[iSym]];
                if (RTStrCmp(pName->Name, "WinVerifyTrust") == 0)
                {
                    PrintSXS("Intercepting WinVerifyTrust for SETUPAPI.DLL (old: ", paIat[iSym], ")\r\n");
                    paIat[iSym] = (uintptr_t)InterceptedWinVerifyTrust;
                    return 0;
                }
            }
            return ErrorMsgSU("Failed to parse SETUPAPI.DLL for WinVerifyTrust interception: #", 9);
        }
    }
    return ErrorMsgSU("Failed to parse SETUPAPI.DLL for WinVerifyTrust interception: #", 10);
}

#endif /* RT_ARCH_X86 */

/**
 * Loads a DLL from the same directory as the installer.
 *
 * @returns Module handle, NULL on failure (fully messaged).
 * @param   pwszName            The DLL name.
 */
static HMODULE LoadAppDll(const wchar_t *pwszName)
{
    /* Get the process image path. */
    WCHAR  wszPath[MAX_PATH];
    UINT   cwcPath = GetModuleFileNameW(NULL, wszPath, MAX_PATH);
    if (!cwcPath || cwcPath >= MAX_PATH)
    {
        ErrorMsgLastErr("LoadAppDll: GetModuleFileNameW failed");
        return NULL;
    }

    /* Drop the image filename. */
    do
    {
        cwcPath--;
        if (RTPATH_IS_SEP(wszPath[cwcPath]))
        {
            cwcPath++;
            wszPath[cwcPath] = '\0';
            break;
        }
    } while (cwcPath > 0);

    if (!cwcPath) /* This should be impossible */
    {
        ErrorMsg("LoadAppDll: GetModuleFileNameW returned no path!");
        return NULL;
    }

    /* Append the dll name if we can. */
    size_t const cwcName = RTUtf16Len(pwszName);
    if (cwcPath + cwcName >= RT_ELEMENTS(wszPath))
    {
        ErrorMsgSWSWS("LoadAppDll: Path '", wszPath, "' too long when adding '", pwszName, "'");
        return NULL;
    }
    memcpy(&wszPath[cwcPath], pwszName, (cwcName + 1) * sizeof(wszPath[0]));

    /* Try load the module.  We will try restrict the library search to the
       system32 directory if supported by the OS. Older OSes doesn't support
       this, so we fall back on full search in that case. */
    HMODULE hMod = LoadLibraryExW(wszPath, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hMod == NULL && GetLastError() == ERROR_INVALID_PARAMETER)
        hMod = LoadLibraryExW(wszPath, NULL, 0);
    if (!hMod)
        ErrorMsgLastErrSWS("LoadAppDll: LoadLibraryExW failed on '", wszPath, "'");
    return hMod;
}

/**
 * Logging callback for the Windows driver (un)installation code.
 */
static DECLCALLBACK(void) vboxWinDrvInstLogCallback(VBOXWINDRIVERLOGTYPE enmType, const char *pszMsg, void *pvUser)
{
    HANDLE const hLog = (HANDLE)pvUser;

    /*
     * Log to standard output:
     */
    switch (enmType)
    {
        case VBOXWINDRIVERLOGTYPE_ERROR:
            PrintSSS("*** Error: ", pszMsg, "\r\n");
            break;

        default:
            PrintSS(pszMsg, "\r\n");
            break;
    }

    /*
     * Log to file (if any):
     */
    if (hLog != INVALID_HANDLE_VALUE)
    {
        char szBuf[1024];
        RTStrCopy(szBuf, sizeof(szBuf), enmType == VBOXWINDRIVERLOGTYPE_ERROR ? "*** Error: " : "");
        DWORD dwIgn;
        WriteFile(hLog, szBuf, (DWORD)strlen(szBuf), &dwIgn, NULL);
        WriteFile(hLog, pszMsg, (DWORD)strlen(pszMsg), &dwIgn, NULL);
        WriteFile(hLog, RT_STR_TUPLE("\r\n"), &dwIgn, NULL);
    }
}

/**
 * Writes the driver log file header.
 *
 * @returns VBox status code.
 * @param   hLog                Handle to log file.
 * @param   pwszInfFile         INF file this log belongs to.
 */
static void driverLogWriteHeader(HANDLE hLog, const wchar_t *pwszInfFile)
{
    /* Don't want to use RTStrPrintf here as it drags in a lot of code, thus this tedium... */
    char   szBuf[256];
    size_t offBuf = 2;
    RTStrCopy(szBuf, sizeof(szBuf), "\r\n");

    SYSTEMTIME SysTime = {0};
    GetSystemTime(&SysTime);

    RTStrFormatU32(&szBuf[offBuf], sizeof(szBuf) - offBuf, SysTime.wYear, 10, 4, 0, RTSTR_F_ZEROPAD | RTSTR_F_WIDTH);
    offBuf += strlen(&szBuf[offBuf]);
    szBuf[offBuf++] = '-';

    RTStrFormatU32(&szBuf[offBuf], sizeof(szBuf) - offBuf, SysTime.wMonth, 10, 2, 0, RTSTR_F_ZEROPAD | RTSTR_F_WIDTH);
    offBuf += strlen(&szBuf[offBuf]);
    szBuf[offBuf++] = '-';

    RTStrFormatU32(&szBuf[offBuf], sizeof(szBuf) - offBuf, SysTime.wDay, 10, 2, 0, RTSTR_F_ZEROPAD | RTSTR_F_WIDTH);
    offBuf += strlen(&szBuf[offBuf]);
    szBuf[offBuf++] = 'T';

    RTStrFormatU32(&szBuf[offBuf], sizeof(szBuf) - offBuf, SysTime.wHour, 10, 2, 0, RTSTR_F_ZEROPAD | RTSTR_F_WIDTH);
    offBuf += strlen(&szBuf[offBuf]);
    szBuf[offBuf++] = ':';

    RTStrFormatU32(&szBuf[offBuf], sizeof(szBuf) - offBuf, SysTime.wMinute, 10, 2, 0, RTSTR_F_ZEROPAD | RTSTR_F_WIDTH);
    offBuf += strlen(&szBuf[offBuf]);
    szBuf[offBuf++] = ':';

    RTStrFormatU32(&szBuf[offBuf], sizeof(szBuf) - offBuf, SysTime.wSecond, 10, 2, 0, RTSTR_F_ZEROPAD | RTSTR_F_WIDTH);
    offBuf += strlen(&szBuf[offBuf]);
    szBuf[offBuf++] = '.';

    RTStrFormatU32(&szBuf[offBuf], sizeof(szBuf) - offBuf, SysTime.wMilliseconds, 10, 3, 0, RTSTR_F_ZEROPAD | RTSTR_F_WIDTH);
    offBuf += strlen(&szBuf[offBuf]);
    RTStrCat(&szBuf[offBuf], sizeof(szBuf) - offBuf, "Z: Opened log file ");

    DWORD dwIgn;
    WriteFile(hLog, szBuf, (DWORD)strlen(szBuf), &dwIgn, NULL);

    char *pszUtf8 = NULL;
    int vrc = RTUtf16ToUtf8(pwszInfFile, &pszUtf8);
    if (RT_SUCCESS(vrc))
    {
        WriteFile(hLog, pszUtf8, (DWORD)strlen(pszUtf8), &dwIgn, NULL);
        RTStrFree(pszUtf8);
        WriteFile(hLog, RT_STR_TUPLE("'\r\n"), &dwIgn, NULL);
    }
    else
        WriteFile(hLog, RT_STR_TUPLE("<RTUtf16ToUtf8 failed>'\r\n"), &dwIgn, NULL);
}

/**
 * Opens (creates / appends) the driver (un)installation log.
 *
 * @returns VBox status code.
 * @param   pwszLogFile         Path to log file to create / open. If set to NULL, no logging will be performed.
 * @param   phLog               Where to return the log handle on success.
 */
static int driverLogOpen(const wchar_t *pwszLogFile, PHANDLE phLog)
{
    int rc = VINF_SUCCESS;

    /* Failures here are non-fatal. */
    if (pwszLogFile)
    {
        HANDLE hLog = INVALID_HANDLE_VALUE;
        hLog = CreateFileW(pwszLogFile, FILE_GENERIC_WRITE & ~FILE_WRITE_DATA /* append mode */, FILE_SHARE_READ,
                           NULL /*pSecAttr*/, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL /*hTemplateFile*/);
        if (hLog != INVALID_HANDLE_VALUE)
        {
            driverLogWriteHeader(hLog, pwszLogFile);

            *phLog = hLog;
        }
        else
        {
            ErrorMsgLastErrSWS("Failed to open/create log file '", pwszLogFile, "'");
            rc = VERR_CANT_CREATE;
        }
    }

    return rc;
}

/**
 * Closes the driver (un)installation log.
 *
 * @returns VBox status code.
 * @param   hLog                Handle of log to close.
 */
static int driverLogClose(HANDLE hLog)
{
    if (hLog != INVALID_HANDLE_VALUE)
        CloseHandle(hLog);

    return VINF_SUCCESS;
}

/** Handles 'driver install'. */
static int handleDriverInstall(unsigned cArgs, wchar_t **papwszArgs)
{
    char *pszInfFile = NULL;
    int rc = RTUtf16ToUtf8(papwszArgs[0], &pszInfFile);
    if (RT_SUCCESS(rc))
    {
        char *pszPnpId = NULL; /* PnP ID can be optional. */
        if (cArgs >= 2)
            rc = RTUtf16ToUtf8(papwszArgs[1], &pszPnpId);
        if (RT_SUCCESS(rc))
        {
            HANDLE hLog = INVALID_HANDLE_VALUE;
            if (cArgs >= 3)
                /* ignore rc, non-fatal */ driverLogOpen(papwszArgs[2], &hLog);
#ifdef DEBUG
            PrintSSS("INF File: ", pszInfFile, "\r\n");
            PrintSSS("  PnP ID: ", pszPnpId ? pszPnpId : "<None>", "\r\n");
#endif
            VBOXWINDRVINST hWinDrvInst;
            rc = VBoxWinDrvInstCreateEx(&hWinDrvInst, 4 /* Verbosity */, &vboxWinDrvInstLogCallback, hLog /* pvUser */);
            if (RT_SUCCESS(rc))
            {
                rc = VBoxWinDrvInstInstall(hWinDrvInst, pszInfFile, pszPnpId,
                                           VBOX_WIN_DRIVERINSTALL_F_SILENT | VBOX_WIN_DRIVERINSTALL_F_FORCE);

                VBoxWinDrvInstDestroy(hWinDrvInst);
            }

            driverLogClose(hLog);
            hLog = INVALID_HANDLE_VALUE;

            RTStrFree(pszPnpId);
        }
        RTStrFree(pszInfFile);
    }

    return rc == VINF_REBOOT_NEEDED ? EXIT_REBOOT : RT_SUCCESS(rc) ? EXIT_OK : EXIT_FAIL;
}

/** Handles 'driver uninstall'. */
static int handleDriverUninstall(unsigned cArgs, wchar_t **papwszArgs)
{
    char *pszInfFile = NULL;
    int rc = RTUtf16ToUtf8(papwszArgs[0], &pszInfFile);
    if (RT_SUCCESS(rc))
    {
        char *pszModel = NULL; /* Model is optional. */
        if (cArgs >= 2)
            rc = RTUtf16ToUtf8(papwszArgs[1], &pszModel);
        char *pszPnpId = NULL; /* PnP ID is optional. */
        if (cArgs >= 3)
            rc = RTUtf16ToUtf8(papwszArgs[2], &pszPnpId);
        if (RT_SUCCESS(rc))
        {
            HANDLE hLog = INVALID_HANDLE_VALUE;
            if (cArgs >= 3)
                /* ignore rc, non-fatal */ driverLogOpen(papwszArgs[2], &hLog);

#ifdef DEBUG
            PrintSSS("INF File: ", pszInfFile, "\r\n");
            PrintSSS("   Model: ", pszModel ? pszModel : "<None>", "\r\n");
            PrintSSS("  PnP ID: ", pszPnpId ? pszPnpId : "<None>", "\r\n");
#endif
            VBOXWINDRVINST hWinDrvInst;
            rc = VBoxWinDrvInstCreateEx(&hWinDrvInst, 4 /* Verbosity */, &vboxWinDrvInstLogCallback, hLog /* pvUser */);
            if (RT_SUCCESS(rc))
            {
                rc = VBoxWinDrvInstUninstall(hWinDrvInst, pszInfFile, pszModel, pszPnpId,
                                             VBOX_WIN_DRIVERINSTALL_F_SILENT | VBOX_WIN_DRIVERINSTALL_F_FORCE);

                VBoxWinDrvInstDestroy(hWinDrvInst);
            }

            driverLogClose(hLog);
            hLog = INVALID_HANDLE_VALUE;

            RTStrFree(pszPnpId);
        }
        RTStrFree(pszInfFile);
    }

    return rc == VINF_REBOOT_NEEDED ? EXIT_REBOOT : RT_SUCCESS(rc) ? EXIT_OK : EXIT_FAIL;
}


/** Handles 'driver executeinf'. */
static int handleDriverExecuteInf(unsigned cArgs, wchar_t **papwszArgs)
{
    char *pszInfFile = NULL;
    int rc = RTUtf16ToUtf8(papwszArgs[0], &pszInfFile);
    if (RT_SUCCESS(rc))
    {
        char *pszSection = NULL; /* Section is optional. */
        if (cArgs >= 2)
            rc = RTUtf16ToUtf8(papwszArgs[1], &pszSection);
        if (RT_SUCCESS(rc))
        {
            HANDLE hLog = INVALID_HANDLE_VALUE;
            if (cArgs >= 3)
                /* ignore rc, non-fatal */ driverLogOpen(papwszArgs[2], &hLog);

#ifdef DEBUG
            PrintSSS("INF File: ", pszInfFile, "\r\n");
            PrintSSS(" Section: ", pszSection ? pszSection : "DefaultInstall", "\r\n");
#endif
            VBOXWINDRVINST hWinDrvInst;
            rc = VBoxWinDrvInstCreateEx(&hWinDrvInst, 4 /* Verbosity */, &vboxWinDrvInstLogCallback, hLog /* pvUser */);
            if (RT_SUCCESS(rc))
            {
                rc = VBoxWinDrvInstInstallExecuteInf(hWinDrvInst, pszInfFile, pszSection ? pszSection : "DefaultInstall",
                                                     VBOX_WIN_DRIVERINSTALL_F_SILENT | VBOX_WIN_DRIVERINSTALL_F_FORCE);
                VBoxWinDrvInstDestroy(hWinDrvInst);
            }

            driverLogClose(hLog);
            hLog = INVALID_HANDLE_VALUE;
        }
        RTStrFree(pszInfFile);
    }

    return rc == VINF_REBOOT_NEEDED ? EXIT_REBOOT : RT_SUCCESS(rc) ? EXIT_OK : EXIT_FAIL;
}


/**
 * Inner NT4 video driver installation function.
 *
 * This can normally return immediately on errors as the parent will do the
 * cleaning up.
 */
static int InstallNt4VideoDriverInner(WCHAR const * const pwszDriverDir, HDEVINFO hDevInfo, HINF *phInf)
{
    /*
     * Get the first found driver - our Inf file only contains one so this is ok.
     *
     * Note! We must use the V1 structure here as it is the only NT4 recognizes.
     *       There are four versioned structures:
     *          - SP_ALTPLATFORM_INFO
     *          - SP_DRVINFO_DATA_W
     *          - SP_BACKUP_QUEUE_PARAMS_W
     *          - SP_INF_SIGNER_INFO_W,
     *       but we only make use of SP_DRVINFO_DATA_W.
     */
    SetLastError(NO_ERROR);
    SP_DRVINFO_DATA_V1_W drvInfoData = { sizeof(drvInfoData) };
    if (!SetupDiEnumDriverInfoW(hDevInfo, NULL, SPDIT_CLASSDRIVER, 0, &drvInfoData))
        return ErrorMsgLastErr("SetupDiEnumDriverInfoW");

    /*
     * Get necessary driver details
     */
    union
    {
        SP_DRVINFO_DETAIL_DATA_W s;
        uint64_t                 au64Padding[(sizeof(SP_DRVINFO_DETAIL_DATA_W) + 256) / sizeof(uint64_t)];
    } DriverInfoDetailData = { { sizeof(DriverInfoDetailData.s) } };
    DWORD                    cbReqSize            = NULL;
    if (   !SetupDiGetDriverInfoDetailW(hDevInfo, NULL, &drvInfoData,
                                        &DriverInfoDetailData.s, sizeof(DriverInfoDetailData), &cbReqSize)
        && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return ErrorMsgLastErr("SetupDiGetDriverInfoDetailW");

    HINF hInf = *phInf = SetupOpenInfFileW(DriverInfoDetailData.s.InfFileName, NULL, INF_STYLE_WIN4, NULL);
    if (hInf == INVALID_HANDLE_VALUE)
        return ErrorMsgLastErr("SetupOpenInfFileW");

    /*
     * First install the service.
     */
    WCHAR wszServiceSection[LINE_LEN];
    int rc = RTUtf16Copy(wszServiceSection, RT_ELEMENTS(wszServiceSection), DriverInfoDetailData.s.SectionName);
    if (RT_SUCCESS(rc))
        rc = RTUtf16CatAscii(wszServiceSection, RT_ELEMENTS(wszServiceSection), ".Services");
    if (RT_FAILURE(rc))
        return ErrorMsg("wszServiceSection too small");

    INFCONTEXT SvcCtx;
    if (!SetupFindFirstLineW(hInf, wszServiceSection, NULL, &SvcCtx))
        return ErrorMsgLastErr("SetupFindFirstLine"); /* impossible... */

    /*
     * Get the name
     */
    WCHAR wszServiceData[LINE_LEN] = {0};
    if (!SetupGetStringFieldW(&SvcCtx, 1, wszServiceData, RT_ELEMENTS(wszServiceData), NULL))
        return ErrorMsgLastErr("SetupGetStringFieldW");

    WCHAR wszDevInstanceId[LINE_LEN];
    rc = RTUtf16CopyAscii(wszDevInstanceId, RT_ELEMENTS(wszDevInstanceId), "Root\\LEGACY_");
    if (RT_SUCCESS(rc))
        rc = RTUtf16Cat(wszDevInstanceId, RT_ELEMENTS(wszDevInstanceId), wszServiceData);
    if (RT_SUCCESS(rc))
        rc = RTUtf16CatAscii(wszDevInstanceId, RT_ELEMENTS(wszDevInstanceId), "\\0000");
    if (RT_FAILURE(rc))
        return ErrorMsg("wszDevInstanceId too small");

    /*
     * ...
     */
    SP_DEVINFO_DATA deviceInfoData = { sizeof(deviceInfoData) };
    /* Check for existing first. */
    BOOL fDevInfoOkay = SetupDiOpenDeviceInfoW(hDevInfo, wszDevInstanceId, NULL, 0, &deviceInfoData);
    if (!fDevInfoOkay)
    {
        /* Okay, try create a new device info element. */
        if (SetupDiCreateDeviceInfoW(hDevInfo, wszDevInstanceId, (LPGUID)&GUID_DEVCLASS_DISPLAY,
                                     NULL, // Do we need a description here?
                                     NULL, // No user interface
                                     0, &deviceInfoData))
        {
            if (SetupDiRegisterDeviceInfo(hDevInfo, &deviceInfoData, 0, NULL, NULL, NULL))
                fDevInfoOkay = TRUE;
            else
                return ErrorMsgLastErr("SetupDiRegisterDeviceInfo"); /** @todo Original code didn't return here. */
        }
        else
            return ErrorMsgLastErr("SetupDiCreateDeviceInfoW"); /** @todo Original code didn't return here. */
    }
    if (fDevInfoOkay) /** @todo if not needed if it's okay to fail on failure above */
    {
        /* We created a new key in the registry */ /* bogus... */

        /*
         * Redo the install parameter thing with deviceInfoData.
         */
        SP_DEVINSTALL_PARAMS_W DeviceInstallParams = { sizeof(DeviceInstallParams) };
        if (!SetupDiGetDeviceInstallParamsW(hDevInfo, &deviceInfoData, &DeviceInstallParams))
            return ErrorMsgLastErr("SetupDiGetDeviceInstallParamsW(#2)"); /** @todo Original code didn't return here. */

        DeviceInstallParams.cbSize = sizeof(DeviceInstallParams);
        DeviceInstallParams.Flags |= DI_NOFILECOPY      /* We did our own file copying */
                                   | DI_DONOTCALLCONFIGMG
                                   | DI_ENUMSINGLEINF;  /* .DriverPath specifies an inf file */
        rc = RTUtf16Copy(DeviceInstallParams.DriverPath, RT_ELEMENTS(DeviceInstallParams.DriverPath), pwszDriverDir);
        if (RT_SUCCESS(rc))
            rc = RTUtf16CatAscii(DeviceInstallParams.DriverPath, RT_ELEMENTS(DeviceInstallParams.DriverPath),
                                 VBOXGUEST_NT4_VIDEO_INF_NAME);
        if (RT_FAILURE(rc))
            return ErrorMsg("Install dir too deep (long)");

        if (!SetupDiSetDeviceInstallParamsW(hDevInfo, &deviceInfoData, &DeviceInstallParams))
            return ErrorMsgLastErr("SetupDiSetDeviceInstallParamsW(#2)"); /** @todo Original code didn't return here. */

        if (!SetupDiBuildDriverInfoList(hDevInfo, &deviceInfoData, SPDIT_CLASSDRIVER))
            return ErrorMsgLastErr("SetupDiBuildDriverInfoList(#2)");

        /*
         * Repeat the query at the start of the function.
         */
        drvInfoData.cbSize = sizeof(drvInfoData);
        if (!SetupDiEnumDriverInfoW(hDevInfo, &deviceInfoData, SPDIT_CLASSDRIVER, 0, &drvInfoData))
            return ErrorMsgLastErr("SetupDiEnumDriverInfoW(#2)");

        /*
         * ...
         */
        if (!SetupDiSetSelectedDriverW(hDevInfo, &deviceInfoData, &drvInfoData))
            return ErrorMsgLastErr("SetupDiSetSelectedDriverW(#2)");

        if (!SetupDiInstallDevice(hDevInfo, &deviceInfoData))
            return ErrorMsgLastErr("SetupDiInstallDevice(#2)");
    }

    /*
     * Make sure the device is enabled.
     */
    DWORD fConfig = 0;
    if (SetupDiGetDeviceRegistryPropertyW(hDevInfo, &deviceInfoData, SPDRP_CONFIGFLAGS,
                                          NULL, (LPBYTE)&fConfig, sizeof(DWORD), NULL))
    {
        if (fConfig & CONFIGFLAG_DISABLED)
        {
            fConfig &= ~CONFIGFLAG_DISABLED;
            if (!SetupDiSetDeviceRegistryPropertyW(hDevInfo, &deviceInfoData, SPDRP_CONFIGFLAGS,
                                                   (LPBYTE)&fConfig, sizeof(fConfig)))
                ErrorMsg("SetupDiSetDeviceRegistryPropertyW");
        }
    }
    else
        ErrorMsg("SetupDiGetDeviceRegistryPropertyW");

    /*
     * Open the service key.
     */
    WCHAR wszSvcRegKey[LINE_LEN + 64];
    rc = RTUtf16CopyAscii(wszSvcRegKey, RT_ELEMENTS(wszSvcRegKey), "System\\CurrentControlSet\\Services\\");
    if (RT_SUCCESS(rc))
        rc = RTUtf16Cat(wszSvcRegKey, RT_ELEMENTS(wszSvcRegKey), wszServiceData);
    if (RT_SUCCESS(rc))
        rc = RTUtf16CatAscii(wszSvcRegKey, RT_ELEMENTS(wszSvcRegKey), "\\Device0"); /* We only have one device. */
    if (RT_FAILURE(rc))
        return ErrorMsg("Service key name too long");

    DWORD   dwIgn;
    HKEY    hKey = NULL;
    LSTATUS lrc  = RegCreateKeyExW(HKEY_LOCAL_MACHINE, wszSvcRegKey, 0, NULL, REG_OPTION_NON_VOLATILE,
                                   KEY_READ | KEY_WRITE, NULL, &hKey, &dwIgn);
    if (lrc == ERROR_SUCCESS)
    {
        /*
         * Insert service description.
         */
        lrc = RegSetValueExW(hKey, L"Device Description", 0, REG_SZ, (LPBYTE)DriverInfoDetailData.s.DrvDescription,
                            (DWORD)((RTUtf16Len(DriverInfoDetailData.s.DrvDescription) + 1) * sizeof(WCHAR)));
        if (lrc != ERROR_SUCCESS)
            ErrorMsgLStatus("RegSetValueExW", lrc);

        /*
         * Execute the SoftwareSettings section of the INF-file or something like that.
         */
        BOOL fOkay = FALSE;
        WCHAR wszSoftwareSection[LINE_LEN + 32];
        rc = RTUtf16Copy(wszSoftwareSection, RT_ELEMENTS(wszSoftwareSection), wszServiceData);
        if (RT_SUCCESS(rc))
            rc = RTUtf16CatAscii(wszSoftwareSection, RT_ELEMENTS(wszSoftwareSection), ".SoftwareSettings");
        if (RT_SUCCESS(rc))
        {
            if (SetupInstallFromInfSectionW(NULL, hInf, wszSoftwareSection, SPINST_REGISTRY, hKey,
                                            NULL, 0, NULL, NULL, NULL, NULL))
                fOkay = TRUE;
            else
                ErrorMsgLastErr("SetupInstallFromInfSectionW");
        }
        else
            ErrorMsg("Software settings section name too long");
        RegCloseKey(hKey);
        if (!fOkay)
            return EXIT_FAIL;
    }
    else
        ErrorMsgLStatus("RegCreateKeyExW/Service", lrc);

    /*
     * Install OpenGL stuff.
     */
    lrc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\OpenGLDrivers", 0, NULL,
                          REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, NULL, &hKey, &dwIgn);
    if (lrc == ERROR_SUCCESS)
    {
        /* Do installation here if ever necessary. Currently there is no OpenGL stuff */
        RegCloseKey(hKey);
    }
    else
        ErrorMsgLStatus("RegCreateKeyExW/OpenGLDrivers", lrc);

#if 0
    /* If this key is inserted into the registry, windows will show the desktop
       applet on next boot. We decide in the installer if we want that so the code
       is disabled here. */
    lrc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers\\NewDisplay",
                          0, NULL, REG_OPTION_NON_VOLATILE,
                          KEY_READ | KEY_WRITE, NULL, &hHey, &dwIgn)
    if (lrc == ERROR_SUCCESS)
        RegCloseKey(hHey);
    else
        ErrorMsgLStatus("RegCreateKeyExW/NewDisplay", lrc);
#endif

    /*
     * We must reboot at some point
     */
    lrc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers\\RebootNecessary", 0, NULL,
                          REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, NULL, &hKey, &dwIgn);
    if (lrc == ERROR_SUCCESS)
        RegCloseKey(hKey);
    else
        ErrorMsgLStatus("RegCreateKeyExW/RebootNecessary", lrc);

    return EXIT_OK;
}



/**
 * Install the VBox video driver.
 *
 * @param   pwszDriverDir     The base directory where we find the INF.
 */
static int InstallNt4VideoDriver(WCHAR const * const pwszDriverDir)
{
    /*
     * Create an empty list
     */
    HDEVINFO hDevInfo = SetupDiCreateDeviceInfoList((LPGUID)&GUID_DEVCLASS_DISPLAY, NULL);
    if (hDevInfo == INVALID_HANDLE_VALUE)
        return ErrorMsgLastErr("SetupDiCreateDeviceInfoList");

    /*
     * Get the default install parameters.
     */
    int rcExit = EXIT_FAIL;
    SP_DEVINSTALL_PARAMS_W DeviceInstallParams = { sizeof(DeviceInstallParams) };
    if (SetupDiGetDeviceInstallParamsW(hDevInfo, NULL, &DeviceInstallParams))
    {
        /*
         * Insert our install parameters and update hDevInfo with them.
         */
        DeviceInstallParams.cbSize = sizeof(DeviceInstallParams);
        DeviceInstallParams.Flags |= DI_NOFILECOPY /* We did our own file copying */
                                   | DI_DONOTCALLCONFIGMG
                                   | DI_ENUMSINGLEINF; /* .DriverPath specifies an inf file */
        int rc = RTUtf16Copy(DeviceInstallParams.DriverPath, RT_ELEMENTS(DeviceInstallParams.DriverPath), pwszDriverDir);
        if (RT_SUCCESS(rc))
            rc = RTUtf16CatAscii(DeviceInstallParams.DriverPath, RT_ELEMENTS(DeviceInstallParams.DriverPath),
                                 VBOXGUEST_NT4_VIDEO_INF_NAME);
        if (RT_SUCCESS(rc))
        {
            if (SetupDiSetDeviceInstallParamsW(hDevInfo, NULL, &DeviceInstallParams))
            {
                /*
                 * Read the drivers from the INF-file.
                 */
                if (SetupDiBuildDriverInfoList(hDevInfo, NULL, SPDIT_CLASSDRIVER))
                {
                    HINF hInf = NULL;
                    rcExit = InstallNt4VideoDriverInner(pwszDriverDir, hDevInfo, &hInf);

                    if (hInf)
                        SetupCloseInfFile(hInf);
                    SetupDiDestroyDriverInfoList(hDevInfo, NULL, SPDIT_CLASSDRIVER);
                }
                else
                    ErrorMsgLastErr("SetupDiBuildDriverInfoList");
            }
            else
                ErrorMsgLastErr("SetupDiSetDeviceInstallParamsW");

        }
        else
            ErrorMsg("Install dir too deep (long)");
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }
    else
        ErrorMsgLastErr("SetupDiGetDeviceInstallParams"); /** @todo Original code didn't return here. */
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return rcExit;
}


/** Handles 'driver nt4-install-video'. */
static int handleDriverNt4InstallVideo(unsigned cArgs, wchar_t **papwszArgs)
{
    /* One optional parameter: installation directory containing INF file. */
    WCHAR wszInstallDir[MAX_PATH];
    DWORD cwcInstallDir;
    if (cArgs < 1)
    {
        cwcInstallDir = GetModuleFileNameW(GetModuleHandle(NULL), &wszInstallDir[0], RT_ELEMENTS(wszInstallDir));
        if (cwcInstallDir > 0)
        {
            while (cwcInstallDir > 0 && !RTPATH_IS_SEP(wszInstallDir[cwcInstallDir - 1]))
                cwcInstallDir--;
            if (!cwcInstallDir) /* paranoia^3 */
            {
                wszInstallDir[cwcInstallDir++] = '.';
                wszInstallDir[cwcInstallDir++] = '\\';
            }
            wszInstallDir[cwcInstallDir] = '\0';
        }
    }
    else
    {
        WCHAR *pwszFilenameIgn;
        cwcInstallDir = GetFullPathNameW(papwszArgs[0], RT_ELEMENTS(wszInstallDir) - 1, wszInstallDir, &pwszFilenameIgn);
        if (cwcInstallDir == 0 || cwcInstallDir > RT_ELEMENTS(wszInstallDir) - 2)
            return ErrorMsgLastErrSWS("GetFullPathNameW failed for '", papwszArgs[0], "'!");
        if (!RTPATH_IS_SEP(wszInstallDir[cwcInstallDir - 1]))
        {
            wszInstallDir[cwcInstallDir++] = '\\';
            wszInstallDir[cwcInstallDir] = '\0';
        }
    }

    /* Make sure we're on NT4 before continuing: */
    OSVERSIONINFOW VerInfo = { sizeof(VerInfo) };
    GetVersionExW(&VerInfo);
    if (   VerInfo.dwPlatformId != VER_PLATFORM_WIN32_NT
        || VerInfo.dwMajorVersion != 4)
        return ErrorMsgSUSUS("This command is only for NT 4. GetVersionExW reports ", VerInfo.dwMajorVersion, ".",
                             VerInfo.dwMinorVersion, ".");

    return (int)InstallNt4VideoDriver(wszInstallDir);
}



/*********************************************************************************************************************************
*   'service'                                                                                                                    *
*********************************************************************************************************************************/

/**
 * Worker for the 'service create' handler.
 */
static int CreateService(const wchar_t *pwszService,
                         const wchar_t *pwszDisplayName,
                         uint32_t       uServiceType,
                         uint32_t       uStartType,
                         const wchar_t *pwszBinPath,
                         const wchar_t *pwszLoadOrderGroup,
                         const wchar_t *pwszDependencies,
                         const wchar_t *pwszLogonUser,
                         const wchar_t *pwszLogonPassword)
{
    PrintSWSWS("Installing service '", pwszService, "' ('", pwszDisplayName, ") ...\r\n");

    /*
     * Transform the dependency list to a REG_MULTI_SZ.
     */
    if (pwszDependencies != NULL)
    {
        /* Copy it into alloca() buffer so we can modify it. */
        size_t cwc = RTUtf16Len(pwszDependencies);
        wchar_t *pwszDup = (wchar_t *)alloca((cwc + 2) * sizeof(wchar_t));
        memcpy(pwszDup, pwszDependencies, cwc * sizeof(wchar_t));
        pwszDup[cwc]     = L'\0';
        pwszDup[cwc + 1] = L'\0';   /* double termination */

        /* Perform: s/,/\0/g */
        while (cwc-- > 0 )
            if (pwszDup[cwc] == L',')
                pwszDup[cwc] = L'\0';

        pwszDependencies = pwszDup;
    }

    SC_HANDLE hSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (hSCManager == NULL)
        return ErrorMsgLastErr("OpenSCManagerW failed");

    int rcExit = EXIT_FAIL;
    DWORD dwTag = 0xDEADBEAF;
    SC_HANDLE hService = CreateServiceW(hSCManager, pwszService, pwszDisplayName, SERVICE_ALL_ACCESS, uServiceType, uStartType,
                                        SERVICE_ERROR_NORMAL, pwszBinPath, pwszLoadOrderGroup, pwszLoadOrderGroup ? &dwTag : NULL,
                                        pwszDependencies, pwszLogonUser, pwszLogonPassword);
    if (hService != NULL)
    {
        CloseServiceHandle(hService);
        PrintStr("Installation of service successful!\r\n");
        rcExit = EXIT_OK;
    }
    else
    {
        DWORD dwErr = GetLastError();
        if (dwErr == ERROR_SERVICE_EXISTS)
        {
            PrintStr("Service already exists. Updating the service config ...\r\n");
            hService = OpenServiceW(hSCManager, pwszService, SERVICE_ALL_ACCESS);
            if (hService != NULL)
            {
                if (ChangeServiceConfigW(hService, uServiceType, uStartType, SERVICE_ERROR_NORMAL, pwszBinPath,
                                         pwszLoadOrderGroup, pwszLoadOrderGroup ? &dwTag : NULL, pwszDependencies,
                                         pwszLogonUser, pwszLogonPassword, pwszDisplayName))
                {
                    PrintStr("The service config has been successfully updated.\r\n");
                    rcExit = EXIT_OK;
                }
                else
                    rcExit = ErrorMsgLastErrSWS("ChangeServiceConfigW failed on '", pwszService, "'!");
                CloseServiceHandle(hService);
            }
            else
                rcExit = ErrorMsgLastErrSWS("OpenSCManagerW failed on '", pwszService, "'!");

            /*
             * This branch does not return an error to avoid installations failures,
             * if updating service parameters. Better to have a running system with old
             * parameters and the failure information in the installation log.
             */
            rcExit = EXIT_OK;
        }
        else
            rcExit = ErrorMsgLastErrSWS("CreateServiceW for '", pwszService, "'!");
    }

    CloseServiceHandle(hSCManager);
    return rcExit;
}


/** Handles 'service create'. */
static int handleServiceCreate(unsigned cArgs, wchar_t **papwszArgs)
{
    uint32_t uServiceType;
    if (!ArgToUInt32Full(papwszArgs[2], "service-type", &uServiceType))
        return EXIT_USAGE;

    uint32_t uStartType;
    if (!ArgToUInt32Full(papwszArgs[3], "start-type", &uStartType))
        return EXIT_USAGE;

    return CreateService(papwszArgs[0], papwszArgs[1], uServiceType, uStartType, papwszArgs[4],
                         cArgs > 5 ? papwszArgs[5] : NULL,
                         cArgs > 6 ? papwszArgs[6] : NULL,
                         cArgs > 7 ? papwszArgs[7] : NULL,
                         cArgs > 8 ? papwszArgs[8] : NULL);
}


/**
 * Worker for the 'service delete' handler.
 */
static int DelService(const wchar_t *pwszService)
{
    PrintSWS("Removing service '", pwszService, "' ...\r\n");

    SC_HANDLE hSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (hSCManager == NULL)
        return ErrorMsgLastErr("OpenSCManagerW failed");

    int rcExit = EXIT_FAIL;
    SC_HANDLE hService = NULL;
    hService = OpenServiceW(hSCManager, pwszService, SERVICE_ALL_ACCESS);
    if (hService)
    {
        SC_LOCK hSCLock = LockServiceDatabase(hSCManager);
        if (hSCLock != NULL)
        {
            if (DeleteService(hService))
            {
                PrintSWS("Service '", pwszService, "' successfully deleted.\r\n");
                rcExit = EXIT_OK;
            }
            else
            {
                DWORD dwErr = GetLastError();
                if (dwErr == ERROR_SERVICE_MARKED_FOR_DELETE)
                {
                    PrintSWS("Service '", pwszService, "' already marked for deletion.\r\n");
                    rcExit = EXIT_OK;
                }
                else
                    rcExit = ErrorMsgLastErrSWS("Failed to delete service'", pwszService, "'!");
            }
            UnlockServiceDatabase(hSCLock);
        }
        else
            ErrorMsgLastErr("LockServiceDatabase failed");
        CloseServiceHandle(hService);
    }
    else
        rcExit = ErrorMsgLastErrSWS("Failed to open service'", pwszService, "'!");
    CloseServiceHandle(hSCManager);
    return rcExit;
}


/** Handles 'service delete' */
static int handleServiceDelete(unsigned cArgs, wchar_t **papwszArgs)
{
    RT_NOREF(cArgs);
    return DelService(papwszArgs[0]);
}




/*********************************************************************************************************************************
*   'registry'                                                                                                                   *
*********************************************************************************************************************************/

/**
 * Translate a registry root specifier into a HKEY_XXX constant.
 */
static HKEY ArgToRegistryRoot(const wchar_t *pwszRoot)
{
    HKEY hRootKey = NULL;
    if (RTUtf16ICmpAscii(pwszRoot, "hklm") == 0)
        hRootKey = HKEY_LOCAL_MACHINE;
    else if (RTUtf16ICmpAscii(pwszRoot, "hkcu") == 0)
        hRootKey = HKEY_CURRENT_USER;
    else if (RTUtf16ICmpAscii(pwszRoot, "hkcr") == 0)
        hRootKey = HKEY_CLASSES_ROOT;
    else if (RTUtf16ICmpAscii(pwszRoot, "hku") == 0)
        hRootKey = HKEY_USERS;
    else if (RTUtf16ICmpAscii(pwszRoot, "hkcc") == 0)
        hRootKey = HKEY_CURRENT_CONFIG;
    else
        ErrorBadArg("root", pwszRoot, "hklm, hkcu, hkcr, hku or hkcc");
    return hRootKey;
}


/**
 * Reverse of ArgToRegistryRoot.
 */
static wchar_t const *RegistryRootToWStr(HKEY hRootKey)
{
    if (hRootKey == HKEY_LOCAL_MACHINE)
        return L"HKLM";
    if (hRootKey == HKEY_CURRENT_USER)
        return L"HKCU";
    if (hRootKey == HKEY_CLASSES_ROOT)
        return L"HKCR";
    if (hRootKey == HKEY_USERS)
        return L"HKU";
    if (hRootKey == HKEY_CURRENT_CONFIG)
        return L"HKCC";
    return L"<bad-hkey-root>";
}


/**
 * Checks if a string is a substring of another one.
 *
 * Used by the RegistryAddStringToMultiSZ & RegistryRemoveStringToMultiSZ
 * routines.
 */
static bool IsSubStringOf(wchar_t volatile const *pwszStr, size_t cwcStr, wchar_t const *pwszSubStr, size_t cwcSubStr)
{
    if (cwcStr >= cwcSubStr && cwcSubStr > 0)
    {
        wchar_t const wcFirst = *pwszSubStr;
        cwcStr -= cwcSubStr;
        do
        {
            /* Could've used wmemchr here, but it isn't implemented in noCRT yet. */
            if (   *pwszStr == wcFirst
                && memcmp((void const *)pwszStr, pwszSubStr, cwcSubStr * sizeof(wchar_t)) == 0)
                return true;
            pwszStr++;
        } while (cwcStr-- > 0);
    }
    return false;
}


/**
 * Adds a string entry to a MULTI_SZ registry list.
 *
 * @return  Exit code (EXIT_OK, EXIT_FAIL)
 * @param   pwszSubKey      Sub key containing the list.
 * @param   pwszValueName   The actual key name of the list.
 * @param   pwszItemToAdd   The item to add to the list.
 * @param   uPosition       Position (zero-based) of where to add the
 *                          value to the list.
 */
static int RegistryAddStringToMultiSZ(const wchar_t *pwszSubKey, const wchar_t *pwszValueName,
                                      const wchar_t *pwszItemToAdd, uint32_t uPosition)
{
    size_t const cwcItemToAdd = RTUtf16Len(pwszItemToAdd);
    size_t const cbItemToAdd  = (cwcItemToAdd + 1) * sizeof(wchar_t);
#ifdef DEBUG
    PrintSWSWSWSXS("AddStringToMultiSZ: Adding MULTI_SZ item '", pwszItemToAdd,
                   "' to HKLM/'", pwszSubKey, "'/'", pwszValueName, "' at position ", uPosition, "\r\n");
#endif

    /*
     * Open/create the key.
     */
    HKEY    hKey   = NULL;
    DWORD   dwDisp = 0;
    LSTATUS lrc = RegCreateKeyEx(HKEY_LOCAL_MACHINE, pwszSubKey, 0 /*Reserved*/, NULL /*pClass*/, REG_OPTION_NON_VOLATILE,
                                 KEY_READ | KEY_WRITE, NULL /*pSecAttr*/, &hKey, &dwDisp);
    if (lrc != ERROR_SUCCESS)
        return ErrorMsgLStatusSWSRS("RegistryAddStringToList: RegCreateKeyEx HKLM/'", pwszSubKey, "' failed: ", lrc, NULL);

    /*
     * Query the current value, first query just gets the buffer size the 2nd does the actual query.
     * We make sure the buffer is large enough to contain the new item we're supposed to add.
     */
    int   rcExit  = EXIT_FAIL;
    PBYTE pbBuf   = NULL;
    DWORD cbValue = 0;
    DWORD dwType  = 0;
    lrc = RegQueryValueEx(hKey, pwszValueName, NULL, &dwType, NULL, &cbValue);
    if (lrc == ERROR_SUCCESS || lrc == ERROR_MORE_DATA)
    {
        cbValue = cbValue + _1K - sizeof(wchar_t)*2; /* 1KB of paranoia fudge, even if we ASSUME no races. */
        pbBuf = (PBYTE)RTMemAllocZ(cbValue + sizeof(wchar_t)*2 /* Two extra wchar_t's for proper zero termination. */
                                   + cbItemToAdd);
        if (!pbBuf)
            lrc = ERROR_OUTOFMEMORY;
        lrc = RegQueryValueEx(hKey, pwszValueName, NULL, &dwType, pbBuf, &cbValue);
    }
    if (lrc == ERROR_FILE_NOT_FOUND)
    {
        PrintStr("RegistryAddStringToList: Value not found, creating a new one...\r\n");
        pbBuf = (PBYTE)RTMemAllocZ(cbItemToAdd + sizeof(wchar_t)*8);
        if (pbBuf)
        {
            cbValue = sizeof(wchar_t);
            dwType  = REG_MULTI_SZ;
            lrc     = ERROR_SUCCESS;
        }
        else
            lrc     = ERROR_OUTOFMEMORY;
    }
    if (   lrc == ERROR_SUCCESS
        && dwType == REG_MULTI_SZ)
    {
#ifdef DEBUG
        PrintSXS("RegistryAddStringToList: Current value length: ", cbValue, "\r\n");
#endif

        /*
         * Scan the strings in the buffer, inserting the new item and removing any
         * existing duplicates.  We do this in place.
         *
         * We have made sure above that the buffer is both properly zero terminated
         * and large enough to contain the new item, so we need do no buffer size
         * checking here.
         */
        wchar_t volatile *pwszSrc   = (wchar_t volatile *)pbBuf;
        wchar_t volatile *pwszDst   = (wchar_t volatile *)pbBuf;
        size_t            cbLeft    = cbValue;
        for (uint32_t uCurPos = 0; ; uCurPos++)
        {
            size_t const cwcSrc  = RTUtf16Len((wchar_t const *)pwszSrc);
            size_t const cbSrc   = (cwcSrc + 1) * sizeof(wchar_t);
            bool const   fTheEnd = !cwcSrc && cbSrc >= cbLeft;

            /* Insert the item if we're in the right position now, or if we're
               at the last string and still haven't reached it. */
            if (uCurPos == uPosition || (fTheEnd && uCurPos < uPosition))
            {
                pwszSrc = (wchar_t volatile *)memmove((PBYTE)pwszSrc + cbItemToAdd, (wchar_t const *)pwszSrc, cbLeft);
                memcpy((void *)pwszDst, pwszItemToAdd, cbItemToAdd);
                pwszDst += cwcItemToAdd + 1;
                uCurPos++;
            }
            if (fTheEnd)
                break;

            /* We do not add empty strings nor strings matching the one we're adding. */
            if (!cwcSrc || IsSubStringOf(pwszSrc, cwcSrc, pwszItemToAdd, cwcItemToAdd))
                uCurPos--;
            else
            {
                if (pwszDst != pwszSrc)
                    memmove((void *)pwszDst, (void const *)pwszSrc, cbSrc);
                pwszDst += cwcSrc + 1;
            }
            pwszSrc += cwcSrc + 1;
            cbLeft  -= cbSrc;
        }
        *pwszDst = '\0';
        DWORD const cbNewValue = (DWORD)((PBYTE)(pwszDst + 1) - pbBuf);
#ifdef DEBUG
        PrintSXS("RegistryAddStringToList: New value length: ", cbNewValue, "\r\n");
#endif

        /*
         * Always write the value since we cannot tell whether it changed or
         * not without adding a bunch extra code above.
         */
        lrc = RegSetValueExW(hKey, pwszValueName, 0, REG_MULTI_SZ, pbBuf, cbNewValue);
        if (lrc == ERROR_SUCCESS)
        {
#ifdef DEBUG
            PrintSWSWS("RegistryAddStringToList: The item '", pwszItemToAdd, "' was added successfully to '",
                       pwszValueName, "'.\r\n");
#endif
            rcExit = EXIT_OK;
        }
        else
            ErrorMsgLStatusSWSWSRS("RegistryAddStringToList: RegSetValueExW HKLM/'",
                                   pwszSubKey, "'/'", pwszValueName, "' failed: ", lrc, NULL);
    }
    else if (lrc != ERROR_SUCCESS)
        ErrorMsgLStatusSWSWSRS("RemoveStringFromMultiSZ: RegQueryValueEx HKLM/'",
                               pwszSubKey, "'/'", pwszValueName, "' failed: ", lrc, NULL);
    else
        ErrorMsgLStatusSWSWSRS("RemoveStringFromMultiSZ: Unexpected value type for HKLM/'",
                               pwszSubKey, "'/'", pwszValueName, "': ", (LSTATUS)dwType, ", expected REG_SZ (1)");
    return rcExit;
}


/** Handles 'registry addmultisz'. */
static int handleRegistryAddMultiSz(unsigned cArgs, wchar_t **papwszArgs)
{
    RT_NOREF(cArgs);

    uint32_t uPosition;
    if (!ArgToUInt32Full(papwszArgs[3], "position", &uPosition))
        return EXIT_USAGE;

    return RegistryAddStringToMultiSZ(papwszArgs[0], papwszArgs[1], papwszArgs[2], uPosition);
}


/**
 * Removes a item from a MULTI_SZ registry list.
 *
 * @return  Exit code (EXIT_OK, EXIT_FAIL)
 * @param   pwszSubKey          Sub key containing the list.
 * @param   pwszValueName       The actual key name of the list.
 * @param   pwszItemToRemove    The item to remove from the list.  Actually, we
 *                              only do a substring match on this, so any item
 *                              containing this string will be removed.
 */
static int RegistryRemoveStringFromMultiSZ(const wchar_t *pwszSubKey, const wchar_t *pwszValueName,
                                           const wchar_t *pwszItemToRemove)
{
#ifdef DEBUG
    PrintSWSWSWS("RemoveStringFromMultiSZ: Removing MULTI_SZ string '", pwszItemToRemove,
                 "' from HKLM/'", pwszSubKey, "'/'", pwszValueName, "'\r\n");
#endif

    /*
     * Open the specified key.
     */
    HKEY hKey = NULL;
    LSTATUS lrc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, pwszSubKey, 0 /*dwOptions*/, KEY_READ | KEY_WRITE, &hKey);
    if (lrc != ERROR_SUCCESS)
        return ErrorMsgLStatusSWSRS("RemoveStringFromMultiSZ: RegOpenKeyExW HKLM/'", pwszSubKey, "' failed: ", lrc, NULL);

    /*
     * Query the current value, first query just gets the buffer size the 2nd does the actual query.
     */
    int   rcExit  = EXIT_FAIL;
    PBYTE pbBuf   = NULL;
    DWORD cbValue = 0;
    DWORD dwType  = 0;
    lrc = RegQueryValueEx(hKey, pwszValueName, NULL, &dwType, NULL, &cbValue);
    if (lrc == ERROR_SUCCESS || lrc  == ERROR_MORE_DATA)
    {
        cbValue = cbValue + _1K - sizeof(wchar_t)*2; /* 1KB of paranoia fudge, even if we ASSUME no races. */
        pbBuf = (PBYTE)RTMemAllocZ(cbValue + sizeof(wchar_t)*2); /* Two extra wchar_t's for proper zero termination, see docs. */
        if (!pbBuf)
            lrc = ERROR_OUTOFMEMORY;
        lrc = RegQueryValueEx(hKey, pwszValueName, NULL, &dwType, pbBuf, &cbValue);
    }
    if (   lrc == ERROR_SUCCESS
        && dwType == REG_MULTI_SZ)
    {
#ifdef DEBUG
        PrintSXS("RemoveStringFromMultiSZ: Current value length: ", cbValue, "\r\n");
#endif
        /*
         * Scan the buffer and remove all strings containing the pwszItemToRemove
         * as a substring.
         */
        size_t const      cwcValueToRemove = RTUtf16Len(pwszItemToRemove);
        wchar_t volatile *pwszSrc          = (wchar_t volatile *)pbBuf;
        wchar_t volatile *pwszDst          = (wchar_t volatile *)pbBuf;
        size_t            cbLeft           = cbValue;
        for (;;)
        {
            /* Find the length for the current string.  We can safely use RTUtf16Len
               here because of a zero terminated buffer with two extra terminator chars. */
            size_t const cwcSrc = RTUtf16Len((wchar_t const *)pwszSrc);
            size_t const cbSrc  = (cwcSrc + 1) * sizeof(wchar_t);
            if (!IsSubStringOf(pwszSrc, cwcSrc, pwszItemToRemove, cwcValueToRemove))
            {
                if (pwszDst != pwszSrc)
                    memmove((void *)pwszDst, (void const *)pwszSrc, cbSrc);
                pwszDst += cwcSrc + 1;
            }

            /* Advance. */
            if (cbLeft < cbSrc)
                break;
            cbLeft  -= cbSrc;
            pwszSrc += cwcSrc + 1;
        }
        *pwszDst = '\0';
        DWORD const cbNewValue = (DWORD)((PBYTE)(pwszDst + 1) - pbBuf);
#ifdef DEBUG
        PrintSXS("RemoveStringFromMultiSZ: New value length: ", cbNewValue, "\r\n");
#endif

        /*
         * Update the value if we made any change.
         */
        if (cbNewValue == cbValue)
        {
#ifdef DEBUG
            PrintSWSWS("RemoveStringFromMultiSZ: The item '", pwszItemToRemove, "' was not part of '",
                       pwszValueName, "', so nothing needed doing.\r\n");
#endif
            rcExit = EXIT_OK;
        }
        else
        {
            lrc = RegSetValueExW(hKey, pwszValueName, 0, REG_MULTI_SZ, pbBuf, cbNewValue);
            if (lrc == ERROR_SUCCESS)
            {
#ifdef DEBUG
                PrintSWSWS("RemoveStringFromMultiSZ: The item '", pwszItemToRemove, "' was removed successfully from '",
                           pwszValueName, "'.\r\n");
#endif
                rcExit = EXIT_OK;
            }
            else
                ErrorMsgLStatusSWSWSRS("RegistryAddStringToList: RegSetValueExW HKLM/'",
                                       pwszSubKey, "'/'", pwszValueName, "' failed: ", lrc, NULL);
        }
    }
    else if (lrc == ERROR_FILE_NOT_FOUND)
    {
#ifdef DEBUG
        PrintStr("RemoveStringFromMultiSZ: value not present in registry\r\n");
#endif
        rcExit = EXIT_OK;
    }
    else if (lrc != ERROR_SUCCESS)
        ErrorMsgLStatusSWSWSRS("RemoveStringFromMultiSZ: RegQueryValueEx HKLM/'",
                               pwszSubKey, "'/'", pwszValueName, "' failed: ", lrc, NULL);
    else
        ErrorMsgLStatusSWSWSRS("RemoveStringFromMultiSZ: Unexpected value type for HKLM/'",
                               pwszSubKey, "'/'", pwszValueName, "': ", (LSTATUS)dwType, ", expected REG_SZ (1)");
    RegCloseKey(hKey);
    RTMemFree(pbBuf);
    return rcExit;
}


/** Handles 'registry delmultisz'. */
static int handleRegistryDelMultiSz(unsigned cArgs, wchar_t **papwszArgs)
{
    RT_NOREF(cArgs);
    return RegistryRemoveStringFromMultiSZ(papwszArgs[0], papwszArgs[1], papwszArgs[2]);
}


/**
 * Compare the current list item with the one to add/remove.
 *
 * Used by RegistryAddStringToList and RegistryRemoveStringFromList.
 */
static bool IsStringListItemMatch(wchar_t volatile *pwszItem1, size_t cwcItem1,
                                  wchar_t const *pwszItem2, size_t cwcItem2)
{
    if (cwcItem1 == cwcItem2)
    {
#if 0 /* 94720 bytes */
        if (RTUtf16NICmp((wchar_t const *)pwszItem1, pwszItem2, cwcItem1) == 0)
            return true;
#else /* vs 62464 bytes */
        /* Temporarily zero termination of item 1 as it's easier, and therefore
           safer, to use lstrcmpiW than CompareStringW or CompareStringExW.  The
           latter is Vista and later, the former has a big fat warning on it.  */
        wchar_t const wcEnd = pwszItem1[cwcItem1];
        pwszItem1[cwcItem1] = '\0';
        int const iDiff = lstrcmpiW((wchar_t const *)pwszItem1, pwszItem2);
        pwszItem1[cwcItem1] = wcEnd;
        return iDiff == 0;
#endif
    }
    return false;
}


/**
 * Adds an item to a comma separated registry string list (REG_SZ).
 *
 * Only operates in HKLM for now, if needed it can be extended later for use
 * with other hives.
 *
 * @return  Exit code (EXIT_OK, EXIT_FAIL)
 * @param   hRootKey            The root key.
 * @param   pwszSubKey          Sub key containing the list value.
 * @param   pwszValueName       The name of the value holding the list.
 * @param   pwszItemToAdd       The value to add to the list.
 * @param   uPosition           Position (zero-based) of where to insert the
 *                              value into the list.
 * @param   fFlags              VBOX_REG_STRINGLIST_ALLOW_DUPLICATES or 0.
 */
static int RegistryAddStringToList(HKEY hRootKey, const wchar_t *pwszSubKey, const wchar_t *pwszValueName,
                                   const wchar_t *pwszItemToAdd, uint32_t uPosition, uint32_t fFlags)
{
    /* Overflow precaution - see comment below. */
    size_t const cwcItemToAdd = RTUtf16Len(pwszItemToAdd);
    if (cwcItemToAdd >= 256 /* see wszNewValue size below */)
        return ErrorMsg("RegistryAddStringToList: The value to add is too long! Max 256 chars.");

    /*
     * Open/create the key.
     */
    HKEY    hKey   = NULL;
    DWORD   dwDisp = 0;
    LSTATUS lrc = RegCreateKeyEx(hRootKey, pwszSubKey, 0 /*Reserved*/, NULL /*pClass*/, REG_OPTION_NON_VOLATILE,
                                 KEY_READ | KEY_WRITE, NULL /*pSecAttr*/, &hKey, &dwDisp);
    if (lrc != ERROR_SUCCESS)
        return ErrorMsgLStatusSWSWSRS("RegistryAddStringToList: RegCreateKeyEx ", RegistryRootToWStr(hRootKey), "/'", pwszSubKey,
                                      "' failed: ", lrc, NULL);

    /*
     * Query the current value.
     */
    int     rcExit         = EXIT_FAIL;
    wchar_t wszValue[1024] = { 0 };
    DWORD   cbValue        = sizeof(wszValue) - sizeof(wchar_t);
    DWORD   dwType         = 0;
    lrc = RegQueryValueEx(hKey, pwszValueName, NULL /*pReserved*/, &dwType, (LPBYTE)wszValue, &cbValue);
    if (lrc == ERROR_FILE_NOT_FOUND)
    {
        PrintStr("RegistryAddStringToList: Value not found, creating a new one...\r\n");
        wszValue[0] = '\0';
        cbValue     = sizeof(wchar_t);
        dwType      = REG_SZ;
        lrc = ERROR_SUCCESS;
    }
    if (lrc == ERROR_SUCCESS && dwType == REG_SZ)
    {
#ifdef DEBUG
        PrintSWS("RegistryAddStringToList: Value string: '", wszValue, "'\r\n");
#endif

        /*
         * Scan the list and make a new copy of it with the new item added
         * in the specified place.
         *
         * Having checked that what we're adding isn't more than 256 + 1 chars long
         * above, we can avoid tedious overflow checking here the simple expedient of
         * using an output buffer that's at least 256 + 1 chars bigger than the source.
         */
        wchar_t  wszNewValue[RT_ELEMENTS(wszValue) + 256 + 4] = { 0 };
        wchar_t *pwszDst = wszNewValue;
        wchar_t *pwszSrc = wszValue;
        for (unsigned uCurPos = 0;; uCurPos++)
        {
            /* Skip leading commas: */
            wchar_t wc            = *pwszSrc;
            bool    fLeadingComma = wc == ',';
            if (fLeadingComma)
                do
                    wc = *++pwszSrc;
                while (wc == ',');

            /* Insert the new item if we're at the right position or have reached
               the end of the list and have yet done so. */
            if (uCurPos == uPosition || (!wc && uCurPos < uPosition))
            {
                if (fLeadingComma || (wc == '\0' && pwszDst != wszNewValue))
                    *pwszDst++ = ',';
                memcpy(pwszDst, pwszItemToAdd, cwcItemToAdd * sizeof(wchar_t));
                pwszDst += cwcItemToAdd;
                fLeadingComma = true;
            }

            /* Get out of the loop if we're at the end of the input. */
            if (!wc)
                break; /* don't preserve trailing commas? Old code didn't (see strtok_r code). */

            /* Start of a new 'value', so, find the end of it. */
            wchar_t *pwszSrcEnd = pwszSrc + 1;
            do
                wc = *++pwszSrcEnd;
            while (wc != '\0' && wc != ',');
            size_t const cwcItem = (size_t)(pwszSrcEnd - pwszSrc);

            /* If it matches pwszItemToRemove and the VBOX_REG_STRINGLIST_ALLOW_DUPLICATES
               wasn't specified, we'll skip this value. */
            ASMCompilerBarrier(); /* Paranoia ^ 2*/
            if (   !(fFlags & VBOX_REG_STRINGLIST_ALLOW_DUPLICATES)
                && IsStringListItemMatch(pwszSrc, cwcItem, pwszItemToAdd, cwcItemToAdd))
            {
                pwszSrc = pwszSrcEnd;
                if (!fLeadingComma)
                    while (*pwszSrc == ',')
                        pwszSrc++;
                uCurPos--;
            }
            else
            {
                if (fLeadingComma)
                    *pwszDst++ = ',';
                memmove(pwszDst, pwszSrc, cwcItem * sizeof(*pwszDst));
                pwszDst += cwcItem;
                pwszSrc  = pwszSrcEnd;
                ASMCompilerBarrier(); /* Paranoia ^ 3 */
            }

            /* pwszSrc should not point at a comma or a zero terminator. */
        }
        *pwszDst = '\0';
        DWORD const cbNewValue = (DWORD)((pwszDst + 1 - &wszNewValue[0]) * sizeof(wchar_t));

#ifdef DEBUG
        PrintSWS("RegistryAddStringToList: New value:    '", wszNewValue, "'\r\n");
#endif

        /*
         * Add the value if changed.
         */
        if (   cbNewValue == cbValue
            && memcmp(wszNewValue, wszValue, cbNewValue) == 0)
            rcExit = EXIT_OK;
        else
        {
            lrc = RegSetValueExW(hKey, pwszValueName, 0, REG_SZ, (LPBYTE)wszNewValue, cbNewValue);
            if (lrc == ERROR_SUCCESS)
                rcExit = EXIT_OK;
            else
                ErrorMsgLStatusSWSWSWSRS("RegistryAddStringToList: RegSetValueExW HKLM/'",
                                         pwszSubKey, "'/'", pwszValueName, "' = '", wszNewValue, "' failed: ", lrc, NULL);
        }
    }
    else if (lrc != ERROR_SUCCESS)
        ErrorMsgLStatusSWSWSWSRS("RegistryAddStringToList: RegQueryValueEx ", RegistryRootToWStr(hRootKey), "/'",
                                 pwszSubKey, "'/'", pwszValueName, "' failed: ", lrc, NULL);
    else
        ErrorMsgLStatusSWSWSWSRS("RegistryAddStringToList: Unexpected value type for ", RegistryRootToWStr(hRootKey), "/'",
                                 pwszSubKey, "'/'", pwszValueName, "': ", (LSTATUS)dwType, ", expected REG_SZ (1)");

    RegCloseKey(hKey);
    return rcExit;
}


/**
 * Handles 'netprovider add'.
 */
static int handleNetProviderAdd(unsigned cArgs, wchar_t **papwszArgs)
{
    const wchar_t * const pwszProvider = papwszArgs[0];
    wchar_t const * const pwszPosition = cArgs > 1 ? papwszArgs[1] : L"0";
    uint32_t              uPosition    = 0;
    if (cArgs > 1 && !ArgToUInt32Full(pwszPosition, "position", &uPosition))
        return EXIT_USAGE;

    PrintSWSWS("Adding network provider '", pwszProvider, "' (Position = ", pwszPosition, ") ...\r\n");
    int rcExit = RegistryAddStringToList(HKEY_LOCAL_MACHINE,
                                         L"System\\CurrentControlSet\\Control\\NetworkProvider\\Order",
                                         L"ProviderOrder",
                                         pwszProvider, uPosition, VBOX_REG_STRINGLIST_NONE);
    if (rcExit == EXIT_OK)
        PrintStr("Network provider successfully added!\r\n");

    return rcExit;
}


/**
 * Handles 'registry addlistitem'.
 */
static int handleRegistryAddListItem(unsigned cArgs, wchar_t **papwszArgs)
{
    /*
     * Parameters.
     */
    wchar_t const * const pwszRoot      = papwszArgs[0];
    wchar_t const * const pwszSubKey    = papwszArgs[1];
    wchar_t const * const pwszValueName = papwszArgs[2];
    wchar_t const * const pwszItem      = papwszArgs[3];
    wchar_t const * const pwszPosition  = cArgs > 4 ? papwszArgs[4] : L"0";
    wchar_t const * const pwszFlags     = cArgs > 5 ? papwszArgs[5] : NULL;

    HKEY hRootKey = ArgToRegistryRoot(pwszRoot);
    if (hRootKey == NULL)
        return EXIT_USAGE;

    uint32_t uPosition = 0;
    if (!ArgToUInt32Full(pwszPosition, "position", &uPosition))
        return EXIT_USAGE;

    uint32_t fFlags = 0;
    if (pwszFlags)
    {
        if (RTUtf16ICmpAscii(pwszFlags, "dup") == 0)
            fFlags = VBOX_REG_STRINGLIST_ALLOW_DUPLICATES;
        else if (RTUtf16ICmpAscii(pwszFlags, "no-dups") == 0)
            fFlags = 0;
        else
            return ErrorBadArg("flags", pwszFlags, "'dup' or 'no-dups'");
    }

    /*
     * Do the work.
     */
    int rcExit = RegistryAddStringToList(hRootKey, pwszSubKey, pwszValueName, pwszItem, uPosition, fFlags);
    if (rcExit == EXIT_OK)
        PrintSWSWSWSWS("Successfully added '", pwszItem, "' to ", RegistryRootToWStr(hRootKey), "/'", pwszSubKey, "'/'",
                       pwszValueName, "'\r\n");

    return rcExit;
}


/**
 * Removes an item from a comma separated registry string (REG_SZ).
 *
 * Only operates in HKLM for now, if needed it can be extended later for use
 * with other hives.
 *
 * @return  Exit code (EXIT_OK, EXIT_FAIL)
 * @param   hRootKey            The root key.
 * @param   pwszSubKey          Subkey containing the list value.
 * @param   pwszValueName       The value name.
 * @param   pwszItemToRemove    The item to remove from the list.  Empty values
 *                              are not supported.
 */
static int RegistryRemoveStringFromList(HKEY hRootKey, const wchar_t *pwszSubKey, const wchar_t *pwszValueName,
                                        const wchar_t *pwszItemToRemove)
{
    /*
     * Open the specified key.
     */
    HKEY hKey = NULL;
    LSTATUS lrc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, pwszSubKey, 0 /*dwOptions*/, KEY_READ | KEY_WRITE, &hKey);
    if (lrc != ERROR_SUCCESS)
        return ErrorMsgLStatusSWSWSRS("RegistryRemoveStringFromList: RegOpenKeyExW ", RegistryRootToWStr(hRootKey),
                                      "/'", pwszSubKey, "' failed: ", lrc, NULL);

    /*
     * Query the specified value.
     */
    int     rcExit         = EXIT_FAIL;
    wchar_t wszValue[1296] = { 0 };
    DWORD   cbValue        = sizeof(wszValue) - sizeof(wchar_t);
    DWORD   dwType         = 0;
    lrc = RegQueryValueEx(hKey, pwszValueName, NULL /*pReserved*/, &dwType, (LPBYTE)wszValue, &cbValue);
    if (lrc == ERROR_SUCCESS && dwType == REG_SZ)
    {
#ifdef DEBUG
        PrintSWS("RegistryRemoveStringFromList: Value string: '", wszValue, "'\r\n");
#endif

        /*
         * Scan for item, shifting the query result as we scan.
         */
        size_t const      cwcItemToRemove = RTUtf16Len(pwszItemToRemove);
        wchar_t volatile *pwszSrc         = wszValue;
        wchar_t volatile *pwszDst         = wszValue;
        for (;;)
        {
            /* Skip leading commas: */
            wchar_t    wc            = *pwszSrc;
            bool const fLeadingComma = wc == ',';
            if (fLeadingComma)
                do
                    wc = *++pwszSrc;
                while (wc == ',');
            if (!wc)
                break; /* don't preserve trailing commas? Old code didn't (see strtok_r code). */

            /* Start of a new 'value', so, find the end of it. */
            wchar_t volatile *pwszSrcEnd = pwszSrc + 1;
            do
                wc = *++pwszSrcEnd;
            while (wc != '\0' && wc != ',');
            size_t const cwcItem = (size_t)(pwszSrcEnd - pwszSrc);

            /* If it matches pwszItemToRemove, do not copy it. */
            ASMCompilerBarrier(); /* Paranoia ^ 2 */
            if (IsStringListItemMatch(pwszSrc, cwcItem, pwszItemToRemove, cwcItemToRemove))
            {
                pwszSrc = pwszSrcEnd;
                if (!fLeadingComma)
                    while (*pwszSrc == ',')
                        pwszSrc++;
            }
            else
            {
                if (fLeadingComma)
                    *pwszDst++ = ',';
                memmove((void *)pwszDst, (void const *)pwszSrc, cwcItem * sizeof(*pwszDst));
                pwszDst += cwcItem;
                pwszSrc  = pwszSrcEnd;
                ASMCompilerBarrier(); /* paranoia ^ 3 */
            }

            /* pwszSrc should not point at a comma or a zero terminator. */
        }
        *pwszDst = '\0';
#ifdef DEBUG
        PrintSWS("RegistryRemoveStringFromList: New value:    '", wszValue, "'\r\n");
#endif

        /*
         * Save the new value if we've made any changes.
         */
        if (pwszDst == pwszSrc)
            rcExit = EXIT_OK;
        else
        {
            cbValue = (DWORD)((pwszDst + 1 - &wszValue[0]) * sizeof(wchar_t));
            lrc = RegSetValueExW(hKey, pwszValueName, 0, REG_SZ, (LPBYTE)wszValue, cbValue);
            if (lrc == ERROR_SUCCESS)
                rcExit = EXIT_OK;
            else
                ErrorMsgLStatusSWSWSWSWSRS("RegistryRemoveStringFromList: RegSetValueExW ", RegistryRootToWStr(hRootKey), "/'",
                                           pwszSubKey, "'/'", pwszValueName, "' = '", wszValue, "' failed: ", lrc, NULL);
        }
    }
    else if (lrc == ERROR_FILE_NOT_FOUND)
    {
#ifdef DEBUG
        PrintStr("RegistryRemoveStringFromList: Value not present in registry\r\n");
#endif
        rcExit = EXIT_OK;
    }
    else if (lrc != ERROR_SUCCESS)
        ErrorMsgLStatusSWSWSWSRS("RegistryRemoveStringFromList: RegQueryValueEx ", RegistryRootToWStr(hRootKey), "/'",
                                 pwszSubKey, "'/'", pwszValueName, "' failed: ", lrc, NULL);
    else
        ErrorMsgLStatusSWSWSWSRS("RegistryRemoveStringFromList: Unexpected value type for ", RegistryRootToWStr(hRootKey), "/'",
                                 pwszSubKey, "'/'", pwszValueName, "': ", (LSTATUS)dwType, ", expected REG_SZ (1)");
    RegCloseKey(hKey);
    return rcExit;
}


/**
 * Handles 'netprovider remove'.
 */
static int handleNetProviderRemove(unsigned cArgs, wchar_t **papwszArgs)
{
    const wchar_t * const pwszProvider = papwszArgs[0];
    PrintSWS("Removing network provider '", pwszProvider, "' ...\r\n");

    int rcExit = RegistryRemoveStringFromList(HKEY_LOCAL_MACHINE,
                                              L"System\\CurrentControlSet\\Control\\NetworkProvider\\Order",
                                              L"ProviderOrder",
                                              pwszProvider);
    if (rcExit == EXIT_OK)
        PrintStr("Network provider successfully removed!\r\n");

    RT_NOREF(cArgs);
    return rcExit;
}


/**
 * Handles 'registry dellistitem'.
 */
static int handleRegistryDelListItem(unsigned cArgs, wchar_t **papwszArgs)
{
    /*
     * Parameters.
     */
    RT_NOREF(cArgs);
    wchar_t const * const pwszRoot      = papwszArgs[0];
    wchar_t const * const pwszSubKey    = papwszArgs[1];
    wchar_t const * const pwszValueName = papwszArgs[2];
    wchar_t const * const pwszItem      = papwszArgs[3];

    HKEY hRootKey = ArgToRegistryRoot(pwszRoot);
    if (hRootKey == NULL)
        return EXIT_USAGE;

    /*
     * Do the work.
     */
    int rcExit = RegistryRemoveStringFromList(hRootKey, pwszSubKey, pwszValueName, pwszItem);
    if (rcExit == EXIT_OK)
        PrintSWSWSWSWS("Successfully removed '", pwszItem, "' from ", RegistryRootToWStr(hRootKey), "/'", pwszSubKey, "'/'",
                       pwszValueName, "'\r\n");

    return rcExit;
}


/**
 * Handles 'registry write'.
 */
static int handleRegistryWrite(unsigned cArgs, wchar_t **papwszArgs)
{
    /*
     * Mandatory parameters.
     */
    wchar_t const * const pwszRoot      = papwszArgs[0];
    wchar_t const * const pwszSubKey    = papwszArgs[1];
    wchar_t const * const pwszValueName = papwszArgs[2];
    wchar_t const * const pwszType      = papwszArgs[3];
    wchar_t const * const pwszValue     = papwszArgs[4];

    /*
     * Root key:
     */
    HKEY hRootKey = ArgToRegistryRoot(pwszRoot);
    if (hRootKey == NULL)
        return EXIT_USAGE;

    /*
     * Type and value with default length.
     */
    union
    {
        uint32_t    dw;
        uint64_t    qw;
    } uValue;
    DWORD           dwType;
    DWORD           cbValue;
    BYTE const     *pbValue;
    if (   RTUtf16ICmpAscii(pwszType, "REG_BINARY") == 0
        || RTUtf16ICmpAscii(pwszType, "REG_BIN") == 0
        || RTUtf16ICmpAscii(pwszType, "BINARY") == 0)
    {
        dwType  = REG_BINARY;
        cbValue = (DWORD)(RTUtf16Len(pwszValue) + 1) * sizeof(wchar_t);
        pbValue = (BYTE const *)pwszValue;
    }
    else if (   RTUtf16ICmpAscii(pwszType, "REG_DWORD") == 0
             || RTUtf16ICmpAscii(pwszType, "DWORD") == 0)
    {
        if (!ArgToUInt32Full(pwszValue, "dword value", &uValue.dw))
            return EXIT_USAGE;
        dwType  = REG_DWORD;
        pbValue = (BYTE const *)&uValue.dw;
        cbValue = sizeof(uValue.dw);
    }
    else if (   RTUtf16ICmpAscii(pwszType, "REG_QWORD") == 0
             || RTUtf16ICmpAscii(pwszType, "QWORD") == 0)
    {
        if (!ArgToUInt64Full(pwszValue, "qword value", &uValue.qw))
            return EXIT_USAGE;
        dwType  = REG_QWORD;
        pbValue = (BYTE const *)&uValue.qw;
        cbValue = sizeof(uValue.qw);
    }
    else if (   RTUtf16ICmpAscii(pwszType, "REG_SZ") == 0
             || RTUtf16ICmpAscii(pwszType, "SZ") == 0)
    {
        dwType  = REG_SZ;
        cbValue = (DWORD)((RTUtf16Len(pwszValue) + 1) * sizeof(wchar_t));
        pbValue = (BYTE const *)pwszValue;
    }
    else
        return ErrorBadArg("type", pwszType, "");

    /*
     * Binary only: Reinterpret the input as - optional.
     */
    if (cArgs > 5)
    {
        if (dwType != REG_BINARY)
            return ErrorMsg("The 'binary-conversion' argument is currently only supported for REG_BINARY type values!");
        if (RTUtf16ICmpAscii(papwszArgs[5], "dword") == 0)
        {
            if (!ArgToUInt32Full(pwszValue, "dword(/binary) value", &uValue.dw))
                return EXIT_USAGE;
            pbValue = (BYTE const *)&uValue.dw;
            cbValue = sizeof(uValue.dw);
        }
        else if (RTUtf16ICmpAscii(papwszArgs[5], "qword") == 0)
        {
            if (!ArgToUInt64Full(pwszValue, "qword(/binary) value", &uValue.qw))
                return EXIT_USAGE;
            pbValue = (BYTE const *)&uValue.qw;
            cbValue = sizeof(uValue.qw);
        }
        else
            return ErrorBadArg("binary-conversion", papwszArgs[0], "dword");
    }

    /*
     * Binary only: Max length to write - optional.
     */
    if (cArgs> 6)
    {
        if (dwType != REG_BINARY)
            return ErrorMsg("The 'max-size' argument is currently only supported for REG_BINARY type values!");
        uint32_t cbMaxValue;
        if (!ArgToUInt32Full(papwszArgs[6], "max-size", &cbMaxValue))
            return EXIT_USAGE;
        if (cbValue > cbMaxValue)
            cbValue = cbMaxValue;
    }

    /*
     * Do the writing.
     */
    HKEY    hKey = NULL;
    LSTATUS lrc  = RegCreateKeyExW(hRootKey, pwszSubKey, 0 /*Reserved*/, NULL /*pwszClass*/, 0 /*dwOptions*/,
                                   KEY_WRITE, NULL /*pSecAttr*/, &hKey, NULL /*pdwDisposition*/);
    if (lrc != ERROR_SUCCESS)
        return ErrorMsgLStatusSWSWSRS("RegCreateKeyExW ", RegistryRootToWStr(hRootKey), "/'", pwszSubKey, "' failed: ", lrc, NULL);

    lrc = RegSetValueExW(hKey, pwszValueName, 0, dwType, pbValue, cbValue);
    RegCloseKey(hKey);
    if (lrc != ERROR_SUCCESS)
        return ErrorMsgLStatusSWSWSWSRS("RegSetValueExW ", RegistryRootToWStr(hRootKey), "/'", pwszSubKey, "'/'",
                                        pwszValueName, "' failed: ",  lrc, NULL);
    return EXIT_OK;
}


/**
 * Handles 'registry delete'.
 */
static int handleRegistryDelete(unsigned cArgs, wchar_t **papwszArgs)
{
    /*
     * Parameters.
     */
    RT_NOREF(cArgs);
    wchar_t const * const pwszRoot      = papwszArgs[0];
    wchar_t const * const pwszSubKey    = papwszArgs[1];
    wchar_t const * const pwszValueName = papwszArgs[2];

    HKEY const hRootKey = ArgToRegistryRoot(pwszRoot);
    if (hRootKey == NULL)
        return EXIT_USAGE;

    /*
     * Do the deleting.
     */
    HKEY    hKey = NULL;
    LSTATUS lrc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, papwszArgs[1] /*pwszSubKey*/, 0 /*dwOptions*/, KEY_READ | KEY_WRITE, &hKey);
    if (lrc != ERROR_FILE_NOT_FOUND)
    {
        if (lrc != ERROR_SUCCESS)
            return ErrorMsgLStatusSWSWSRS("RegOpenKeyExW ", pwszRoot, "/'", pwszSubKey, "' failed: ", lrc, NULL);

        lrc = RegDeleteValueW(hKey, pwszValueName);
        RegCloseKey(hKey);
        if (lrc != ERROR_SUCCESS && lrc != ERROR_FILE_NOT_FOUND)
            return ErrorMsgLStatusSWSWSWSRS("RegDeleteValueW ", pwszRoot, "/'", pwszSubKey, "'/'",
                                            pwszValueName, "' failed: ",  lrc, NULL);
    }
    return EXIT_OK;
}


/** Handles 'version' and its aliases. */
static int handleVersion(unsigned cArgs, wchar_t **papwszArgs)
{
    PrintStr(RT_XSTR(VBOX_VERSION_MAJOR) "." RT_XSTR(VBOX_VERSION_MINOR) "." RT_XSTR(VBOX_VERSION_BUILD) "r" RT_XSTR(VBOX_SVN_REV) "\r\n");
    RT_NOREF(cArgs, papwszArgs);
    return EXIT_OK;
}


/** Handles 'help' and all its aliases. */
static int handleHelp(unsigned cArgs, wchar_t **papwszArgs)
{
    /*       "0         1         2         3         4         5         6         7         8 */
    /*       "012345678901234567890123456789012345678901234567890123456789012345678901234567890 */
    PrintStr("VirtualBox Guest Additions Installation Helper for Windows\r\n"
             "Version: " RT_XSTR(VBOX_VERSION_MAJOR) "." RT_XSTR(VBOX_VERSION_MINOR) "." RT_XSTR(VBOX_VERSION_BUILD) "r" RT_XSTR(VBOX_SVN_REV) "\r\n"
             "\r\n"
             "Syntax: VBoxDrvInst <command> <subcommand>\r\n"
             "\r\n"
             "Drivers:\r\n"
             "    VBoxDrvInst driver install <inf-file> [pnp-id] [log-file]\r\n"
             "    VBoxDrvInst driver uninstall <inf-file> <model> [pnp-id] [log-file]\r\n"
             "    VBoxDrvInst driver executeinf <inf-file> [section]\r\n"
             "    VBoxDrvInst driver nt4-install-video [install-dir]\r\n"
             "\r\n"
             "Service:\r\n"
             "    VBoxDrvInst service create <name> <display-name> <service-type>\r\n"
             "        <start-type> <binary-path> [load-order] [deps] [user] [password]\r\n"
             "    VBoxDrvInst service delete <name>\r\n"
             "\r\n"
             "Network Provider:\r\n"
             "    VBoxDrvInst netprovider add <name> <position>\r\n"
             "    VBoxDrvInst netprovider remove <name>\r\n"
             "\r\n"
             "Registry:\r\n"
             "    VBoxDrvInst registry write <root> <sub-key> <value-name> <type> <value>\r\n"
             "        [binary-conversion] [max-size]\r\n"
             "    VBoxDrvInst registry delete <root> <sub-key> <value-name>\r\n"
             /** @todo Add roots for these two. */
             "    VBoxDrvInst registry addmultisz <sub-key> <value-name> <to-add> <position>\r\n"
             "    VBoxDrvInst registry delmultisz <sub-key> <value-name> <to-remove>\r\n"
             "    VBoxDrvInst registry addlistitem <root> <sub-key> <value-name> <to-add>\r\n"
             "        [position [dup|no-dup]]\r\n"
             "    VBoxDrvInst registry dellistitem <root> <sub-key> <value-name> <to-remove>\r\n"
             "\r\n"
             "Standard options:\r\n"
             "    VBoxDrvInst [help|--help|/help|-h|/h|-?|/h] [...]\r\n"
             "    VBoxDrvInst [version|--version|-V]\r\n"
             );
    RT_NOREF(cArgs, papwszArgs);
    return EXIT_OK;
}


int wmain(int argc, wchar_t **argv)
{
    int rc = RTR3InitExeNoArguments(0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    static struct
    {
        const char *pszCmd;
        const char *pszSubCmd;
        unsigned    cMin, cMax;
        int       (*pfnHandler)(unsigned cArgs, wchar_t **papwszArgs);
    } s_aActions[] =
    {
        { "driver",         "install",              1,  3, handleDriverInstall },
        { "driver",         "uninstall",            2,  4, handleDriverUninstall },
        { "driver",         "executeinf",           1,  3, handleDriverExecuteInf },
        { "driver",         "nt4-install-video",    0,  1, handleDriverNt4InstallVideo },
        { "service",        "create",               5,  9, handleServiceCreate },
        { "service",        "delete",               1,  1, handleServiceDelete },
        { "netprovider",    "add",                  1,  2, handleNetProviderAdd },
        { "netprovider",    "remove",               1,  2, handleNetProviderRemove },
        { "registry",       "addlistitem",          4,  6, handleRegistryAddListItem },
        { "registry",       "dellistitem",          4,  4, handleRegistryDelListItem },
        { "registry",       "addmultisz",           4,  4, handleRegistryAddMultiSz },
        { "registry",       "delmultisz",           3,  3, handleRegistryDelMultiSz },
        { "registry",       "write",                5,  7, handleRegistryWrite },
        { "registry",       "delete",               3,  3, handleRegistryDelete },

        { "help",           NULL,                   0, ~0U, handleHelp },
        { "--help",         NULL,                   0, ~0U, handleHelp },
        { "/help",          NULL,                   0, ~0U, handleHelp },
        { "-h",             NULL,                   0, ~0U, handleHelp },
        { "/h",             NULL,                   0, ~0U, handleHelp },
        { "-?",             NULL,                   0, ~0U, handleHelp },
        { "/?",             NULL,                   0, ~0U, handleHelp },
        { "version",        NULL,                   0, ~0U, handleVersion },
        { "--version",      NULL,                   0, ~0U, handleVersion },
        { "-V",             NULL,                   0, ~0U, handleVersion },
    };

    /*
     * Lookup the action handler.
     */
    int rcExit = EXIT_USAGE;
    if (argc >= 2)
    {
        const wchar_t * const pwszCmd    = argv[1];
        const wchar_t * const pwszSubCmd = argc > 2 ? argv[2] : NULL;
        unsigned              i          = 0;
        for (i = 0; i < RT_ELEMENTS(s_aActions); i++)
            if (   RTUtf16ICmpAscii(pwszCmd, s_aActions[i].pszCmd) == 0
                && (   !s_aActions[i].pszSubCmd
                    || RTUtf16ICmpAscii(pwszSubCmd, s_aActions[i].pszSubCmd) == 0))
            {
                unsigned   const cArgs      = (unsigned)argc - (s_aActions[i].pszSubCmd ? 3 : 2);
                wchar_t ** const papwszArgs = &argv[s_aActions[i].pszSubCmd ? 3 : 2];
                if (cArgs >= s_aActions[i].cMin && cArgs <= s_aActions[i].cMax)
                    rcExit = s_aActions[i].pfnHandler(cArgs, papwszArgs);
                else
                {
                    bool const fTooFew = cArgs < s_aActions[i].cMin;
                    ErrorMsgBegin(fTooFew ? "Too few parameters for '" : "Too many parameters for '");
                    ErrorMsgStr(s_aActions[i].pszCmd);
                    if (s_aActions[i].pszSubCmd)
                    {
                        ErrorMsgStr(" ");
                        ErrorMsgStr(s_aActions[i].pszSubCmd);
                    }
                    ErrorMsgStr("'! Got ");
                    ErrorMsgU64(cArgs);
                    ErrorMsgStr(fTooFew ? ", expected at least " : ", expected at most ");;
                    ErrorMsgU64(fTooFew ? s_aActions[i].cMin : s_aActions[i].cMax);
                    ErrorMsgEnd(".");
                }
                break;
            }
        if (i >= RT_ELEMENTS(s_aActions))
        {
            ErrorMsgBegin("Unknown action '");
            ErrorMsgWStr(pwszCmd);
            if (pwszSubCmd)
            {
                ErrorMsgBegin(" ");
                ErrorMsgWStr(pwszSubCmd);
            }
            ErrorMsgEnd("'! Please consult \"--help\" for more information.\r\n");
        }
    }
    else
        ErrorMsg("No parameters given. Please consult \"--help\" for more information.\r\n");
    return rcExit;
}


#ifdef IPRT_NO_CRT
int main(int argc, char **argv)
{
    /*
     * Convert the arguments to UTF16 and call wmain.  We don't bother freeing
     * any of these strings as the process is exiting and it's a waste of time.
     */
    wchar_t **papwszArgs = (wchar_t **)alloca((argc + 1) * sizeof(wchar_t *));
    int i = 0;
    while (i < argc)
    {
        papwszArgs[i] = NULL;
        int rc = RTStrToUtf16(argv[i], &papwszArgs[i]);
        if (RT_SUCCESS(rc))
            i++;
        else
            return ErrorMsg("Failed to convert command line arguments to UTF16!!");
    }
    papwszArgs[i] = NULL;
    return wmain(argc, papwszArgs);
}
#endif

