/* $Id: vboxmrxnp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Shared Folders - Network provider dll
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
#include <winsvc.h>
#include <winnetwk.h>
#include <npapi.h>
#include <devioctl.h>
#include <stdio.h>

#include "../driver/vbsfshared.h"

#include <iprt/alloc.h>
#include <iprt/initterm.h>
#include <iprt/string.h>
#include <iprt/log.h>
#include <VBox/version.h>
#include <VBox/VBoxGuestLib.h>
#include <VBox/Log.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define MRX_VBOX_SERVER_NAME_U     L"VBOXSVR"
#define MRX_VBOX_SERVER_NAME_ALT_U L"VBOXSRV"

#define WNNC_DRIVER(major, minor) (major * 0x00010000 + minor)


static WCHAR vboxToUpper(WCHAR wc)
{
    /* The CharUpper parameter is a pointer to a null-terminated string,
     * or specifies a single character. If the high-order word of this
     * parameter is zero, the low-order word must contain a single character to be converted.
     */
    return (WCHAR)(uintptr_t)CharUpperW((LPWSTR)(uintptr_t)wc);
}

static DWORD vbsfIOCTL(ULONG IoctlCode,
                       PVOID InputDataBuf,
                       ULONG InputDataLen,
                       PVOID OutputDataBuf,
                       PULONG pOutputDataLen)
{
    ULONG cbOut = 0;

    if (!pOutputDataLen)
    {
        pOutputDataLen = &cbOut;
    }

    ULONG dwStatus = WN_SUCCESS;

    HANDLE DeviceHandle = CreateFile(DD_MRX_VBOX_USERMODE_DEV_NAME_U,
                                     GENERIC_READ | GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     (LPSECURITY_ATTRIBUTES)NULL,
                                     OPEN_EXISTING,
                                     0,
                                     (HANDLE)NULL);

    if (INVALID_HANDLE_VALUE != DeviceHandle)
    {
        BOOL fSuccess = DeviceIoControl(DeviceHandle,
                                        IoctlCode,
                                        InputDataBuf,
                                        InputDataLen,
                                        OutputDataBuf,
                                        *pOutputDataLen,
                                        pOutputDataLen,
                                        NULL);

        if (!fSuccess)
        {
            dwStatus = GetLastError();

            Log(("VBOXNP: vbsfIOCTL: DeviceIoctl last error = %d\n", dwStatus));
        }

        CloseHandle(DeviceHandle);
    }
    else
    {
        dwStatus = GetLastError();

        static int sLogged = 0;
        if (!sLogged)
        {
            LogRel(("VBOXNP: vbsfIOCTL: Error opening device, last error = %d\n",
                    dwStatus));
            sLogged++;
        }
    }

    return dwStatus;
}

DWORD APIENTRY NPGetCaps(DWORD nIndex)
{
    DWORD rc = 0;

    Log(("VBOXNP: GetNetCaps: Index = 0x%x\n", nIndex));

    switch (nIndex)
    {
        case WNNC_SPEC_VERSION:
        {
            rc = WNNC_SPEC_VERSION51;
        } break;

        case WNNC_NET_TYPE:
        {
            rc = WNNC_NET_RDR2SAMPLE;
        } break;

        case WNNC_DRIVER_VERSION:
        {
            rc = WNNC_DRIVER(1, 0);
        } break;

        case WNNC_CONNECTION:
        {
            vbsfIOCTL(IOCTL_MRX_VBOX_START, NULL, 0, NULL, NULL);

            rc = WNNC_CON_GETCONNECTIONS |
                 WNNC_CON_CANCELCONNECTION |
                 WNNC_CON_ADDCONNECTION |
                 WNNC_CON_ADDCONNECTION3;
        } break;

        case WNNC_ENUMERATION:
        {
            rc = WNNC_ENUM_LOCAL |
                 WNNC_ENUM_GLOBAL |
                 WNNC_ENUM_SHAREABLE;
        } break;

        case WNNC_START:
        {
            rc = WNNC_WAIT_FOR_START;
            break;
        }

        case WNNC_DIALOG:
        {
            rc = WNNC_DLG_GETRESOURCEPARENT |
                 WNNC_DLG_GETRESOURCEINFORMATION;
        } break;

        case WNNC_USER:
        case WNNC_ADMIN:
        default:
        {
            rc = 0;
        } break;
    }

    return rc;
}

DWORD APIENTRY NPLogonNotify(PLUID pLogonId,
                             LPCWSTR pAuthentInfoType,
                             LPVOID pAuthentInfo,
                             LPCWSTR pPreviousAuthentInfoType,
                             LPVOID pPreviousAuthentInfo,
                             LPWSTR pStationName,
                             LPVOID StationHandle,
                             LPWSTR *pLogonScript)
{
    RT_NOREF(pLogonId, pAuthentInfoType, pAuthentInfo, pPreviousAuthentInfoType, pPreviousAuthentInfo, pStationName,
             StationHandle, pLogonScript);
    Log(("VBOXNP: NPLogonNotify\n"));
    *pLogonScript = NULL;
    return WN_SUCCESS;
}

DWORD APIENTRY NPPasswordChangeNotify(LPCWSTR pAuthentInfoType,
                                      LPVOID pAuthentInfo,
                                      LPCWSTR pPreviousAuthentInfoType,
                                      LPVOID pPreviousAuthentInfo,
                                      LPWSTR pStationName,
                                      LPVOID StationHandle,
                                      DWORD dwChangeInfo)
{
    RT_NOREF(pAuthentInfoType, pAuthentInfo, pPreviousAuthentInfoType, pPreviousAuthentInfo, pStationName,
             StationHandle, dwChangeInfo);
    Log(("VBOXNP: NPPasswordChangeNotify\n"));

    SetLastError(WN_NOT_SUPPORTED);
    return WN_NOT_SUPPORTED;
}

DWORD APIENTRY NPAddConnection(LPNETRESOURCE pNetResource,
                               LPWSTR pPassword,
                               LPWSTR pUserName)
{
    Log(("VBOXNP: NPAddConnection\n"));
    return NPAddConnection3(NULL, pNetResource, pPassword, pUserName, 0);
}

