/* $Id: VBoxLA.cpp 108105 2025-01-28 16:08:20Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxLA - VBox Location Awareness notifications.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/list.h>
#include <iprt/ldr.h>
#include <iprt/log.h>
#include <iprt/utf16.h>

#define _WIN32_WINNT 0x0501
#include <iprt/win/windows.h>

#include "VBoxTray.h"
#include "VBoxLA.h"


/*********************************************************************************************************************************
*   Defines                                                                                                                      *
*********************************************************************************************************************************/
#define REG_KEY_LEN 1024
#define MAX_CLIENT_NAME_CHARS 1024

#define LA_DO_NOTHING           0
#define LA_DO_ATTACH            1
#define LA_DO_DETACH            2
#define LA_DO_DETACH_AND_ATTACH 3
#define LA_DO_ATTACH_AND_DETACH 4


#define LA_UTCINFO_CLIENT_NAME      0
#define LA_UTCINFO_CLIENT_IPADDR    1
#define LA_UTCINFO_CLIENT_LOCATION  2
#define LA_UTCINFO_CLIENT_OTHERINFO 3
#define LA_UTCINFO_CLIENT_INFO_LAST 3

#define LA_UTCINFO_PROP_NAME  0
#define LA_UTCINFO_PROP_VALUE 1


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct _VBOXLACONTEXT
{
    const VBOXTRAYSVCENV *pEnv;

    bool fLogEnabled;
    bool fDetachOnDisconnect;

    uint32_t u32GuestPropHandle;  /* The client identifier of the guest property system. */

    RTLISTANCHOR listAttachActions;
    RTLISTANCHOR listDetachActions;

    uint64_t u64LastQuery;        /* The timestamp of the last query of the properties. */

    uint32_t u32Action;           /* Which action to do: LA_DO_*. */
    uint32_t u32PrevAction;       /* Which action were done last time. */

    struct                        /* Information about the client, which properties are monitored. */
    {
        uint32_t u32ClientId;     /* The RDP client identifier. 0 if none. */

        uint32_t u32LastAttach;
        uint64_t u64LastAttachTimestamp;

        char *pszLastName;
        uint64_t u64LastNameTimestamp;

        char *pszPropName;       /* The actual Client/%ID%/Name property name with client id. */
        char *pszPropIPAddr;      /* The actual Client/%ID%/IPAddr property name with client id. */
        char *pszPropLocation;   /* The actual Client/%ID%/Location property name with client id. */
        char *pszPropOtherInfo;  /* The actual Client/%ID%/OtherInfo property name with client id. */

        char *pszPropAttach;      /* The actual Client/%ID%/Attach property name with client id. */

        char *pszPropWaitPattern; /* Which properties are monitored. */
    } activeClient;

    BOOL (WINAPI * pfnProcessIdToSessionId)(DWORD dwProcessId, DWORD *pSessionId);
} VBOXLACONTEXT, *PVBOXLACONTEXT;

typedef struct _ACTIONENTRY
{
    RTLISTNODE nodeActionEntry;
    uint32_t u32Index;
    WCHAR wszCommandLine[1];
} ACTIONENTRY, *PACTIONENTRY;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static VBOXLACONTEXT g_Ctx = { 0 };

static const char * const g_pszPropActiveClient = "/VirtualBox/HostInfo/VRDP/ActiveClient";

static const char * const g_pszPropAttachTemplate = "/VirtualBox/HostInfo/VRDP/Client/%u/Attach";

static const char * const g_pszVolatileEnvironment = "Volatile Environment";

static const WCHAR *const g_pwszClientName = L"CLIENTNAME";

static const WCHAR * const g_pwszUTCINFOClientInfo[] =
{
    L"UTCINFO_CLIENTNAME",
    L"UTCINFO_CLIENTIPA",
    L"UTCINFO_CLIENTLOCATION",
    L"UTCINFO_CLIENTOTHERINFO"
};

static const char * const g_pszPropInfoTemplates[] =
{
    "/VirtualBox/HostInfo/VRDP/Client/%u/Name",
    "/VirtualBox/HostInfo/VRDP/Client/%u/IPAddr",
    "/VirtualBox/HostInfo/VRDP/Client/%u/Location",
    "/VirtualBox/HostInfo/VRDP/Client/%u/OtherInfo"
};

#ifdef RT_ARCH_AMD64
static const WCHAR *g_pwszRegKeyDisconnectActions = L"Software\\Wow6432Node\\Oracle\\Sun Ray\\ClientInfoAgent\\DisconnectActions";
static const WCHAR *g_pwszRegKeyReconnectActions  = L"Software\\Wow6432Node\\Oracle\\Sun Ray\\ClientInfoAgent\\ReconnectActions";
#else
static const WCHAR *g_pwszRegKeyDisconnectActions = L"Software\\Oracle\\Sun Ray\\ClientInfoAgent\\DisconnectActions";
static const WCHAR *g_pwszRegKeyReconnectActions  = L"Software\\Oracle\\Sun Ray\\ClientInfoAgent\\ReconnectActions";
#endif /* !RT_ARCH_AMD64 */

static const char g_szCommandPrefix[] = "Command";


