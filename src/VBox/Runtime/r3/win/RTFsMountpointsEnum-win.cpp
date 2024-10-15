/* $Id: RTFsMountpointsEnum-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File System, RTFsMountpointsEnum, Windows.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#include "internal/iprt.h"
#include <iprt/nt/nt-and-windows.h>
#include "internal-r3-win.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/once.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/utf16.h>

#include <iprt/win/windows.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/* kernel32.dll: */
typedef HANDLE (WINAPI *PFNFINDFIRSTVOLUMEW)(LPWSTR, DWORD);
typedef BOOL (WINAPI *PFNFINDNEXTVOLUMEW)(HANDLE, LPWSTR, DWORD);
typedef BOOL (WINAPI *PFNFINDVOLUMECLOSE)(HANDLE);
typedef BOOL (WINAPI *PFNGETVOLUMEPATHNAMESFORVOLUMENAMEW)(LPCWSTR, LPWCH, DWORD, PDWORD);
typedef HANDLE (WINAPI *PFNFINDFIRSTVOLUMEMOUNTPOINTW)(LPCWSTR, LPWSTR, DWORD);
typedef BOOL (WINAPI *PFNFINDNEXTVOLUMEMOUNTPOINTW)(HANDLE, LPWSTR, DWORD);
typedef BOOL (WINAPI *PFNFINDVOLUMEMOUNTPOINTCLOSE)(HANDLE);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/* kernel32.dll: */
static PFNFINDFIRSTVOLUMEW                 g_pfnFindFirstVolumeW                 = NULL;
static PFNFINDNEXTVOLUMEW                  g_pfnFindNextVolumeW                  = NULL;
static PFNFINDVOLUMECLOSE                  g_pfnFindVolumeClose                  = NULL;
static PFNGETVOLUMEPATHNAMESFORVOLUMENAMEW g_pfnGetVolumePathNamesForVolumeNameW = NULL;
static PFNFINDFIRSTVOLUMEMOUNTPOINTW       g_pfnFindFirstVolumeMountPointW       = NULL;
static PFNFINDNEXTVOLUMEMOUNTPOINTW        g_pfnFindNextVolumeMountPointW        = NULL;
static PFNFINDVOLUMEMOUNTPOINTCLOSE        g_pfnFindVolumeMountPointClose        = NULL;

/** Init once structure we need. */
static RTONCE g_rtFsWinResolveOnce = RTONCE_INITIALIZER;


/**
 * Initialize the import APIs.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if not supported.
 * @param   pvUser              Ignored.
 */
static DECLCALLBACK(int) rtFsWinResolveOnce(void *pvUser)
{
    RT_NOREF(pvUser);

    /*
     * kernel32.dll volume APIs introduced after NT4.
     */
    g_pfnFindFirstVolumeW                 = (PFNFINDFIRSTVOLUMEW                )GetProcAddress(g_hModKernel32, "FindFirstVolumeW");
    g_pfnFindNextVolumeW                  = (PFNFINDNEXTVOLUMEW                 )GetProcAddress(g_hModKernel32, "FindNextVolumeW");
    g_pfnFindVolumeClose                  = (PFNFINDVOLUMECLOSE                 )GetProcAddress(g_hModKernel32, "FindVolumeClose");
    g_pfnGetVolumePathNamesForVolumeNameW = (PFNGETVOLUMEPATHNAMESFORVOLUMENAMEW)GetProcAddress(g_hModKernel32, "GetVolumePathNamesForVolumeNameW");
    g_pfnFindFirstVolumeMountPointW       = (PFNFINDFIRSTVOLUMEMOUNTPOINTW      )GetProcAddress(g_hModKernel32, "FindFirstVolumeMountPointW");
    g_pfnFindNextVolumeMountPointW        = (PFNFINDNEXTVOLUMEMOUNTPOINTW       )GetProcAddress(g_hModKernel32, "FindNextVolumeMountPointW");
    g_pfnFindVolumeMountPointClose        = (PFNFINDVOLUMEMOUNTPOINTCLOSE       )GetProcAddress(g_hModKernel32, "FindVolumeMountPointClose");

    if (   !g_pfnFindFirstVolumeW
        || !g_pfnFindNextVolumeW
        || !g_pfnFindVolumeClose
        || !g_pfnGetVolumePathNamesForVolumeNameW
        || !g_pfnFindFirstVolumeMountPointW
        || !g_pfnFindNextVolumeMountPointW
        || !g_pfnFindVolumeMountPointClose)
        return VERR_NOT_SUPPORTED;

    return VINF_SUCCESS;
}

/**
 * Handles a mountpoint callback with WCHAR (as UTF-16) strings.
 *
 * @returns IPRT status code. Failure terminates the enumeration.
 * @param   pfnCallback         Callback function to invoke.
 * @param   pvUser              User-supplied pointer.
 * @param   pwszStr             WCHAR string to pass to the callback function.
 */