DWORD APIENTRY NPAddConnection3(HWND hwndOwner,
                                LPNETRESOURCE pNetResource,
                                LPWSTR pPassword,
                                LPWSTR pUserName,
                                DWORD dwFlags)
{
    RT_NOREF(hwndOwner, pPassword, pUserName, dwFlags);
    DWORD dwStatus = WN_SUCCESS;
    WCHAR ConnectionName[256];
    WCHAR LocalName[3];
    BOOLEAN fLocalName = TRUE;

    Log(("VBOXNP: NPAddConnection3: dwFlags = 0x%x\n", dwFlags));
    Log(("VBOXNP: NPAddConnection3: Local Name:  %ls\n", pNetResource->lpLocalName ));
    Log(("VBOXNP: NPAddConnection3: Remote Name: %ls\n", pNetResource->lpRemoteName ));

    if (   pNetResource->dwType != RESOURCETYPE_DISK
        && pNetResource->dwType != RESOURCETYPE_ANY)
    {
        Log(("VBOXNP: NPAddConnection3: Incorrect net resource type %d\n", pNetResource->dwType));
        return WN_BAD_NETNAME;
    }

    /* Build connection name: \Device\VBoxMiniRdr\;%DriveLetter%:\vboxsvr\share */

    lstrcpy(ConnectionName, DD_MRX_VBOX_FS_DEVICE_NAME_U);
    lstrcat(ConnectionName, L"\\;");

    if (pNetResource->lpLocalName == NULL)
    {
        LocalName[0] = L'\0';
        fLocalName = FALSE;
    }
    else
    {
        if (   pNetResource->lpLocalName[0]
            && pNetResource->lpLocalName[1] == L':')
        {
            LocalName[0] = vboxToUpper(pNetResource->lpLocalName[0]);
            LocalName[1] = L':';
            LocalName[2] = L'\0';

            lstrcat(ConnectionName, LocalName);
        }
        else
        {
            dwStatus = WN_BAD_LOCALNAME;
        }
    }


    if (dwStatus == WN_SUCCESS)
    {
        /* Append the remote name. */
        if (   pNetResource->lpRemoteName
            && pNetResource->lpRemoteName[0] == L'\\'
            && pNetResource->lpRemoteName[1] == L'\\' )
        {
            /* No need for (lstrlen + 1), because 'lpNetResource->lpRemoteName' leading \ is not copied. */
            if (lstrlen(ConnectionName) + lstrlen(pNetResource->lpRemoteName) <= sizeof(ConnectionName) / sizeof(WCHAR))
            {
                lstrcat(ConnectionName, &pNetResource->lpRemoteName[1]);
            }
            else
            {
                dwStatus = WN_BAD_NETNAME;
            }
        }
        else
        {
            dwStatus = WN_BAD_NETNAME;
        }
    }

    Log(("VBOXNP: NPAddConnection3: ConnectionName: [%ls], len %d, dwStatus 0x%08X\n",
         ConnectionName, (lstrlen(ConnectionName) + 1) * sizeof(WCHAR), dwStatus));

    if (dwStatus == WN_SUCCESS)
    {
        WCHAR wszTmp[128];

        SetLastError(NO_ERROR);

        if (   fLocalName
            && QueryDosDevice(LocalName, wszTmp, sizeof(wszTmp) / sizeof(WCHAR)))
        {
            Log(("VBOXNP: NPAddConnection3: Connection [%ls] already connected.\n",
                 ConnectionName));
            dwStatus = WN_ALREADY_CONNECTED;
        }
        else
        {
            if (   !fLocalName
                || GetLastError() == ERROR_FILE_NOT_FOUND)
            {
                dwStatus = vbsfIOCTL(IOCTL_MRX_VBOX_ADDCONN,
                                     ConnectionName,
                                     (lstrlen(ConnectionName) + 1) * sizeof(WCHAR),
                                     NULL,
                                     NULL);

                if (dwStatus == WN_SUCCESS)
                {
                    if (   fLocalName
                        && !DefineDosDevice(DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM,
                                            pNetResource->lpLocalName,
                                            ConnectionName))
                    {
                        dwStatus = GetLastError();
                    }
                }
                else
                {
                    dwStatus = WN_BAD_NETNAME;
                }
            }
            else
            {
                dwStatus = WN_ALREADY_CONNECTED;
            }
        }
    }

    Log(("VBOXNP: NPAddConnection3: Returned 0x%08X\n",
         dwStatus));
    return dwStatus;
}

DWORD APIENTRY NPCancelConnection(LPWSTR pName,
                                  BOOL fForce)
{
    RT_NOREF(fForce);
    DWORD dwStatus = WN_NOT_CONNECTED;

    Log(("VBOXNP: NPCancelConnection: Name = %ls\n",
         pName));

    if (pName && pName[0] != 0)
    {
        WCHAR ConnectionName[256];

        if (pName[1] == L':')
        {
            WCHAR RemoteName[128];
            WCHAR LocalName[3];

            LocalName[0] = vboxToUpper(pName[0]);
            LocalName[1] = L':';
            LocalName[2] = L'\0';

            ULONG cbOut = sizeof(RemoteName) - sizeof(WCHAR); /* Trailing NULL. */

            dwStatus = vbsfIOCTL(IOCTL_MRX_VBOX_GETCONN,
                                 LocalName,
                                 sizeof(LocalName),
                                 (PVOID)RemoteName,
                                 &cbOut);

            if (   dwStatus == WN_SUCCESS
                && cbOut > 0)
            {
                RemoteName[cbOut / sizeof(WCHAR)] = L'\0';

                if (lstrlen(DD_MRX_VBOX_FS_DEVICE_NAME_U) + 2 + lstrlen(LocalName) + lstrlen(RemoteName) + 1 > sizeof(ConnectionName) / sizeof(WCHAR))
                {
                    dwStatus = WN_BAD_NETNAME;
                }
                else
                {
                    lstrcpy(ConnectionName, DD_MRX_VBOX_FS_DEVICE_NAME_U);
                    lstrcat(ConnectionName, L"\\;");
                    lstrcat(ConnectionName, LocalName);
                    lstrcat(ConnectionName, RemoteName);

                    dwStatus = vbsfIOCTL(IOCTL_MRX_VBOX_DELCONN,
                                         ConnectionName,
                                         (lstrlen(ConnectionName) + 1) * sizeof(WCHAR),
                                         NULL,
                                         NULL);

                    if (dwStatus == WN_SUCCESS)
                    {
                        if (!DefineDosDevice(DDD_REMOVE_DEFINITION | DDD_RAW_TARGET_PATH | DDD_EXACT_MATCH_ON_REMOVE,
                                             LocalName,
                                             ConnectionName))
                        {
                            dwStatus = GetLastError();
                        }
                    }
                }
            }
            else
            {
                dwStatus = WN_NOT_CONNECTED;
            }
        }
        else
        {
            BOOLEAN Verifier;

            Verifier  = ( pName[0] == L'\\' );
            Verifier &= ( pName[1] == L'V' ) || ( pName[1] == L'v' );
            Verifier &= ( pName[2] == L'B' ) || ( pName[2] == L'b' );
            Verifier &= ( pName[3] == L'O' ) || ( pName[3] == L'o' );
            Verifier &= ( pName[4] == L'X' ) || ( pName[4] == L'x' );
            Verifier &= ( pName[5] == L'S' ) || ( pName[5] == L's' );
            /* Both vboxsvr & vboxsrv are now accepted */
            if (( pName[6] == L'V' ) || ( pName[6] == L'v'))
            {
                Verifier &= ( pName[6] == L'V' ) || ( pName[6] == L'v' );
                Verifier &= ( pName[7] == L'R' ) || ( pName[7] == L'r' );
            }
            else
            {
                Verifier &= ( pName[6] == L'R' ) || ( pName[6] == L'r' );
                Verifier &= ( pName[7] == L'V' ) || ( pName[7] == L'v' );
            }
            Verifier &= ( pName[8] == L'\\') || ( pName[8] == 0 );

            if (Verifier)
            {
                /* Full remote path */
                if (lstrlen(DD_MRX_VBOX_FS_DEVICE_NAME_U) + 2 + lstrlen(pName) + 1 > sizeof(ConnectionName) / sizeof(WCHAR))
                {
                    dwStatus = WN_BAD_NETNAME;
                }
                else
                {
                    lstrcpy(ConnectionName, DD_MRX_VBOX_FS_DEVICE_NAME_U);
                    lstrcat(ConnectionName, L"\\;");
                    lstrcat(ConnectionName, pName);

                    dwStatus = vbsfIOCTL(IOCTL_MRX_VBOX_DELCONN,
                                         ConnectionName,
                                         (lstrlen(ConnectionName) + 1) * sizeof(WCHAR),
                                         NULL,
                                         NULL);
                }
            }
            else
            {
                dwStatus = WN_NOT_CONNECTED;
            }
        }
    }

    Log(("VBOXNP: NPCancelConnection: Returned 0x%08X\n",
         dwStatus));
    return dwStatus;
}