static BOOL laGetRegistryDWORD(const WCHAR *pwszRegKey, const WCHAR *pwszName, DWORD *pdwValue)
{
    LONG lErr;

    HKEY hKey;
    lErr = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                         pwszRegKey,
                         0,
                         KEY_QUERY_VALUE,
                         &hKey);

    if (lErr != ERROR_SUCCESS)
    {
        LogRel(("LA: RegOpenKeyExW: failed [%ls]\n",
                    pwszRegKey));
        return FALSE;
    }

    DWORD nRegData = sizeof(DWORD);
    DWORD dwType = 0;
    lErr = RegQueryValueExW(hKey,
                            pwszName,
                            NULL,
                            &dwType,
                            (BYTE *)pdwValue,
                            &nRegData);

    if (lErr != ERROR_SUCCESS)
    {
        LogRel(("LA: RegQueryValueExW: failed [%ls/%ls]\n",
                    pwszRegKey, pwszName));
        RegCloseKey(hKey);
        return FALSE;
    }

    if (nRegData != sizeof(DWORD))
    {
        LogRel(("LA: buffer overflow reg %d, [%ls]\n",
                    nRegData, pwszRegKey));
        RegCloseKey(hKey);
        return FALSE;
    }

    if (dwType != REG_DWORD)
    {
        LogRel(("LA: wrong type %d, [%ls/%ls]\n",
                    dwType, pwszRegKey, pwszName));
        RegCloseKey(hKey);
        return FALSE;
    }

    RegCloseKey(hKey);

    if (lErr != ERROR_SUCCESS)
    {
        return FALSE;
    }

    return TRUE;
}

static void ActionExecutorDeleteActions(RTLISTANCHOR *listActions)
{
    ACTIONENTRY *pIter;
    ACTIONENTRY *pIterNext;
    RTListForEachSafe(listActions, pIter, pIterNext, ACTIONENTRY, nodeActionEntry)
    {
        RTListNodeRemove(&pIter->nodeActionEntry);
        RTMemFree(pIter);
    }
}

static BOOL ActionExecutorEnumerateRegistryKey(const WCHAR *pwszRegKey,
                                               RTLISTANCHOR *listActions)
{
    BOOL bRet = TRUE;
    HKEY hKey;
    DWORD dwErr;

    dwErr = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          pwszRegKey,
                          0,
                          KEY_QUERY_VALUE,
                          &hKey);

    if (dwErr != ERROR_SUCCESS)
    {
        LogFlowFunc(("Can't open registry key [%ls], error %d\n",
               pwszRegKey, dwErr));
        return FALSE;
    }

    DWORD dwIndex = 0;

    for (;;)
    {
        DWORD dwRet;

        WCHAR wszValueName[256];
        DWORD cchValueName = RT_ELEMENTS(wszValueName);
        DWORD type;
        BYTE abData[1024];
        DWORD cbData = sizeof(abData);

        dwRet = RegEnumValueW(hKey,
                              dwIndex++,
                              wszValueName,
                              &cchValueName,
                              NULL,
                              &type,
                              abData,
                              &cbData);

        if (dwRet == ERROR_NO_MORE_ITEMS)
        {
            LogFlowFunc(("Enumeration exhausted\n"));
            bRet = TRUE;
            break;
        }
        else if (dwRet != ERROR_SUCCESS)
        {
            LogFlowFunc(("Enumeration failed, error %d\n",
                   dwRet));
            bRet = FALSE;
            break;
        }

        if ((type != REG_SZ) && (type != REG_EXPAND_SZ))
        {
            LogFlowFunc(("skipped type %d\n",
                   type));
            continue;
        }

        char szName[256];
        char *pszName = &szName[0];
        int rc = RTUtf16ToUtf8Ex(wszValueName,
                                 RT_ELEMENTS(wszValueName),
                                 &pszName, sizeof(szName), NULL);
        if (RT_FAILURE(rc))
        {
            LogFlowFunc(("RTUtf16ToUtf8Ex for [%ls] rc %Rrc\n",
                    wszValueName, rc));
            continue;
        }

        /* Check if the name starts with "Command" */
        if (RTStrNICmp(szName, g_szCommandPrefix, RT_ELEMENTS(g_szCommandPrefix) - 1) != 0)
        {
            LogFlowFunc(("skipped prefix %s\n",
                   szName));
            continue;
        }

        char *pszIndex = &szName[RT_ELEMENTS(g_szCommandPrefix) - 1];

        uint32_t nIndex = RTStrToUInt32(pszIndex);
        if (nIndex == 0)
        {
            LogFlowFunc(("skipped index %s\n",
                   szName));
            continue;
        }

        /* Allocate with terminating nul after data. */
        ACTIONENTRY *pEntry = (ACTIONENTRY *)RTMemAlloc(sizeof(ACTIONENTRY) + cbData);
        if (!pEntry)
        {
            LogFlowFunc(("RTMemAlloc failed\n"));
            bRet = FALSE;
            break;
        }

        RT_ZERO(pEntry->nodeActionEntry);
        pEntry->u32Index = nIndex;
        memcpy(pEntry->wszCommandLine, abData, cbData);
        pEntry->wszCommandLine[cbData / sizeof(WCHAR)] = 0;

        /* Insert the new entry to the list. Sort by index. */
        if (RTListIsEmpty(listActions))
        {
            RTListAppend(listActions, &pEntry->nodeActionEntry);
        }
        else
        {
            bool fAdded = false;
            ACTIONENTRY *pIter;
            RTListForEach(listActions, pIter, ACTIONENTRY, nodeActionEntry)
            {
                if (pIter->u32Index > nIndex)
                {
                    RTListNodeInsertBefore(&pIter->nodeActionEntry, &pEntry->nodeActionEntry);
                    fAdded = true;
                    break;
                }
            }
            if (!fAdded)
            {
                RTListAppend(listActions, &pEntry->nodeActionEntry);
            }
        }

        LogFlowFunc(("added %d %ls\n",
               pEntry->u32Index, pEntry->wszCommandLine));
    }

    RegCloseKey(hKey);

#ifdef LOG_ENABLED
    ACTIONENTRY *pIter;
    RTListForEach(listActions, pIter, ACTIONENTRY, nodeActionEntry)
    {
        LogFlowFunc(("[%u]: [%ls]\n",
               pIter->u32Index, pIter->wszCommandLine));
    }