DECLINLINE(int) rtFsWinHandleCallback(PFNRTFSMOUNTPOINTENUM pfnCallback, void *pvUser, PWCHAR pwszStr)
{
    char *psz;
    int rc = RTUtf16ToUtf8((PCRTUTF16)pwszStr, &psz);
    AssertRCReturn(rc, rc);
    rc = pfnCallback(psz, pvUser);
    RTStrFree(psz);
    return rc;
}

/**
 * Mountpoint enumeration worker.
 *
 * This always enumerates all available/connected (disk-based) mountpoints and
 * can be used for other functions to perform (custom) filtering.
 *
 * @returns IPRT status code.
 * @param   fRemote             Also enumerates (currently connected) remote network resources.
 * @param   pfnCallback         Callback function to invoke.
 * @param   pvUser              User-supplied pointer.  Optional and can be NULL.
 */
static int rtFsWinMountpointsEnumWorker(bool fRemote, PFNRTFSMOUNTPOINTENUM pfnCallback, void *pvUser)
{
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);
    /* pvUser is optional. */

    int rc = VINF_SUCCESS;

    SetLastError(0);

    WCHAR wszVol[RTPATH_MAX];
    HANDLE hVol = g_pfnFindFirstVolumeW(wszVol, RT_ELEMENTS(wszVol));
    if (   hVol
        && hVol != INVALID_HANDLE_VALUE)
    {
        do
        {
            WCHAR wszMp[RTPATH_MAX];
            HANDLE hMp = g_pfnFindFirstVolumeMountPointW(wszVol, wszMp, RT_ELEMENTS(wszMp));
            if (   hMp
                && hMp != INVALID_HANDLE_VALUE)
            {
                do
                {
                    rc = rtFsWinHandleCallback(pfnCallback, pvUser, wszMp);
                    if (RT_FAILURE(rc))
                        break;
                }
                while (g_pfnFindNextVolumeMountPointW(hMp, wszMp, RT_ELEMENTS(wszMp)));
                g_pfnFindVolumeMountPointClose(hMp);
            }
            else
            {
                DWORD const dwErr = GetLastError();
                if (   dwErr != ERROR_NO_MORE_FILES
                    && dwErr != ERROR_PATH_NOT_FOUND
                    && dwErr != ERROR_UNRECOGNIZED_VOLUME
                    && dwErr != ERROR_ACCESS_DENIED) /* Can happen for regular users, so just skip this stuff then. */
                    rc = RTErrConvertFromWin32(GetLastError());
            }

            if (RT_SUCCESS(rc))
            {
                DWORD cwch = 0;
                if (g_pfnGetVolumePathNamesForVolumeNameW(wszVol, wszMp, RT_ELEMENTS(wszMp), &cwch))
                {
                    size_t off = 0;
                    while (off < cwch)
                    {
                        size_t cwc = wcslen(&wszMp[off]);
                        if (cwc)
                        {
                            rc = rtFsWinHandleCallback(pfnCallback, pvUser, &wszMp[off]);
                            if (RT_FAILURE(rc))
                                break;
                        }
                        off += cwc + 1;
                    }
                }
                else
                    rc = RTErrConvertFromWin32(GetLastError());
            }

        } while (RT_SUCCESS(rc) && g_pfnFindNextVolumeW(hVol, wszVol, RT_ELEMENTS(wszVol)));
        g_pfnFindVolumeClose(hVol);
    }
    else
        rc = RTErrConvertFromWin32(GetLastError());

    if (   RT_SUCCESS(rc)
        && fRemote)
    {
        HANDLE hEnum;
        DWORD dwErr = WNetOpenEnumA(RESOURCE_CONNECTED, RESOURCETYPE_DISK, RESOURCEUSAGE_ALL, NULL, &hEnum);
        if (dwErr == NO_ERROR)
        {
            for (;;)
            {
                DWORD cResources = 1;
                DWORD cbRet = sizeof(wszVol);
                dwErr = WNetEnumResourceW(hEnum, &cResources, wszVol, &cbRet);
                if (dwErr == NO_ERROR)
                {
                    NETRESOURCEW const *pRsrc = (NETRESOURCEW const *)wszVol;
                    if (   pRsrc
                        && pRsrc->lpLocalName)
                    {
                        rc = rtFsWinHandleCallback(pfnCallback, pvUser, (PWCHAR)pRsrc->lpLocalName);
                        if (RT_FAILURE(rc))
                            break;
                    }
                }
                else
                {
                    if (dwErr != ERROR_NO_MORE_ITEMS)
                        rc = RTErrConvertFromWin32(dwErr);
                    break;
                }
            }
        }
        else
            rc = RTErrConvertFromWin32(dwErr);
    }

    return rc;
}


RTR3DECL(int) RTFsMountpointsEnum(PFNRTFSMOUNTPOINTENUM pfnCallback, void *pvUser)
{
    int rc = RTOnce(&g_rtFsWinResolveOnce, rtFsWinResolveOnce, NULL);
    if (RT_FAILURE(rc))
        return rc;

    return rtFsWinMountpointsEnumWorker(true /* fRemote */, pfnCallback, pvUser);
}