DWORD APIENTRY NPGetConnection(LPWSTR pLocalName,
                               LPWSTR pRemoteName,
                               LPDWORD pBufferSize)
{
    DWORD dwStatus = WN_NOT_CONNECTED;

    WCHAR RemoteName[128];
    ULONG cbOut = 0;

    Log(("VBOXNP: NPGetConnection: pLocalName = %ls\n",
         pLocalName));

    if (pLocalName && pLocalName[0] != 0)
    {
        if (pLocalName[1] == L':')
        {
            WCHAR LocalName[3];

            cbOut = sizeof(RemoteName) - sizeof(WCHAR);
            RemoteName[cbOut / sizeof(WCHAR)] = 0;

            LocalName[0] = vboxToUpper(pLocalName[0]);
            LocalName[1] = L':';
            LocalName[2] = L'\0';

            dwStatus = vbsfIOCTL(IOCTL_MRX_VBOX_GETCONN,
                                 LocalName,
                                 sizeof(LocalName),
                                 (PVOID)RemoteName,
                                 &cbOut);

            if (dwStatus != NO_ERROR)
            {
                /* The device specified by pLocalName is not redirected by this provider. */
                dwStatus = WN_NOT_CONNECTED;
            }
            else
            {
                RemoteName[cbOut / sizeof(WCHAR)] = 0;

                if (cbOut == 0)
                {
                    dwStatus = WN_NO_NETWORK;
                }
            }
        }
    }

    if (dwStatus == WN_SUCCESS)
    {
        ULONG cbRemoteName = (lstrlen(RemoteName) + 1) * sizeof (WCHAR); /* Including the trailing 0. */

        Log(("VBOXNP: NPGetConnection: RemoteName: %ls, cb %d\n",
             RemoteName, cbRemoteName));

        DWORD len = sizeof(WCHAR) + cbRemoteName; /* Including the leading '\'. */

        if (*pBufferSize >= len)
        {
            pRemoteName[0] = L'\\';
            CopyMemory(&pRemoteName[1], RemoteName, cbRemoteName);

            Log(("VBOXNP: NPGetConnection: returning pRemoteName: %ls\n",
                 pRemoteName));
        }
        else
        {
            if (*pBufferSize != 0)
            {
                /* Log only real errors. Do not log a 0 bytes try. */
                Log(("VBOXNP: NPGetConnection: Buffer overflow: *pBufferSize = %d, len = %d\n",
                     *pBufferSize, len));
            }

            dwStatus = WN_MORE_DATA;
        }

        *pBufferSize = len;
    }

    if ((dwStatus != WN_SUCCESS) &&
        (dwStatus != WN_MORE_DATA))
    {
        Log(("VBOXNP: NPGetConnection: Returned error 0x%08X\n",
             dwStatus));
    }

    return dwStatus;
}

static const WCHAR *vboxSkipServerPrefix(const WCHAR *pRemoteName, const WCHAR *pPrefix)
{
    while (*pPrefix)
    {
        if (vboxToUpper(*pPrefix) != vboxToUpper(*pRemoteName))
        {
            /* Not a prefix */
            return NULL;
        }

        pPrefix++;
        pRemoteName++;
    }

    return pRemoteName;
}

static const WCHAR *vboxSkipServerName(const WCHAR *pRemoteName)
{
    int cLeadingBackslashes = 0;
    while (*pRemoteName == L'\\')
    {
        pRemoteName++;
        cLeadingBackslashes++;
    }

    if (cLeadingBackslashes == 0 || cLeadingBackslashes == 2)
    {
        const WCHAR *pAfterPrefix = vboxSkipServerPrefix(pRemoteName, MRX_VBOX_SERVER_NAME_U);

        if (!pAfterPrefix)
        {
            pAfterPrefix = vboxSkipServerPrefix(pRemoteName, MRX_VBOX_SERVER_NAME_ALT_U);
        }

        return pAfterPrefix;
    }

    return NULL;
}

/* Enumerate shared folders as hierarchy:
 * VBOXSVR(container)
 * +--------------------+
 * |                     \
 * Folder1(connectable)  FolderN(connectable)
 */
typedef struct _NPENUMCTX
{
    ULONG index; /* Index of last entry returned. */
    DWORD dwScope;
    DWORD dwOriginalScope;
    DWORD dwType;
    DWORD dwUsage;
    bool fRoot;
} NPENUMCTX;

DWORD APIENTRY NPOpenEnum(DWORD dwScope,
                          DWORD dwType,
                          DWORD dwUsage,
                          LPNETRESOURCE pNetResource,
                          LPHANDLE lphEnum)
{
    DWORD dwStatus;

    Log(("VBOXNP: NPOpenEnum: dwScope 0x%08X, dwType 0x%08X, dwUsage 0x%08X, pNetResource %p\n",
         dwScope, dwType, dwUsage, pNetResource));

    if (dwUsage == 0)
    {
        /* The bitmask may be zero to match all of the flags. */
        dwUsage = RESOURCEUSAGE_CONNECTABLE | RESOURCEUSAGE_CONTAINER;
    }

    *lphEnum = NULL;

    /* Allocate the context structure. */
    NPENUMCTX *pCtx = (NPENUMCTX *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(NPENUMCTX));

    if (pCtx == NULL)
    {
        dwStatus = WN_OUT_OF_MEMORY;
    }
    else
    {
        if (pNetResource && pNetResource->lpRemoteName)
        {
            Log(("VBOXNP: NPOpenEnum: pRemoteName %ls\n",
                 pNetResource->lpRemoteName));
        }

        switch (dwScope)
        {
            case 6: /* Advertised as WNNC_ENUM_SHAREABLE. This returns C$ system shares.
                     * NpEnumResource will return NO_MORE_ENTRIES.
                     */
            {
                if (pNetResource == NULL || pNetResource->lpRemoteName == NULL)
                {
                    /* If it is NULL or if the pRemoteName field of the NETRESOURCE is NULL,
                     * the provider should enumerate the top level of its network.
                     * But system shares can't be on top level.
                     */
                    dwStatus = WN_NOT_CONTAINER;
                    break;
                }

                const WCHAR *pAfterName = vboxSkipServerName(pNetResource->lpRemoteName);
                if (   pAfterName == NULL
                    || (*pAfterName != L'\\' && *pAfterName != 0))
                {
                    dwStatus = WN_NOT_CONTAINER;
                    break;
                }

                /* Valid server name. */

                pCtx->index = 0;
                pCtx->dwScope = 6;
                pCtx->dwOriginalScope = dwScope;
                pCtx->dwType = dwType;
                pCtx->dwUsage = dwUsage;

                dwStatus = WN_SUCCESS;
                break;
            }
            case RESOURCE_GLOBALNET: /* All resources on the network. */
            {
                if (pNetResource == NULL || pNetResource->lpRemoteName == NULL)
                {
                    /* If it is NULL or if the pRemoteName field of the NETRESOURCE is NULL,
                     * the provider should enumerate the top level of its network.
                     */
                    pCtx->fRoot = true;
                }
                else
                {
                    /* Enumerate pNetResource->lpRemoteName container, which can be only the VBOXSVR container. */
                    const WCHAR *pAfterName = vboxSkipServerName(pNetResource->lpRemoteName);
                    if (   pAfterName == NULL
                        || (*pAfterName != L'\\' && *pAfterName != 0))
                    {
                        dwStatus = WN_NOT_CONTAINER;
                        break;
                    }

                    /* Valid server name. */
                    pCtx->fRoot = false;
                }

                pCtx->index = 0;
                pCtx->dwScope = RESOURCE_GLOBALNET;
                pCtx->dwOriginalScope = dwScope;
                pCtx->dwType = dwType;
                pCtx->dwUsage = dwUsage;

                dwStatus = WN_SUCCESS;
                break;
            }

            case RESOURCE_CONNECTED: /* All currently connected resources. */
            case RESOURCE_CONTEXT: /* The interpretation of this is left to the provider. Treat this as RESOURCE_GLOBALNET. */
            {
                pCtx->index = 0;
                pCtx->dwScope = RESOURCE_CONNECTED;
                pCtx->dwOriginalScope = dwScope;
                pCtx->dwType = dwType;
                pCtx->dwUsage = dwUsage;
                pCtx->fRoot = false; /* Actually ignored for RESOURCE_CONNECTED. */

                dwStatus = WN_SUCCESS;
                break;
            }

            default:
                Log(("VBOXNP: NPOpenEnum: unsupported scope 0x%lx\n",
                     dwScope));
                dwStatus = WN_NOT_SUPPORTED;
                break;
        }
    }

    if (dwStatus != WN_SUCCESS)
    {
        Log(("VBOXNP: NPOpenEnum: Returned error 0x%08X\n",
             dwStatus));
        if (pCtx)
        {
            HeapFree(GetProcessHeap(), 0, pCtx);
        }
    }
    else
    {
        Log(("VBOXNP: NPOpenEnum: pCtx %p\n",
             pCtx));
        *lphEnum = pCtx;
    }

    return dwStatus;
}

