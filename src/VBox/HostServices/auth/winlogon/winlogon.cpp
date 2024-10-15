/* $Id: winlogon.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox External Authentication Library - Windows Logon Authentication.
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

/* If defined, debug messages will be written to the debugger. */
// #define AUTH_DEBUG

#include <iprt/win/windows.h>
#include <VBox/VBoxAuth.h>
#include <iprt/cdefs.h>

#ifdef AUTH_DEBUG
# include <stdio.h>

static void dprintfw(const WCHAR *fmt, ...)
{
   va_list va;
   va_start(va, fmt);

   WCHAR buffer[1024];

   _vsnwprintf(buffer, sizeof (buffer), fmt, va);

   OutputDebugStringW(buffer);

   va_end(va);
}
# define DBGAUTH(a) dprintfw a
#else
# define DBGAUTH(a)
#endif

static WCHAR g_wszEmpty[] = { L"" };

static void freeWideChar(WCHAR *pwszString)
{
    if (pwszString && pwszString != &g_wszEmpty[0])
    {
        size_t cb = (wcslen(pwszString) + 1) * sizeof(WCHAR);
        SecureZeroMemory(pwszString, cb);
        free(pwszString);
    }
}

static WCHAR *utf8ToWideChar(const char *pszString)
{
    /*
     * Shortcut for empty strings.
     */
    if (!pszString || *pszString == 0)
        return &g_wszEmpty[0];

    /*
     * Return NULL on errors.
     */
    WCHAR *pwszString = NULL;

    /*
     * First calc result string length.
     */
    const DWORD dwFlags = MB_ERR_INVALID_CHARS;
    int cwc = MultiByteToWideChar(CP_UTF8, dwFlags, pszString, -1, NULL, 0);
    if (cwc > 0)
    {
        /*
         * Alloc space for result buffer.
         */
        pwszString = (WCHAR *)malloc(cwc * sizeof(WCHAR));
        if (pwszString)
        {
            /*
             * Do the translation.
             */
            if (MultiByteToWideChar(CP_UTF8, dwFlags, pszString, -1, pwszString, cwc) <= 0)
            {
                /* translation error */
                free(pwszString);
                pwszString = NULL;
            }
        }
    }

    return pwszString;
}

/* Prototype it to make sure we've got the right prototype. */
#if defined(_MSC_VER)
extern "C" __declspec(dllexport) FNAUTHENTRY3 AuthEntry;
#else
extern "C" FNAUTHENTRY3 AuthEntry;
#endif

/**
 * @callback_method_impl{FNAUTHENTRY3}
 */
extern "C" DECLEXPORT(AuthResult) AUTHCALL
AuthEntry(const char *pszCaller,
          PAUTHUUID pUuid,
          AuthGuestJudgement guestJudgement,
          const char *pszUser,
          const char *pszPassword,
          const char *pszDomain,
          int fLogon,
          unsigned clientId)
{
    RT_NOREF4(pszCaller, pUuid, guestJudgement, clientId);
    if (!fLogon)
    {
        /* Nothing to cleanup. The return code does not matter. */
        return AuthResultAccessDenied;
    }

    LPWSTR pwszUsername = utf8ToWideChar(pszUser);
    LPWSTR pwszDomain   = utf8ToWideChar(pszDomain);
    LPWSTR pwszPassword = utf8ToWideChar(pszPassword);

    DBGAUTH((L"u[%ls], d[%ls], p[%ls]\n", lpwszUsername, lpwszDomain, lpwszPassword));

    AuthResult result = AuthResultAccessDenied;

    if (pwszUsername && pwszDomain && pwszPassword)
    {
        /* LOGON32_LOGON_INTERACTIVE is intended for users who will be interactively using the computer,
         * such as a user being logged on by a terminal server, remote shell, or similar process.
         */
        DWORD dwLogonType     = LOGON32_LOGON_INTERACTIVE;
        DWORD dwLogonProvider = LOGON32_PROVIDER_DEFAULT;

        HANDLE hToken;

        BOOL fSuccess = LogonUserW(pwszUsername,
                                   pwszDomain,
                                   pwszPassword,
                                   dwLogonType,
                                   dwLogonProvider,
                                   &hToken);

        if (fSuccess)
        {
            DBGAUTH((L"LogonUser success. hToken = %p\n", hToken));

            result = AuthResultAccessGranted;

            CloseHandle(hToken);
        }
        else
        {
            DBGAUTH((L"LogonUser failed %08X\n", GetLastError()));
        }
    }

    freeWideChar(pwszUsername);
    freeWideChar(pwszDomain);
    freeWideChar(pwszPassword);

    return result;
}

