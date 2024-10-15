/* $Id: VBoxNetLwfInstall.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NetLwfInstall - VBoxNetLwf installer command line tool
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
#include <VBox/VBoxNetCfg-win.h>
#include <devguid.h>

#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/utf16.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define VBOX_NETCFG_APP_NAME L"NetLwfInstall"
#define VBOX_NETLWF_INF L".\\VBoxNetLwf.inf"
#define VBOX_NETLWF_RETRIES 10


static DECLCALLBACK(void) winNetCfgLogger(const char *pszString)
{
    RTMsgInfo("%s", pszString);
}

/** Wrapper around GetfullPathNameW that will try an alternative INF location.
 *
 * The default location is the current directory.  If not found there, the
 * alternative location is the executable directory.  If not found there either,
 * the first alternative is present to the caller.
 */
static DWORD MyGetfullPathNameW(LPCWSTR pwszName, size_t cchFull, LPWSTR pwszFull)
{
    LPWSTR pwszFilePart;
    DWORD dwSize = GetFullPathNameW(pwszName, (DWORD)cchFull, pwszFull, &pwszFilePart);
    if (dwSize <= 0)
        return dwSize;

    /* if it doesn't exist, see if the file exists in the same directory as the executable. */
    if (GetFileAttributesW(pwszFull) == INVALID_FILE_ATTRIBUTES)
    {
        WCHAR wsz[512];
        DWORD cch = GetModuleFileNameW(GetModuleHandle(NULL), &wsz[0], RT_ELEMENTS(wsz));
        if (cch > 0)
        {
            while (cch > 0 && wsz[cch - 1] != '/' && wsz[cch - 1] != '\\' && wsz[cch - 1] != ':')
                cch--;
            unsigned i = 0;
            while (cch < RT_ELEMENTS(wsz))
            {
                wsz[cch] = pwszFilePart[i++];
                if (!wsz[cch])
                {
                    dwSize = GetFullPathNameW(wsz, (DWORD)cchFull, pwszFull, NULL);
                    if (dwSize > 0 && GetFileAttributesW(pwszFull) != INVALID_FILE_ATTRIBUTES)
                        return dwSize;
                    break;
                }
                cch++;
            }
        }
    }

    /* fallback */
    return GetFullPathNameW(pwszName, (DWORD)cchFull, pwszFull, NULL);
}

static int VBoxNetLwfInstall()
{
    WCHAR wszInf[MAX_PATH];
    INetCfg *pnc;
    int rcExit = RTEXITCODE_FAILURE;

    VBoxNetCfgWinSetLogging(winNetCfgLogger);

    HRESULT hr = CoInitialize(NULL);
    if (hr == S_OK)
    {
        for (int i = 0;; i++)
        {
            LPWSTR pwszLockedBy = NULL;
            hr = VBoxNetCfgWinQueryINetCfg(&pnc, TRUE, VBOX_NETCFG_APP_NAME, 10000, &pwszLockedBy);
            if (hr == S_OK)
            {
                DWORD dwSize;
                dwSize = MyGetfullPathNameW(VBOX_NETLWF_INF, RT_ELEMENTS(wszInf), wszInf);
                if (dwSize > 0)
                {
                    /** @todo add size check for (RT_ELEMENTS(wszInf) == dwSize (string length in WCHARs) */
                    hr = VBoxNetCfgWinNetLwfInstall(pnc, wszInf);
                    if (hr == S_OK)
                    {
                        RTMsgInfo("Installed successfully!");
                        rcExit = RTEXITCODE_SUCCESS;
                    }
                    else
                        RTMsgError("Failed installing VBoxNetLwf: %Rhrc", hr);
                }
                else
                {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    RTMsgError("Failed getting full inf path for VBoxNetLwf.inf: %Rhrc", hr);
                }

                VBoxNetCfgWinReleaseINetCfg(pnc, TRUE);
                break;
            }

            if (hr == NETCFG_E_NO_WRITE_LOCK && pwszLockedBy)
            {
                if (i < VBOX_NETLWF_RETRIES && RTUtf16ICmpAscii(pwszLockedBy, "6to4svc.dll") == 0)
                {
                    RTMsgInfo("6to4svc.dll is holding the lock - retrying %d out of %d\n", i + 1, VBOX_NETLWF_RETRIES);
                    CoTaskMemFree(pwszLockedBy);
                }
                else
                {
                    RTMsgError("write lock is owned by another application (%ls), close the application and retry installing",
                               pwszLockedBy);
                    CoTaskMemFree(pwszLockedBy);
                    break;
                }
            }
            else
            {
                RTMsgError("Failed getting the INetCfg interface: %Rhrc", hr);
                break;
            }
        }

        CoUninitialize();
    }
    else
        RTMsgError("Failed initializing COM: %Rhrc", hr);

    VBoxNetCfgWinSetLogging(NULL);

    return rcExit;
}

int __cdecl main(int argc, char **argv)
{
    RTR3InitExeNoArguments(0);
    if (argc != 1)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "This utility takes no arguments\n");
    NOREF(argv);

    return VBoxNetLwfInstall();
}