DWORD APIENTRY NPEnumResource(HANDLE hEnum,
                              LPDWORD lpcCount,
                              LPVOID pBuffer,
                              LPDWORD pBufferSize)
{
    DWORD dwStatus = WN_SUCCESS;
    NPENUMCTX *pCtx = (NPENUMCTX *)hEnum;

    BYTE ConnectionList[26];
    ULONG cbOut;
    WCHAR LocalName[3];
    WCHAR RemoteName[128];
    int cbRemoteName;

    ULONG cbEntry = 0;

    Log(("VBOXNP: NPEnumResource: hEnum %p, lpcCount %p, pBuffer %p, pBufferSize %p.\n",
         hEnum, lpcCount, pBuffer, pBufferSize));

    if (pCtx == NULL)
    {
        Log(("VBOXNP: NPEnumResource: WN_BAD_HANDLE\n"));
        return WN_BAD_HANDLE;
    }

    if (lpcCount == NULL || pBuffer == NULL)
    {
        Log(("VBOXNP: NPEnumResource: WN_BAD_VALUE\n"));
        return WN_BAD_VALUE;
    }

    Log(("VBOXNP: NPEnumResource: *lpcCount 0x%x, *pBufferSize 0x%x, pCtx->index %d\n",
         *lpcCount, *pBufferSize, pCtx->index));

    LPNETRESOURCE pNetResource = (LPNETRESOURCE)pBuffer;
    ULONG cbRemaining = *pBufferSize;
    ULONG cEntriesCopied = 0;
    PWCHAR pStrings = (PWCHAR)((PBYTE)pBuffer + *pBufferSize);
    PWCHAR pDst;

    if (pCtx->dwScope == RESOURCE_CONNECTED)
    {
        Log(("VBOXNP: NPEnumResource: RESOURCE_CONNECTED\n"));

        memset(ConnectionList, 0, sizeof(ConnectionList));
        cbOut = sizeof(ConnectionList);

        dwStatus = vbsfIOCTL(IOCTL_MRX_VBOX_GETLIST,
                             NULL, 0,
                             ConnectionList,
                             &cbOut);

        if (dwStatus == WN_SUCCESS && cbOut > 0)
        {
            while (cEntriesCopied < *lpcCount && pCtx->index < RTL_NUMBER_OF(ConnectionList))
            {
                if (ConnectionList[pCtx->index])
                {
                    LocalName[0] = L'A' + (WCHAR)pCtx->index;
                    LocalName[1] = L':';
                    LocalName[2] = L'\0';
                    memset(RemoteName, 0, sizeof(RemoteName));
                    cbOut = sizeof(RemoteName);

                    dwStatus = vbsfIOCTL(IOCTL_MRX_VBOX_GETCONN,
                                         LocalName,
                                         sizeof(LocalName),
                                         RemoteName,
                                         &cbOut);

                    if (dwStatus != WN_SUCCESS || cbOut == 0)
                    {
                        dwStatus = WN_NO_MORE_ENTRIES;
                        break;
                    }

                    /* How many bytes is needed for the current NETRESOURCE data. */
                    cbRemoteName = (lstrlen(RemoteName) + 1) * sizeof(WCHAR);
                    cbEntry = sizeof(NETRESOURCE);
                    cbEntry += sizeof(LocalName);
                    cbEntry += sizeof(WCHAR) + cbRemoteName; /* Leading \. */
                    cbEntry += sizeof(MRX_VBOX_PROVIDER_NAME_U);

                    if (cbEntry > cbRemaining)
                    {
                        break;
                    }

                    cbRemaining -= cbEntry;

                    memset(pNetResource, 0, sizeof (*pNetResource));

                    pNetResource->dwScope = RESOURCE_CONNECTED;
                    pNetResource->dwType = RESOURCETYPE_DISK;
                    pNetResource->dwDisplayType = RESOURCEDISPLAYTYPE_SHARE;
                    pNetResource->dwUsage = RESOURCEUSAGE_CONNECTABLE;

                    /* Reserve the space in the string area. */
                    pStrings = (PWCHAR)((PBYTE)pStrings - (cbEntry - sizeof(NETRESOURCE)));
                    pDst = pStrings;

                    pNetResource->lpLocalName = pDst;
                    *pDst++ = L'A' + (WCHAR)pCtx->index;
                    *pDst++ = L':';
                    *pDst++ = L'\0';

                    pNetResource->lpRemoteName = pDst;
                    *pDst++ = L'\\';
                    CopyMemory(pDst, RemoteName, cbRemoteName);
                    pDst += cbRemoteName / sizeof(WCHAR);

                    pNetResource->lpComment = NULL;

                    pNetResource->lpProvider = pDst;
                    CopyMemory(pDst, MRX_VBOX_PROVIDER_NAME_U, sizeof(MRX_VBOX_PROVIDER_NAME_U));

                    Log(("VBOXNP: NPEnumResource: pRemoteName: %ls\n",
                         pNetResource->lpRemoteName));

                    cEntriesCopied++;
                    pNetResource++;
                }

                pCtx->index++;
            }
        }
        else
        {
            dwStatus = WN_NO_MORE_ENTRIES;
        }
    }
    else if (pCtx->dwScope == RESOURCE_GLOBALNET)
    {
        Log(("VBOXNP: NPEnumResource: RESOURCE_GLOBALNET: root %d\n", pCtx->fRoot));

        if (pCtx->fRoot)
        {
            /* VBOXSVR container. */
            if (pCtx->index > 0)
            {
                dwStatus = WN_NO_MORE_ENTRIES;
            }
            else
            {
                /* Return VBOXSVR server.
                 * Determine the space needed for this entry.
                 */
                cbEntry = sizeof(NETRESOURCE);
                cbEntry += 2 * sizeof(WCHAR) + sizeof(MRX_VBOX_SERVER_NAME_U); /* \\ + the server name */
                cbEntry += sizeof(MRX_VBOX_PROVIDER_NAME_U);

                if (cbEntry > cbRemaining)
                {
                    /* Do nothing. */
                }
                else
                {
                    cbRemaining -= cbEntry;

                    memset(pNetResource, 0, sizeof (*pNetResource));

                    pNetResource->dwScope = RESOURCE_GLOBALNET;
                    pNetResource->dwType = RESOURCETYPE_ANY;
                    pNetResource->dwDisplayType = RESOURCEDISPLAYTYPE_SERVER;
                    pNetResource->dwUsage = RESOURCEUSAGE_CONTAINER;

                    pStrings = (PWCHAR)((PBYTE)pStrings - (cbEntry - sizeof(NETRESOURCE)));
                    pDst = pStrings;

                    pNetResource->lpLocalName = NULL;

                    pNetResource->lpRemoteName = pDst;
                    *pDst++ = L'\\';
                    *pDst++ = L'\\';
                    CopyMemory(pDst, MRX_VBOX_SERVER_NAME_U, sizeof(MRX_VBOX_SERVER_NAME_U));
                    pDst += sizeof(MRX_VBOX_SERVER_NAME_U) / sizeof(WCHAR);

                    pNetResource->lpComment = NULL;

                    pNetResource->lpProvider = pDst;
                    CopyMemory(pDst, MRX_VBOX_PROVIDER_NAME_U, sizeof(MRX_VBOX_PROVIDER_NAME_U));

                    cEntriesCopied++;

                    pCtx->index++;
                }
            }
        }
        else
        {
            /* Shares of VBOXSVR. */
            memset(ConnectionList, 0, sizeof (ConnectionList));
            cbOut = sizeof(ConnectionList);

            dwStatus = vbsfIOCTL(IOCTL_MRX_VBOX_GETGLOBALLIST,
                                 NULL,
                                 0,
                                 ConnectionList,
                                 &cbOut);

            if (dwStatus == WN_SUCCESS && cbOut > 0)
            {
                while (cEntriesCopied < *lpcCount && pCtx->index < RTL_NUMBER_OF(ConnectionList))
                {
                    if (ConnectionList[pCtx->index])
                    {
                        memset(RemoteName, 0, sizeof(RemoteName));
                        cbOut = sizeof(RemoteName);

                        dwStatus = vbsfIOCTL(IOCTL_MRX_VBOX_GETGLOBALCONN,
                                             &ConnectionList[pCtx->index],
                                             sizeof(ConnectionList[pCtx->index]),
                                             RemoteName,
                                             &cbOut);

                        if (dwStatus != WN_SUCCESS || cbOut == 0)
                        {
                            dwStatus = WN_NO_MORE_ENTRIES;
                            break;
                        }

                        /* How many bytes is needed for the current NETRESOURCE data. */
                        cbRemoteName = (lstrlen(RemoteName) + 1) * sizeof(WCHAR);
                        cbEntry = sizeof(NETRESOURCE);
                        /* Remote name: \\ + vboxsvr + \ + name. */
                        cbEntry += 2 * sizeof(WCHAR) + sizeof(MRX_VBOX_SERVER_NAME_U) + cbRemoteName;
                        cbEntry += sizeof(MRX_VBOX_PROVIDER_NAME_U);

                        if (cbEntry > cbRemaining)
                        {
                            break;
                        }

                        cbRemaining -= cbEntry;

                        memset(pNetResource, 0, sizeof (*pNetResource));

                        pNetResource->dwScope = pCtx->dwOriginalScope;
                        pNetResource->dwType = RESOURCETYPE_DISK;
                        pNetResource->dwDisplayType = RESOURCEDISPLAYTYPE_SHARE;
                        pNetResource->dwUsage = RESOURCEUSAGE_CONNECTABLE;

                        pStrings = (PWCHAR)((PBYTE)pStrings - (cbEntry - sizeof(NETRESOURCE)));
                        pDst = pStrings;

                        pNetResource->lpLocalName = NULL;

                        pNetResource->lpRemoteName = pDst;
                        *pDst++ = L'\\';
                        *pDst++ = L'\\';
                        CopyMemory(pDst, MRX_VBOX_SERVER_NAME_U, sizeof(MRX_VBOX_SERVER_NAME_U) - sizeof(WCHAR));
                        pDst += sizeof(MRX_VBOX_SERVER_NAME_U) / sizeof(WCHAR) - 1;
                        *pDst++ = L'\\';
                        CopyMemory(pDst, RemoteName, cbRemoteName);
                        pDst += cbRemoteName / sizeof(WCHAR);

                        pNetResource->lpComment = NULL;

                        pNetResource->lpProvider = pDst;
                        CopyMemory(pDst, MRX_VBOX_PROVIDER_NAME_U, sizeof(MRX_VBOX_PROVIDER_NAME_U));

                        Log(("VBOXNP: NPEnumResource: pRemoteName: %ls\n",
                             pNetResource->lpRemoteName));

                        cEntriesCopied++;
                        pNetResource++;
                    }

                    pCtx->index++;
                }
            }
            else
            {
                dwStatus = WN_NO_MORE_ENTRIES;
            }
        }
    }
    else if (pCtx->dwScope == 6)
    {
        Log(("VBOXNP: NPEnumResource: dwScope 6\n"));
        dwStatus = WN_NO_MORE_ENTRIES;
    }
    else
    {
        Log(("VBOXNP: NPEnumResource: invalid dwScope 0x%x\n",
             pCtx->dwScope));
        return WN_BAD_HANDLE;
    }

    *lpcCount = cEntriesCopied;

    if (cEntriesCopied == 0 && dwStatus == WN_SUCCESS)
    {
        if (pCtx->index >= RTL_NUMBER_OF(ConnectionList))
        {
            dwStatus = WN_NO_MORE_ENTRIES;
        }
        else
        {
            Log(("VBOXNP: NPEnumResource: More Data Needed - %d\n",
                 cbEntry));
            *pBufferSize = cbEntry;
            dwStatus = WN_MORE_DATA;
        }
    }

    Log(("VBOXNP: NPEnumResource: Entries returned %d, dwStatus 0x%08X\n",
         cEntriesCopied, dwStatus));
    return dwStatus;
}