#endif

    if (!bRet)
    {
        ActionExecutorDeleteActions(listActions);
    }

    LogFlowFunc(("action enum %d\n", bRet));

    return bRet;
}

static void ActionExecutorExecuteActions(RTLISTANCHOR *listActions)
{
    LogFlowFunc(("ExecuteActions\n"));

    ACTIONENTRY *pIter;
    RTListForEach(listActions, pIter, ACTIONENTRY, nodeActionEntry)
    {
        LogFlowFunc(("[%u]: [%ls]\n",
               pIter->u32Index, pIter->wszCommandLine));

        STARTUPINFOW si;
        PROCESS_INFORMATION pi;

        GetStartupInfoW(&si);

        if (!CreateProcessW(NULL,                  // lpApplicationName
                            pIter->wszCommandLine, // lpCommandLine
                            NULL,                  // lpProcessAttributes
                            NULL,                  // lpThreadAttributes
                            FALSE,                 // bInheritHandles
                            0,                     // dwCreationFlags
                            NULL,                  // lpEnvironment
                            NULL,                  // lpCurrentDirectory
                            &si,                   // lpStartupInfo
                            &pi))                  // lpProcessInformation
        {
            LogFlowFunc(("Executing [%ls] failed, error %d\n",
                   pIter->wszCommandLine, GetLastError()));
        }
        else
        {
            LogFlowFunc(("Executing [%ls] succeeded\n",
                   pIter->wszCommandLine));

            /* Don't care about waiting on the new process, so close these. */
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }

    LogFlowFunc(("ExecuteActions leave\n"));
}

static BOOL GetVolatileEnvironmentKey(PVBOXLACONTEXT pCtx, WCHAR *pwszRegKey, DWORD cbRegKey)
{
    BOOL fFound = FALSE;

    DWORD nSessionID;
    LONG lErr;
    HKEY hKey;
    char szRegKey[REG_KEY_LEN];

    /* Attempt to open HKCU\Volatile Environment\<session ID> first. */
    if (   pCtx->pfnProcessIdToSessionId
        && pCtx->pfnProcessIdToSessionId(GetCurrentProcessId(), &nSessionID))
    {
        RTStrPrintf(szRegKey, sizeof(szRegKey),
                    "%s\\%d",
                    g_pszVolatileEnvironment, nSessionID);

        lErr = RegOpenKeyExA(HKEY_CURRENT_USER,
                             szRegKey,
                             0,
                             KEY_SET_VALUE,
                             &hKey);

        if (lErr == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            fFound = TRUE;
        }
    }

    if (!fFound)
    {
        /* Fall back to HKCU\Volatile Environment. */
        RTStrPrintf(szRegKey, sizeof(szRegKey),
                    "%s",
                    g_pszVolatileEnvironment);

        lErr = RegOpenKeyExA(HKEY_CURRENT_USER,
                             szRegKey,
                             0,
                             KEY_SET_VALUE,
                             &hKey);

        if (lErr == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            fFound = TRUE;
        }
    }

    if (fFound)
    {
        LogFlowFunc(("GetVolatileEnvironmentKey: [%s]\n", szRegKey));

        /* Convert szRegKey to Utf16 string. */
        PRTUTF16 putf16Unicode = pwszRegKey;
        size_t cchUnicode = cbRegKey / sizeof(WCHAR);

        int rc = RTStrToUtf16Ex(szRegKey, RTSTR_MAX,
                                &putf16Unicode, cchUnicode, NULL);
        if (RT_FAILURE(rc))
        {
            LogFlowFunc(("RTStrToUtf16Ex failed %Rrc\n", rc));
            fFound = FALSE;
        }
        else
        {
            LogFlowFunc(("unicode [%ls]\n", putf16Unicode));
        }
    }
    else
    {
        LogFlowFunc(("GetVolatileEnvironmentKey: not found\n"));
    }

    return fFound;
}

static BOOL laGetUtcInfoClientName(PVBOXLACONTEXT pCtx, WCHAR *pwszClientName, DWORD cbClientName)
{
    LONG lErr;

    WCHAR wszRegKey[REG_KEY_LEN];
    if (!GetVolatileEnvironmentKey(pCtx, wszRegKey, sizeof(wszRegKey)))
    {
        return FALSE;
    }

    HKEY hKey;
    lErr = RegOpenKeyExW(HKEY_CURRENT_USER,
                         wszRegKey,
                         0,
                         KEY_QUERY_VALUE,
                         &hKey);

    if (lErr != ERROR_SUCCESS)
    {
        LogFlowFunc(("RegOpenKeyExW: failed [%ls]\n",
               wszRegKey));
        return FALSE;
    }

    DWORD nRegData;
    DWORD dwType;
    lErr = RegQueryValueExW(hKey,
                            g_pwszUTCINFOClientInfo[LA_UTCINFO_CLIENT_NAME],
                            NULL,
                            &dwType,
                            NULL,
                            &nRegData);

    if (lErr != ERROR_SUCCESS)
    {
        LogFlowFunc(("RegQueryValueExW: failed [%ls]\n",
               wszRegKey));
        RegCloseKey(hKey);
        return FALSE;
    }

    if (nRegData >= cbClientName)
    {
        LogFlowFunc(("buffer overflow reg %d, buffer %d, [%ls]\n",
               nRegData, cbClientName, wszRegKey));
        RegCloseKey(hKey);
        return FALSE;
    }

    if (dwType != REG_SZ)
    {
        LogFlowFunc(("wrong type %d, [%ls]\n",
               dwType, wszRegKey));
        RegCloseKey(hKey);
        return FALSE;
    }

    ZeroMemory(pwszClientName, cbClientName);

    lErr = RegQueryValueExW(hKey,
                            g_pwszUTCINFOClientInfo[LA_UTCINFO_CLIENT_NAME],
                            NULL,
                            NULL,
                            (BYTE *)pwszClientName,
                            &nRegData);

    RegCloseKey(hKey);

    if (lErr != ERROR_SUCCESS)
    {
        return FALSE;
    }

    return TRUE;
}

static BOOL laSetClientName(PVBOXLACONTEXT pCtx, const WCHAR *pwszClientName)
{
    LONG lErr;

    WCHAR wszRegKey[REG_KEY_LEN];
    if (!GetVolatileEnvironmentKey(pCtx, wszRegKey, sizeof(wszRegKey)))
    {
        return FALSE;
    }

    HKEY hKey;
    lErr = RegOpenKeyExW(HKEY_CURRENT_USER,
                         wszRegKey,
                         0,
                         KEY_SET_VALUE,
                         &hKey);

    if (lErr != ERROR_SUCCESS)
    {
        return FALSE;
    }

    DWORD nClientName = (lstrlenW(pwszClientName) + 1) * sizeof(WCHAR);
    lErr = RegSetValueExW(hKey,
                          g_pwszClientName,
                          0,
                          REG_SZ,
                          (BYTE*)pwszClientName,
                          nClientName);

    RegCloseKey(hKey);

    if (lErr != ERROR_SUCCESS)
    {
        return FALSE;
    }

    return TRUE;
}

static void laBroadcastSettingChange(void)
{
    DWORD_PTR dwResult;

    if (SendMessageTimeoutA(HWND_BROADCAST,
                            WM_SETTINGCHANGE,
                            NULL,
                            (LPARAM)"Environment",
                            SMTO_ABORTIFHUNG,
                            5000,
                            &dwResult) == 0)
    {
        LogFlowFunc(("SendMessageTimeout failed, error %ld\n", GetLastError()));
    }
}

static void laUpdateClientName(PVBOXLACONTEXT pCtx)
{
    WCHAR wszUtcInfoClientName[MAX_CLIENT_NAME_CHARS];

    if (laGetUtcInfoClientName(pCtx, wszUtcInfoClientName, sizeof(wszUtcInfoClientName)))
    {
        if (laSetClientName(pCtx, wszUtcInfoClientName))
            laBroadcastSettingChange();
    }
}

static void laOnClientLocationInfo(PVBOXLACONTEXT pCtx, char *pszClientInfo[][2])
{
    /*
     * Write the client location info to:
     * HKCU\Volatile Environment\<CLIENT_LOCATION_INFO> or
     * HKCU\Volatile Environment\<SessionID>\<CLIENT_LOCATION_INFO>
     * depending on whether this is a Terminal Services or desktop session
     * respectively.
     * The client location info are: Name, IPAddr, Location, OtherInfo
     */
    unsigned int idx;
    WCHAR wszRegKey[REG_KEY_LEN];
    if (!GetVolatileEnvironmentKey(pCtx, wszRegKey, sizeof(wszRegKey)))
    {
        LogFlowFunc(("Failed to get 'Volatile Environment' registry key\n"));
        return;
    }

    /* Now write the client name under the appropriate key. */
    LONG lRet;
    HKEY hKey;

    lRet = RegOpenKeyExW(HKEY_CURRENT_USER,
                         wszRegKey,
                         0,
                         KEY_SET_VALUE,
                         &hKey);

    if (lRet != ERROR_SUCCESS)
    {
        LogFlowFunc(("Failed to open key [%ls], error %lu\n", wszRegKey, lRet));
        return;
    }

    PRTUTF16 putf16UnicodeClientInfo[LA_UTCINFO_CLIENT_INFO_LAST + 1] = {NULL};
    for (idx = 0; idx <= LA_UTCINFO_CLIENT_INFO_LAST; idx++)
    {
        if (pszClientInfo[idx][LA_UTCINFO_PROP_VALUE] == NULL)
            break;

        /* pszClientInfo is UTF8, make an Unicode copy for registry. */
        size_t cchUnicodeClientInfo = 0;

        int rc = RTStrToUtf16Ex(pszClientInfo[idx][LA_UTCINFO_PROP_VALUE], MAX_CLIENT_NAME_CHARS,
                                &putf16UnicodeClientInfo[idx], 0, &cchUnicodeClientInfo);

        if (RT_FAILURE(rc))
        {
            LogFlowFunc(("RTStrToUniEx failed %Rrc\n", rc));
            break;
        }

        DWORD nDataLength = (DWORD)((cchUnicodeClientInfo + 1) * sizeof(WCHAR));
        lRet = RegSetValueExW(hKey,
                              g_pwszUTCINFOClientInfo[idx],
                              0,
                              REG_SZ,
                              (BYTE *)putf16UnicodeClientInfo[idx],
                              nDataLength);

        if (lRet != ERROR_SUCCESS)
        {
            LogFlowFunc(("RegSetValueExW failed error %lu for %s \n", lRet, g_pwszUTCINFOClientInfo[idx]));
        }
    }

    RegCloseKey(hKey);

    laBroadcastSettingChange();

    /* Also, write these info (Name, IPAddr, Location and Other Info) to the environment of this process, as it
     * doesn't listen for WM_SETTINGCHANGE messages.
     */

    for (idx = 0; idx <= LA_UTCINFO_CLIENT_INFO_LAST; idx++)
    {
        if (putf16UnicodeClientInfo[idx] == NULL)
            break;

        SetEnvironmentVariableW(g_pwszUTCINFOClientInfo[idx], putf16UnicodeClientInfo[idx]);

        RTUtf16Free(putf16UnicodeClientInfo[idx]);
    }
}

static void laDoAttach(PVBOXLACONTEXT pCtx)
{
    LogFlowFunc(("laDoAttach\n"));

    /* Hardcoded action. */
    laUpdateClientName(pCtx);

    /* Process configured actions. */
    ActionExecutorExecuteActions(&pCtx->listAttachActions);
}

static void laDoDetach(PVBOXLACONTEXT pCtx)
{
    LogFlowFunc(("laDoDetach\n"));

    /* Process configured actions. */
    ActionExecutorExecuteActions(&pCtx->listDetachActions);
}

static int laGetProperty(uint32_t u32GuestPropHandle, const char *pszName, uint64_t *pu64Timestamp, char **ppszValue)
{
    int rc = VINF_SUCCESS;

    /* The buffer for storing the data and its initial size.  We leave a bit
     * of space here in case the maximum values are raised.
     */
    uint32_t cbBuf = 1024;
    void *pvBuf = NULL;

    /* Because there is a race condition between our reading the size of a
     * property and the guest updating it, we loop a few times here and
     * hope.  Actually this should never go wrong, as we are generous
     * enough with buffer space.
     */
    unsigned i;
    for (i = 0; i < 3; ++i)
    {
        void *pvTmpBuf = RTMemRealloc(pvBuf, cbBuf);
        if (pvTmpBuf == NULL)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        pvBuf = pvTmpBuf;

        rc = VbglR3GuestPropRead(u32GuestPropHandle, pszName, pvBuf, cbBuf,
                                 NULL, pu64Timestamp, NULL,
                                 &cbBuf);
        if (rc != VERR_BUFFER_OVERFLOW)
        {
            break;
        }

        cbBuf += 1024;
    }

    if (RT_SUCCESS(rc))
    {
        LogFlowFunc(("laGetProperty: [%s]\n"
               "            value: [%s]\n"
               "        timestamp: %lld ns\n",
               pszName, (char *)pvBuf, *pu64Timestamp));

        *ppszValue = (char *)pvBuf;
    }
    else if (rc == VERR_NOT_FOUND)
    {
        LogFlowFunc(("laGetProperty: not found [%s]\n", pszName));
        RTMemFree(pvBuf);
    }
    else
    {
        LogFlowFunc(("Failed to retrieve the property value, error %Rrc\n", rc));
        RTMemFree(pvBuf);
    }

    return rc;
}

static int laWaitProperties(uint32_t u32GuestPropHandle,
                            const char *pszPatterns,
                            uint64_t u64LastTimestamp,
                            uint64_t *pu64Timestamp,
                            uint32_t u32Timeout)
{
    int rc = VINF_SUCCESS;

    /* The buffer for storing the data and its initial size.  We leave a bit
     * of space here in case the maximum values are raised.
     */
    void *pvBuf = NULL;
    uint32_t cbBuf = 4096;

    /* Because there is a race condition between our reading the size of a
     * property and the guest updating it, we loop a few times here and
     * hope.  Actually this should never go wrong, as we are generous
     * enough with buffer space.
     */
    unsigned i;
    for (i = 0; i < 3; ++i)
    {
        void *pvTmpBuf = RTMemRealloc(pvBuf, cbBuf);
        if (NULL == pvTmpBuf)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        pvBuf = pvTmpBuf;

        rc = VbglR3GuestPropWait(u32GuestPropHandle, pszPatterns, pvBuf, cbBuf,
                                 u64LastTimestamp, u32Timeout,
                                 NULL /* ppszName */,
                                 NULL /* ppszValue */,
                                 pu64Timestamp,
                                 NULL /* ppszFlags */,
                                 &cbBuf,
                                 NULL /* pfWasDeleted */);

        if (rc != VERR_BUFFER_OVERFLOW)
            break;

        cbBuf += 1024;
    }

    RTMemFree(pvBuf);

    return rc;
}

static int laGetUint32(uint32_t u32GuestPropHandle, const char *pszName, uint64_t *pu64Timestamp, uint32_t *pu32Value)
{
    uint64_t u64Timestamp = 0;
    char *pszValue = NULL;

    int rc = laGetProperty(u32GuestPropHandle,
                           pszName,
                           &u64Timestamp,
                           &pszValue);
    if (RT_SUCCESS(rc))
    {
        if (pszValue && *pszValue)
        {
            uint32_t u32 = 0;
            rc = RTStrToUInt32Full(pszValue, 10, &u32);

            if (RT_SUCCESS(rc))
            {
                *pu64Timestamp = u64Timestamp;
                *pu32Value = u32;
            }
        }
        else
        {
            rc = VERR_NOT_SUPPORTED;
        }
    }

    if (pszValue)
        RTMemFree(pszValue);

    LogFlowFunc(("laGetUint32: rc = %Rrc, [%s]\n", rc, pszName));
    return rc;
}

static int laGetString(uint32_t u32GuestPropHandle, const char *pszName, uint64_t *pu64Timestamp, char **ppszValue)
{
    int rc = laGetProperty(u32GuestPropHandle,
                           pszName,
                           pu64Timestamp,
                           ppszValue);

    LogFlowFunc(("laGetString: rc = %Rrc, [%s]\n", rc, pszName));
    return rc;
}

static int laGetActiveClient(PVBOXLACONTEXT pCtx, uint64_t *pu64Timestamp, uint32_t *pu32Value)
{
    int rc = laGetUint32(pCtx->u32GuestPropHandle,
                         g_pszPropActiveClient,
                         pu64Timestamp,
                         pu32Value);

    LogFlowFunc(("laGetActiveClient: rc %Rrc, %RU32, %RU64\n", rc, *pu32Value, *pu64Timestamp));
    return rc;
}

static int laUpdateCurrentState(PVBOXLACONTEXT pCtx, uint32_t u32ActiveClientId, uint64_t u64ActiveClientTS)
{
    /* Prepare the current state for the active client.
     * If u32ActiveClientId is 0, then there is no connected clients.
     */
    LogFlowFunc(("laUpdateCurrentState: %RU32 %RU64\n", u32ActiveClientId, u64ActiveClientTS));

    int rc = VINF_SUCCESS;

    int l;

    char **pClientInfoMap[LA_UTCINFO_CLIENT_INFO_LAST + 1] =
    {
        &pCtx->activeClient.pszPropName,
        &pCtx->activeClient.pszPropIPAddr,
        &pCtx->activeClient.pszPropLocation,
        &pCtx->activeClient.pszPropOtherInfo,
    };

    pCtx->activeClient.u32LastAttach = UINT32_MAX;
    pCtx->activeClient.u64LastAttachTimestamp = u64ActiveClientTS;

    if (pCtx->activeClient.pszLastName)
    {
        RTMemFree(pCtx->activeClient.pszLastName);
    }
    pCtx->activeClient.pszLastName = NULL;
    pCtx->activeClient.u64LastNameTimestamp = u64ActiveClientTS;

    unsigned int idx;

    for (idx = 0; idx <= LA_UTCINFO_CLIENT_INFO_LAST; idx++)
    {
        if (*pClientInfoMap[idx])
        {
            RTMemFree(*pClientInfoMap[idx]);
            *pClientInfoMap[idx] = NULL;
        }

        if (u32ActiveClientId != 0)
        {
            l = RTStrAPrintf(pClientInfoMap[idx],
                             g_pszPropInfoTemplates[idx],
                             u32ActiveClientId);

            if (l == -1)
            {
                *pClientInfoMap[idx] = NULL;
                rc = VERR_NO_MEMORY;
                break;
            }
        }
    }

    if (RT_SUCCESS(rc))
    {
        if (pCtx->activeClient.pszPropAttach)
        {
            RTMemFree(pCtx->activeClient.pszPropAttach);
            pCtx->activeClient.pszPropAttach = NULL;
        }
        if (u32ActiveClientId != 0)
        {
            l = RTStrAPrintf(&pCtx->activeClient.pszPropAttach,
                             g_pszPropAttachTemplate,
                             u32ActiveClientId);
            if (l == -1)
            {
                pCtx->activeClient.pszPropAttach = NULL;
                rc = VERR_NO_MEMORY;
            }
        }
    }

    if (RT_SUCCESS(rc))
    {
        if (pCtx->activeClient.pszPropWaitPattern)
        {
            RTMemFree(pCtx->activeClient.pszPropWaitPattern);
            pCtx->activeClient.pszPropWaitPattern = NULL;
        }
        if (u32ActiveClientId != 0)
        {
            l = RTStrAPrintf(&pCtx->activeClient.pszPropWaitPattern,
                             "%s|%s|%s|%s|%s",
                             pCtx->activeClient.pszPropName,
                             pCtx->activeClient.pszPropAttach,
                             pCtx->activeClient.pszPropIPAddr,
                             pCtx->activeClient.pszPropLocation,
                             pCtx->activeClient.pszPropOtherInfo);
            if (l == -1)
            {
                pCtx->activeClient.pszPropWaitPattern = NULL;
                rc = VERR_NO_MEMORY;
            }
        }
    }

    if (RT_SUCCESS(rc))
    {
        pCtx->activeClient.u32ClientId = u32ActiveClientId;
    }
    else
    {
        pCtx->activeClient.u32ClientId = 0;
    }

    LogFlowFunc(("laUpdateCurrentState rc = %Rrc\n", rc));
    return rc;
}

static int laWait(PVBOXLACONTEXT pCtx, uint64_t *pu64Timestamp, uint32_t u32Timeout)
{
    LogFlowFunc(("laWait [%s]\n", pCtx->activeClient.pszPropWaitPattern));

    int rc = laWaitProperties(pCtx->u32GuestPropHandle,
                              pCtx->activeClient.pszPropWaitPattern,
                              pCtx->u64LastQuery,
                              pu64Timestamp,
                              u32Timeout);

    LogFlowFunc(("laWait rc %Rrc\n", rc));
    return rc;
}

static void laProcessClientInfo(PVBOXLACONTEXT pCtx)
{
    /* Check if the name was changed. */
    /* Get the name string and check if it was changed since last time.
     * Write Client name, IPAddr, Location and Other Info to the registry if the name has changed.
     */
    uint64_t u64Timestamp = 0;
    int rc = VINF_SUCCESS;
    unsigned int idx;

    char *pClientInfoMap[][2] = {
                                  {pCtx->activeClient.pszPropName, NULL},
                                  {pCtx->activeClient.pszPropIPAddr, NULL},
                                  {pCtx->activeClient.pszPropLocation, NULL},
                                  {pCtx->activeClient.pszPropOtherInfo, NULL}
                                 };

    for (idx = 0; idx <= LA_UTCINFO_CLIENT_INFO_LAST; idx++)
    {
         rc = laGetString(pCtx->u32GuestPropHandle,
                         pClientInfoMap[idx][LA_UTCINFO_PROP_NAME],
                         &u64Timestamp,
                         &pClientInfoMap[idx][LA_UTCINFO_PROP_VALUE]);

         LogFlowFunc(("laProcessClientInfo: read [%s], at %RU64\n",
                      pClientInfoMap[idx][LA_UTCINFO_PROP_VALUE], u64Timestamp));

        if (RT_FAILURE(rc))
        {
            LogFlowFunc(("laProcessClientInfo failed at %s\n", pClientInfoMap[idx][LA_UTCINFO_PROP_NAME]));
            break;
        }
    }

    if (pClientInfoMap[LA_UTCINFO_CLIENT_NAME][LA_UTCINFO_PROP_VALUE] != NULL)
    {
        if (u64Timestamp != pCtx->activeClient.u64LastNameTimestamp)
        {
            laOnClientLocationInfo(pCtx, pClientInfoMap);

            pCtx->activeClient.u64LastNameTimestamp = u64Timestamp;
        }
    }

    for (idx = 0; idx <= LA_UTCINFO_CLIENT_INFO_LAST; idx++)
    {
        if (pClientInfoMap[idx][LA_UTCINFO_PROP_VALUE])
        {
            RTMemFree(pClientInfoMap[idx][LA_UTCINFO_PROP_VALUE]);
        }
    }
}

static void laProcessAttach(PVBOXLACONTEXT pCtx)
{
    /* Check if the attach was changed. */
    pCtx->u32Action = LA_DO_NOTHING;

    uint64_t u64Timestamp = 0;
    uint32_t u32Attach = UINT32_MAX;

    int rc = laGetUint32(pCtx->u32GuestPropHandle,
                         pCtx->activeClient.pszPropAttach,
                         &u64Timestamp,
                         &u32Attach);

    if (RT_SUCCESS(rc))
    {
        LogFlowFunc(("laProcessAttach: read %RU32, at %RU64\n", u32Attach, u64Timestamp));
        if (u64Timestamp != pCtx->activeClient.u64LastAttachTimestamp)
        {
            if (u32Attach != pCtx->activeClient.u32LastAttach)
            {
                LogFlowFunc(("laProcessAttach: changed\n"));

                /* Just do the last action. */
                pCtx->u32Action = u32Attach
                                ? LA_DO_ATTACH : LA_DO_DETACH;

                pCtx->activeClient.u32LastAttach = u32Attach;
            }
            else
            {
                LogFlowFunc(("laProcessAttach: same\n"));

                /* The property has changed but the value is the same,
                 * which means that it was changed and restored.
                 */
                pCtx->u32Action = u32Attach
                                ? LA_DO_DETACH_AND_ATTACH : LA_DO_ATTACH_AND_DETACH;
            }

            pCtx->activeClient.u64LastAttachTimestamp = u64Timestamp;
        }

    }

    LogFlowFunc(("laProcessAttach: action %RU32\n", pCtx->u32Action));
}

static void laDoActions(PVBOXLACONTEXT pCtx)
{
    /*
     * Check if the attach was changed.
     *
     * Caller assumes that this function will filter double actions.
     * That is two or more LA_DO_ATTACH will do just one LA_DO_ATTACH.
     */
    LogFlowFunc(("laDoActions: action %RU32, prev %RU32\n", pCtx->u32Action, pCtx->u32PrevAction));

    switch(pCtx->u32Action)
    {
        case LA_DO_ATTACH:
        {
            if (pCtx->u32PrevAction != LA_DO_ATTACH)
            {
                pCtx->u32PrevAction = LA_DO_ATTACH;
                laDoAttach(pCtx);
            }
        } break;

        case LA_DO_DETACH:
        {
            if (pCtx->u32PrevAction != LA_DO_DETACH)
            {
                pCtx->u32PrevAction = LA_DO_DETACH;
                laDoDetach(pCtx);
            }
        } break;

        case LA_DO_DETACH_AND_ATTACH:
        {
            if (pCtx->u32PrevAction != LA_DO_DETACH)
            {
                pCtx->u32PrevAction = LA_DO_DETACH;
                laDoDetach(pCtx);
            }
            pCtx->u32PrevAction = LA_DO_ATTACH;
            laDoAttach(pCtx);
        } break;

        case LA_DO_ATTACH_AND_DETACH:
        {
            if (pCtx->u32PrevAction != LA_DO_ATTACH)
            {
                pCtx->u32PrevAction = LA_DO_ATTACH;
                laDoAttach(pCtx);
            }
            pCtx->u32PrevAction = LA_DO_DETACH;
            laDoDetach(pCtx);
        } break;

        case LA_DO_NOTHING:
        default:
            break;
    }

    pCtx->u32Action = LA_DO_NOTHING;

    LogFlowFunc(("laDoActions: leave\n"));
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnPreInit}
 */
static DECLCALLBACK(int) vbtrLAPreInit(void)
{
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnOption}
 */
static DECLCALLBACK(int) vbtrLAOption(const char **ppszShort, int argc, char **argv, int *pi)
{
    RT_NOREF(ppszShort, argc, argv, pi);

    return -1;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnInit}
 */
DECLCALLBACK(int) vbtrLAInit(const PVBOXTRAYSVCENV pEnv, void **ppvInstance)
{
    AssertPtrReturn(pEnv, VERR_INVALID_POINTER);
    AssertPtrReturn(ppvInstance, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    PVBOXLACONTEXT pCtx = &g_Ctx; /* Only one instance at the moment. */
    AssertPtr(pCtx);

    pCtx->pEnv = pEnv;

    DWORD dwValue = 0;
    if (   laGetRegistryDWORD(L"SOFTWARE\\Oracle\\VirtualBox Guest Additions", L"VBoxTrayLog", &dwValue)
        && (dwValue & 0x10) != 0)
    {
         pCtx->fLogEnabled = true;
    }
    else
    {
         pCtx->fLogEnabled = false;
    }

    /* DetachOnDisconnect is enabled by default. */
    dwValue = 0x02;
    if (   laGetRegistryDWORD(L"SOFTWARE\\Oracle\\VirtualBox Guest Additions", L"VBoxTrayLA", &dwValue)
        && (dwValue & 0x02) == 0)
    {
         pCtx->fDetachOnDisconnect = false;
    }
    else
    {
         pCtx->fDetachOnDisconnect = true;
    }

    LogRel(("LA: DetachOnDisconnect=%RTbool\n", pCtx->fDetachOnDisconnect));

    int rc = VbglR3GuestPropConnect(&pCtx->u32GuestPropHandle);
    if (RT_FAILURE(rc))
        return rc;

    RTListInit(&pCtx->listAttachActions);
    RTListInit(&pCtx->listDetachActions);

    RT_ZERO(pCtx->activeClient);

    *(void **)&pCtx->pfnProcessIdToSessionId = RTLdrGetSystemSymbol("kernel32.dll", "ProcessIdToSessionId");

    *ppvInstance = pCtx;
    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnDestroy}
 */
DECLCALLBACK(void) vbtrLADestroy(void *pvInstance)
{
    AssertPtrReturnVoid(pvInstance);

    LogFlowFunc(("Destroying pvInstance=%p\n", pvInstance));

    PVBOXLACONTEXT pCtx = (PVBOXLACONTEXT)pvInstance;
    AssertPtr(pCtx);

    if (pCtx->u32GuestPropHandle != 0)
    {
        VbglR3GuestPropDisconnect(pCtx->u32GuestPropHandle);
    }

    ActionExecutorDeleteActions(&pCtx->listAttachActions);
    ActionExecutorDeleteActions(&pCtx->listDetachActions);

    pCtx->pfnProcessIdToSessionId = NULL;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnWorker}
 */
DECLCALLBACK(int) vbtrLAWorker(void *pvInstance, bool volatile *pfShutdown)
{
    AssertPtr(pvInstance);
    LogFlowFunc(("pvInstance=%p\n", pvInstance));

    /*
     * Tell the control thread that it can continue
     * spawning services.
     */
    RTThreadUserSignal(RTThreadSelf());

    PVBOXLACONTEXT pCtx = (PVBOXLACONTEXT)pvInstance;

    /*
     * On name change event (/VirtualBox/HostInfo/VRDP/Client/%ID%/Name)
     *   Store the name in the registry (HKCU\Volatile Environment\UTCINFO_CLIENTNAME).
     * On a client attach event (/VirtualBox/HostInfo/VRDP/Client/%ID%/Attach -> 1):
     *   Execute ReconnectActions
     * On a client detach event (/VirtualBox/HostInfo/VRDP/Client/%ID%/Attach -> 0):
     *   Execute DisconnectActions
     *
     * The active connected client id is /VirtualBox/HostInfo/VRDP/ActiveClientClient.
     */

    if (!ActionExecutorEnumerateRegistryKey(g_pwszRegKeyReconnectActions, &pCtx->listAttachActions))
    {
        LogFlowFunc(("Can't enumerate registry key %ls\n", g_pwszRegKeyReconnectActions));
    }
    if (!ActionExecutorEnumerateRegistryKey(g_pwszRegKeyDisconnectActions, &pCtx->listDetachActions))
    {
        LogFlowFunc(("Can't enumerate registry key %ls\n", g_pwszRegKeyDisconnectActions));
    }

    /* A non zero timestamp in the past. */
    pCtx->u64LastQuery = 1;
    /* Start at Detached state. */
    pCtx->u32PrevAction = LA_DO_DETACH;

    int rc;

    for (;;)
    {
        /* Query current ActiveClient.
         * if it differs from the current active client
         *    rebuild the context;
         * wait with timeout for properties change since the active client was changed;
         * if 'Name' was changed
         *    update the name;
         * if 'Attach' was changed
         *    do respective actions.
         * remember the query timestamp;
         */
        uint64_t u64Timestamp = 0;
        uint32_t u32ActiveClientId = 0;
        rc = laGetActiveClient(pCtx, &u64Timestamp, &u32ActiveClientId);

        if (RT_SUCCESS(rc))
        {
            bool fClientIdChanged = pCtx->activeClient.u32ClientId != u32ActiveClientId;

            if (fClientIdChanged)
            {
                rc = laUpdateCurrentState(pCtx, u32ActiveClientId, u64Timestamp);
            }

            if (RT_SUCCESS(rc))
            {
                if (pCtx->activeClient.u32ClientId != 0)
                {
                    rc = laWait(pCtx, &u64Timestamp, 1000);

                    if (RT_SUCCESS(rc))
                    {
                        laProcessAttach(pCtx);

                        laProcessClientInfo(pCtx);

                        laDoActions(pCtx);

                        pCtx->u64LastQuery = u64Timestamp;
                    }
                }
                else
                {
                    /* If the client has been disconnected, do the detach actions. */
                    if (   pCtx->fDetachOnDisconnect
                        && fClientIdChanged)
                    {
                        LogFlowFunc(("Client disconnected\n"));

                        /* laDoActions will prevent a repeated detach action. So if there
                         * was a detach already, then this detach will be ignored.
                         */
                        pCtx->u32Action = LA_DO_DETACH;

                        laDoActions(pCtx);

                        pCtx->u64LastQuery = u64Timestamp;
                    }
                }
            }
        }

        /*
         * Check if it is time to exit.
         * If the code above failed, wait a bit until repeating to avoid a loop.
         * Otherwise just check if the stop event was signalled.
         */
        RTMSINTERVAL msWait;
        if (   rc == VERR_NOT_FOUND
            || pCtx->activeClient.u32ClientId == 0)
        {
            /* No connections, wait longer. */
            msWait = 5000;
            rc = VINF_SUCCESS;
        }
        else if (RT_FAILURE(rc))
        {
            static int s_iBitchedAboutFailedGetActiveClient = 0;
            if (s_iBitchedAboutFailedGetActiveClient++ < 32)
                LogRel(("LA: Retrieving current client(s) failed with %Rrc\n", rc));

            msWait = 10000;
        }
        else
            msWait = 0;

        if (*pfShutdown)
            break;

        if (msWait)
            RTThreadSleep(msWait);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * The service description.
 */
VBOXTRAYSVCDESC g_SvcDescLA =
{
    /* pszName. */
    "LA",
    /* pszDescription. */
    "Location Awareness",
    /* pszUsage. */
    NULL,
    /* pszOptions. */
    NULL,
    /* methods */
    vbtrLAPreInit,
    vbtrLAOption,
    vbtrLAInit,
    vbtrLAWorker,
    NULL /* pfnStop */,
    vbtrLADestroy
};