DWORD APIENTRY NPCloseEnum(HANDLE hEnum)
{
    NPENUMCTX *pCtx = (NPENUMCTX *)hEnum;

    Log(("VBOXNP: NPCloseEnum: hEnum %p\n",
         hEnum));

    if (pCtx)
    {
        HeapFree(GetProcessHeap(), 0, pCtx);
    }

    Log(("VBOXNP: NPCloseEnum: returns\n"));
    return WN_SUCCESS;
}

DWORD APIENTRY NPGetResourceParent(LPNETRESOURCE pNetResource,
                                   LPVOID pBuffer,
                                   LPDWORD pBufferSize)
{
    Log(("VBOXNP: NPGetResourceParent: pNetResource %p, pBuffer %p, pBufferSize %p\n",
         pNetResource, pBuffer, pBufferSize));

    /* Construct a new NETRESOURCE which is syntactically a parent of pNetResource,
     * then call NPGetResourceInformation to actually fill the buffer.
     */
    if (!pNetResource || !pNetResource->lpRemoteName || !pBufferSize)
    {
        return WN_BAD_NETNAME;
    }

    const WCHAR *pAfterName = vboxSkipServerName(pNetResource->lpRemoteName);
    if (   pAfterName == NULL
        || (*pAfterName != L'\\' && *pAfterName != 0))
    {
        Log(("VBOXNP: NPGetResourceParent: WN_BAD_NETNAME\n"));
        return WN_BAD_NETNAME;
    }

    DWORD RemoteNameLength = lstrlen(pNetResource->lpRemoteName);

    DWORD cbEntry = sizeof (NETRESOURCE);
    cbEntry += (RemoteNameLength + 1) * sizeof (WCHAR);

    NETRESOURCE *pParent = (NETRESOURCE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cbEntry);

    if (!pParent)
    {
        return WN_OUT_OF_MEMORY;
    }

    pParent->lpRemoteName = (WCHAR *)((PBYTE)pParent + sizeof (NETRESOURCE));
    lstrcpy(pParent->lpRemoteName, pNetResource->lpRemoteName);

    /* Remove last path component of the pParent->lpRemoteName. */
    WCHAR *pLastSlash = pParent->lpRemoteName + RemoteNameLength;
    if (*pLastSlash == L'\\')
    {
        /* \\server\share\path\, skip last slash immediately. */
        pLastSlash--;
    }

    while (pLastSlash != pParent->lpRemoteName)
    {
        if (*pLastSlash == L'\\')
        {
            break;
        }

        pLastSlash--;
    }

    DWORD dwStatus = WN_SUCCESS;

    if (   pLastSlash == pParent->lpRemoteName
        || pLastSlash == pParent->lpRemoteName + 1)
    {
        /* It is a leading backslash. Construct "no parent" NETRESOURCE. */
        NETRESOURCE *pNetResourceNP = (NETRESOURCE *)pBuffer;

        cbEntry = sizeof(NETRESOURCE);
        cbEntry += sizeof(MRX_VBOX_PROVIDER_NAME_U); /* remote name */
        cbEntry += sizeof(MRX_VBOX_PROVIDER_NAME_U); /* provider name */

        if (cbEntry > *pBufferSize)
        {
            Log(("VBOXNP: NPGetResourceParent: WN_MORE_DATA 0x%x\n", cbEntry));
            *pBufferSize = cbEntry;
            dwStatus = WN_MORE_DATA;
        }
        else
        {
            memset (pNetResourceNP, 0, sizeof (*pNetResourceNP));

            pNetResourceNP->dwType = RESOURCETYPE_ANY;
            pNetResourceNP->dwDisplayType = RESOURCEDISPLAYTYPE_NETWORK;
            pNetResourceNP->dwUsage = RESOURCEUSAGE_CONTAINER;

            WCHAR *pStrings = (WCHAR *)((PBYTE)pBuffer + *pBufferSize);
            pStrings = (PWCHAR)((PBYTE)pStrings - (cbEntry - sizeof(NETRESOURCE)));

            pNetResourceNP->lpRemoteName = pStrings;
            CopyMemory (pStrings, MRX_VBOX_PROVIDER_NAME_U, sizeof(MRX_VBOX_PROVIDER_NAME_U));
            pStrings += sizeof(MRX_VBOX_PROVIDER_NAME_U) / sizeof(WCHAR);

            pNetResourceNP->lpProvider = pStrings;
            CopyMemory (pStrings, MRX_VBOX_PROVIDER_NAME_U, sizeof(MRX_VBOX_PROVIDER_NAME_U));
            pStrings += sizeof(MRX_VBOX_PROVIDER_NAME_U) / sizeof(WCHAR);

            Log(("VBOXNP: NPGetResourceParent: no parent, strings %p/%p\n",
                 pStrings, (PBYTE)pBuffer + *pBufferSize));
        }
    }
    else
    {
        /* Make the parent remote name and get its information. */
        *pLastSlash = 0;

        LPWSTR pSystem = NULL;
        dwStatus = NPGetResourceInformation (pParent, pBuffer, pBufferSize, &pSystem);
    }

    if (pParent)
    {
        HeapFree(GetProcessHeap(), 0, pParent);
    }

    return dwStatus;
}

DWORD APIENTRY NPGetResourceInformation(LPNETRESOURCE pNetResource,
                                        LPVOID pBuffer,
                                        LPDWORD pBufferSize,
                                        LPWSTR *lplpSystem)
{
    Log(("VBOXNP: NPGetResourceInformation: pNetResource %p, pBuffer %p, pBufferSize %p, lplpSystem %p\n",
         pNetResource, pBuffer, pBufferSize, lplpSystem));

    if (   pNetResource == NULL
        || pNetResource->lpRemoteName == NULL
        || pBufferSize == NULL)
    {
        Log(("VBOXNP: NPGetResourceInformation: WN_BAD_VALUE\n"));
        return WN_BAD_VALUE;
    }

    Log(("VBOXNP: NPGetResourceInformation: pRemoteName %ls, *pBufferSize 0x%x\n",
         pNetResource->lpRemoteName, *pBufferSize));

    const WCHAR *pAfterName = vboxSkipServerName(pNetResource->lpRemoteName);
    if (   pAfterName == NULL
        || (*pAfterName != L'\\' && *pAfterName != 0))
    {
        Log(("VBOXNP: NPGetResourceInformation: WN_BAD_NETNAME\n"));
        return WN_BAD_NETNAME;
    }

    if (pNetResource->dwType != 0 && pNetResource->dwType != RESOURCETYPE_DISK)
    {
        /* The caller passed in a nonzero dwType that does not match
         * the actual type of the network resource.
         */
        return WN_BAD_DEV_TYPE;
    }

    /*
     * If the input remote resource name was "\\server\share\dir1\dir2",
     * then the output NETRESOURCE contains information about the resource "\\server\share".
     * The pRemoteName, pProvider, dwType, dwDisplayType, and dwUsage fields are returned
     * containing values, all other fields being set to NULL.
     */
    DWORD cbEntry;
    WCHAR *pStrings = (WCHAR *)((PBYTE)pBuffer + *pBufferSize);
    NETRESOURCE *pNetResourceInfo = (NETRESOURCE *)pBuffer;

    /* Check what kind of the resource is that by parsing path components.
     * pAfterName points to first WCHAR after a valid server name.
     */

    if (pAfterName[0] == 0 || pAfterName[1] == 0)
    {
        /* "\\VBOXSVR" or "\\VBOXSVR\" */
        cbEntry = sizeof(NETRESOURCE);
        cbEntry += 2 * sizeof(WCHAR) + sizeof(MRX_VBOX_SERVER_NAME_U); /* \\ + server name */
        cbEntry += sizeof(MRX_VBOX_PROVIDER_NAME_U); /* provider name */

        if (cbEntry > *pBufferSize)
        {
            Log(("VBOXNP: NPGetResourceInformation: WN_MORE_DATA 0x%x\n", cbEntry));
            *pBufferSize = cbEntry;
            return WN_MORE_DATA;
        }

        memset(pNetResourceInfo, 0, sizeof (*pNetResourceInfo));

        pNetResourceInfo->dwType = RESOURCETYPE_ANY;
        pNetResourceInfo->dwDisplayType = RESOURCEDISPLAYTYPE_SERVER;
        pNetResourceInfo->dwUsage = RESOURCEUSAGE_CONTAINER;

        pStrings = (PWCHAR)((PBYTE)pStrings - (cbEntry - sizeof(NETRESOURCE)));

        pNetResourceInfo->lpRemoteName = pStrings;
        *pStrings++ = L'\\';
        *pStrings++ = L'\\';
        CopyMemory (pStrings, MRX_VBOX_SERVER_NAME_U, sizeof(MRX_VBOX_SERVER_NAME_U));
        pStrings += sizeof(MRX_VBOX_SERVER_NAME_U) / sizeof(WCHAR);

        pNetResourceInfo->lpProvider = pStrings;
        CopyMemory (pStrings, MRX_VBOX_PROVIDER_NAME_U, sizeof(MRX_VBOX_PROVIDER_NAME_U));
        pStrings += sizeof(MRX_VBOX_PROVIDER_NAME_U) / sizeof(WCHAR);

        Log(("VBOXNP: NPGetResourceInformation: pRemoteName: %ls, strings %p/%p\n",
             pNetResourceInfo->lpRemoteName, pStrings, (PBYTE)pBuffer + *pBufferSize));

        if (lplpSystem)
        {
            *lplpSystem = NULL;
        }

        return WN_SUCCESS;
    }

    /* *pAfterName == L'\\', could be share or share + path.
     * Check if there are more path components after the share name.
     */
    const WCHAR *lp = pAfterName + 1;
    while (*lp && *lp != L'\\')
    {
        lp++;
    }

    if (*lp == 0)
    {
        /* It is a share only: \\vboxsvr\share */
        cbEntry = sizeof(NETRESOURCE);
        cbEntry += 2 * sizeof(WCHAR) + sizeof(MRX_VBOX_SERVER_NAME_U); /* \\ + server name with trailing nul */
        cbEntry += (DWORD)((lp - pAfterName) * sizeof(WCHAR)); /* The share name with leading \\ */
        cbEntry += sizeof(MRX_VBOX_PROVIDER_NAME_U); /* provider name */

        if (cbEntry > *pBufferSize)
        {
            Log(("VBOXNP: NPGetResourceInformation: WN_MORE_DATA 0x%x\n", cbEntry));
            *pBufferSize = cbEntry;
            return WN_MORE_DATA;
        }

        memset(pNetResourceInfo, 0, sizeof (*pNetResourceInfo));

        pNetResourceInfo->dwType = RESOURCETYPE_DISK;
        pNetResourceInfo->dwDisplayType = RESOURCEDISPLAYTYPE_SHARE;
        pNetResourceInfo->dwUsage = RESOURCEUSAGE_CONNECTABLE;

        pStrings = (PWCHAR)((PBYTE)pStrings - (cbEntry - sizeof(NETRESOURCE)));

        pNetResourceInfo->lpRemoteName = pStrings;
        *pStrings++ = L'\\';
        *pStrings++ = L'\\';
        CopyMemory(pStrings, MRX_VBOX_SERVER_NAME_U, sizeof(MRX_VBOX_SERVER_NAME_U) - sizeof (WCHAR));
        pStrings += sizeof(MRX_VBOX_SERVER_NAME_U) / sizeof(WCHAR) - 1;
        CopyMemory (pStrings, pAfterName, (lp - pAfterName + 1) * sizeof(WCHAR));
        pStrings += lp - pAfterName + 1;

        pNetResourceInfo->lpProvider = pStrings;
        CopyMemory(pStrings, MRX_VBOX_PROVIDER_NAME_U, sizeof(MRX_VBOX_PROVIDER_NAME_U));
        pStrings += sizeof(MRX_VBOX_PROVIDER_NAME_U) / sizeof(WCHAR);

        Log(("VBOXNP: NPGetResourceInformation: pRemoteName: %ls, strings %p/%p\n",
             pNetResourceInfo->lpRemoteName, pStrings, (PBYTE)pBuffer + *pBufferSize));

        if (lplpSystem)
        {
            *lplpSystem = NULL;
        }

        return WN_SUCCESS;
    }

    /* \\vboxsvr\share\path */
    cbEntry = sizeof(NETRESOURCE);
    cbEntry += 2 * sizeof(WCHAR) + sizeof(MRX_VBOX_SERVER_NAME_U); /* \\ + server name with trailing nul */
    cbEntry += (DWORD)((lp - pAfterName) * sizeof(WCHAR)); /* The share name with leading \\ */
    cbEntry += sizeof(MRX_VBOX_PROVIDER_NAME_U); /* provider name */
    cbEntry += (lstrlen(lp) + 1) * sizeof (WCHAR); /* path string for lplpSystem */

    if (cbEntry > *pBufferSize)
    {
        Log(("VBOXNP: NPGetResourceInformation: WN_MORE_DATA 0x%x\n", cbEntry));
        *pBufferSize = cbEntry;
        return WN_MORE_DATA;
    }

    memset(pNetResourceInfo, 0, sizeof (*pNetResourceInfo));

    pNetResourceInfo->dwType = RESOURCETYPE_DISK;
    pNetResourceInfo->dwDisplayType = RESOURCEDISPLAYTYPE_SHARE;
    pNetResourceInfo->dwUsage = RESOURCEUSAGE_CONNECTABLE;

    pStrings = (PWCHAR)((PBYTE)pStrings - (cbEntry - sizeof(NETRESOURCE)));

    /* The server + share. */
    pNetResourceInfo->lpRemoteName = pStrings;
    *pStrings++ = L'\\';
    *pStrings++ = L'\\';
    CopyMemory (pStrings, MRX_VBOX_SERVER_NAME_U, sizeof(MRX_VBOX_SERVER_NAME_U) - sizeof (WCHAR));
    pStrings += sizeof(MRX_VBOX_SERVER_NAME_U) / sizeof(WCHAR) - 1;
    CopyMemory(pStrings, pAfterName, (lp - pAfterName) * sizeof(WCHAR));
    pStrings += lp - pAfterName;
    *pStrings++ = 0;

    pNetResourceInfo->lpProvider = pStrings;
    CopyMemory(pStrings, MRX_VBOX_PROVIDER_NAME_U, sizeof(MRX_VBOX_PROVIDER_NAME_U));
    pStrings += sizeof(MRX_VBOX_PROVIDER_NAME_U) / sizeof(WCHAR);

    if (lplpSystem)
    {
        *lplpSystem = pStrings;
    }

    lstrcpy(pStrings, lp);
    pStrings += lstrlen(lp) + 1;

    Log(("VBOXNP: NPGetResourceInformation: pRemoteName: %ls, strings %p/%p\n",
         pNetResourceInfo->lpRemoteName, pStrings, (PBYTE)pBuffer + *pBufferSize));
    Log(("VBOXNP: NPGetResourceInformation: *lplpSystem: %ls\n", *lplpSystem));

    return WN_SUCCESS;
}

DWORD APIENTRY NPGetUniversalName(LPCWSTR pLocalPath,
                                  DWORD dwInfoLevel,
                                  LPVOID pBuffer,
                                  LPDWORD pBufferSize)
{
    DWORD dwStatus;

    DWORD BufferRequired = 0;
    DWORD RemoteNameLength = 0;
    DWORD RemainingPathLength = 0;

    WCHAR LocalDrive[3];

    const WCHAR *pRemainingPath;
    WCHAR *pString;

    Log(("VBOXNP: NPGetUniversalName: pLocalPath = %ls, InfoLevel = %d, *pBufferSize = %d\n",
         pLocalPath, dwInfoLevel, *pBufferSize));

    /* Check is input parameter is OK. */
    if (   dwInfoLevel != UNIVERSAL_NAME_INFO_LEVEL
        && dwInfoLevel != REMOTE_NAME_INFO_LEVEL)
    {
        Log(("VBOXNP: NPGetUniversalName: Bad dwInfoLevel value: %d\n",
             dwInfoLevel));
        return WN_BAD_LEVEL;
    }

    /* The 'lpLocalPath' is "X:\something". Extract the "X:" to pass to NPGetConnection. */
    if (   pLocalPath == NULL
        || pLocalPath[0] == 0
        || pLocalPath[1] != L':')
    {
        Log(("VBOXNP: NPGetUniversalName: Bad pLocalPath.\n"));
        return WN_BAD_LOCALNAME;
    }

    LocalDrive[0] = pLocalPath[0];
    LocalDrive[1] = pLocalPath[1];
    LocalDrive[2] = 0;

    /* Length of the original path without the driver letter, including trailing NULL. */
    pRemainingPath = &pLocalPath[2];
    RemainingPathLength = (DWORD)((wcslen(pRemainingPath) + 1) * sizeof(WCHAR));

    /* Build the required structure in place of the supplied buffer. */
    if (dwInfoLevel == UNIVERSAL_NAME_INFO_LEVEL)
    {
        LPUNIVERSAL_NAME_INFOW pUniversalNameInfo = (LPUNIVERSAL_NAME_INFOW)pBuffer;

        BufferRequired = sizeof (UNIVERSAL_NAME_INFOW);

        if (*pBufferSize >= BufferRequired)
        {
            /* Enough place for the structure. */
            pUniversalNameInfo->lpUniversalName = (PWCHAR)((PBYTE)pBuffer + sizeof(UNIVERSAL_NAME_INFOW));

            /* At least so many bytes are available for obtaining the remote name. */
            RemoteNameLength = *pBufferSize - BufferRequired;
        }
        else
        {
            RemoteNameLength = 0;
        }

        /* Put the remote name directly to the buffer if possible and get the name length. */
        dwStatus = NPGetConnection(LocalDrive,
                                   RemoteNameLength? pUniversalNameInfo->lpUniversalName: NULL,
                                   &RemoteNameLength);

        if (   dwStatus != WN_SUCCESS
            && dwStatus != WN_MORE_DATA)
        {
            if (dwStatus != WN_NOT_CONNECTED)
            {
                Log(("VBOXNP: NPGetUniversalName: NPGetConnection returned error 0x%lx\n",
                     dwStatus));
            }
            return dwStatus;
        }

        if (RemoteNameLength < sizeof (WCHAR))
        {
            Log(("VBOXNP: NPGetUniversalName: Remote name is empty.\n"));
            return WN_NO_NETWORK;
        }

        /* Adjust for actual remote name length. */
        BufferRequired += RemoteNameLength;

        /* And for required place for remaining path. */
        BufferRequired += RemainingPathLength;

        if (*pBufferSize < BufferRequired)
        {
            Log(("VBOXNP: NPGetUniversalName: WN_MORE_DATA BufferRequired: %d\n",
                 BufferRequired));
            *pBufferSize = BufferRequired;
            return WN_MORE_DATA;
        }

        /* Enough memory in the buffer. Add '\' and remaining path to the remote name. */
        pString = &pUniversalNameInfo->lpUniversalName[RemoteNameLength / sizeof (WCHAR)];
        pString--; /* Trailing NULL */

        CopyMemory(pString, pRemainingPath, RemainingPathLength);
    }
    else
    {
        LPREMOTE_NAME_INFOW pRemoteNameInfo = (LPREMOTE_NAME_INFOW)pBuffer;
        WCHAR *pDelimiter;

        BufferRequired = sizeof (REMOTE_NAME_INFOW);

        if (*pBufferSize >= BufferRequired)
        {
            /* Enough place for the structure. */
            pRemoteNameInfo->lpUniversalName = (PWCHAR)((PBYTE)pBuffer + sizeof(REMOTE_NAME_INFOW));
            pRemoteNameInfo->lpConnectionName = NULL;
            pRemoteNameInfo->lpRemainingPath = NULL;

            /* At least so many bytes are available for obtaining the remote name. */
            RemoteNameLength = *pBufferSize - BufferRequired;
        }
        else
        {
            RemoteNameLength = 0;
        }

        /* Put the remote name directly to the buffer if possible and get the name length. */
        dwStatus = NPGetConnection(LocalDrive, RemoteNameLength? pRemoteNameInfo->lpUniversalName: NULL, &RemoteNameLength);

        if (   dwStatus != WN_SUCCESS
            && dwStatus != WN_MORE_DATA)
        {
            if (dwStatus != WN_NOT_CONNECTED)
            {
                Log(("VBOXNP: NPGetUniversalName: NPGetConnection returned error 0x%lx\n", dwStatus));
            }
            return dwStatus;
        }

        if (RemoteNameLength < sizeof (WCHAR))
        {
            Log(("VBOXNP: NPGetUniversalName: Remote name is empty.\n"));
            return WN_NO_NETWORK;
        }

        /* Adjust for actual remote name length as a part of the universal name. */
        BufferRequired += RemoteNameLength;

        /* And for required place for remaining path as a part of the universal name. */
        BufferRequired += RemainingPathLength;

        /* pConnectionName, which is the remote name. */
        BufferRequired += RemoteNameLength;

        /* pRemainingPath. */
        BufferRequired += RemainingPathLength;

        if (*pBufferSize < BufferRequired)
        {
            Log(("VBOXNP: NPGetUniversalName: WN_MORE_DATA BufferRequired: %d\n",
                 BufferRequired));
            *pBufferSize = BufferRequired;
            return WN_MORE_DATA;
        }

        /* Enough memory in the buffer. Add \ and remaining path to the remote name. */
        pString = &pRemoteNameInfo->lpUniversalName[RemoteNameLength / sizeof (WCHAR)];
        pString--; /* Trailing NULL */

        pDelimiter = pString; /* Delimiter between the remote name and the remaining path.
                                 * May be 0 if the remaining path is empty.
                                 */

        CopyMemory( pString, pRemainingPath, RemainingPathLength);
        pString += RemainingPathLength / sizeof (WCHAR);

        *pDelimiter = 0; /* Keep NULL terminated remote name. */

        pRemoteNameInfo->lpConnectionName = pString;
        CopyMemory( pString, pRemoteNameInfo->lpUniversalName, RemoteNameLength);
        pString += RemoteNameLength / sizeof (WCHAR);

        pRemoteNameInfo->lpRemainingPath = pString;
        CopyMemory( pString, pRemainingPath, RemainingPathLength);

        /* If remaining path was not empty, restore the delimiter in the universal name. */
        if (RemainingPathLength > sizeof(WCHAR))
        {
           *pDelimiter = L'\\';
        }
    }

    Log(("VBOXNP: NPGetUniversalName: WN_SUCCESS\n"));
    return WN_SUCCESS;
}

BOOL WINAPI DllMain(HINSTANCE hDLLInst,
                    DWORD fdwReason,
                    LPVOID pvReserved)
{
    RT_NOREF(hDLLInst, pvReserved);
    BOOL fReturn = TRUE;

    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            RTR3InitDll(RTR3INIT_FLAGS_UNOBTRUSIVE);
            VbglR3Init();
            LogRel(("VBOXNP: DLL loaded.\n"));
            break;

        case DLL_PROCESS_DETACH:
            LogRel(("VBOXNP: DLL unloaded.\n"));
            VbglR3Term();
            /// @todo RTR3Term();
            break;

        case DLL_THREAD_ATTACH:
            break;

        case DLL_THREAD_DETACH:
            break;

        default:
            break;
    }

    return fReturn;
}
