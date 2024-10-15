/* $Id: process-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Process, Windows.
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
#define LOG_GROUP RTLOGGROUP_PROCESS
#include <iprt/asm.h> /* hack */

#include <iprt/nt/nt-and-windows.h>
#include <Userenv.h>
#include <tlhelp32.h>
#ifndef IPRT_NO_CRT
# include <process.h>
# include <errno.h>
# include <Strsafe.h>
#endif
#include <LsaLookup.h>
#include <Lmcons.h>

#define _NTDEF_ /* Prevents redefining (P)UNICODE_STRING. */
#include <Ntsecapi.h>

#include <iprt/process.h>
#include "internal-r3-win.h"

#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/env.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/ldr.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/once.h>
#include <iprt/path.h>
#include <iprt/pipe.h>
#include <iprt/string.h>
#include <iprt/socket.h>
#include <iprt/utf16.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/* kernel32.dll: */
//typedef DWORD   (WINAPI *PFNWTSGETACTIVECONSOLESESSIONID)(VOID);
typedef HANDLE  (WINAPI *PFNCREATETOOLHELP32SNAPSHOT)(DWORD, DWORD);
typedef BOOL    (WINAPI *PFNPROCESS32FIRSTW)(HANDLE, LPPROCESSENTRY32W);
typedef BOOL    (WINAPI *PFNPROCESS32NEXTW)(HANDLE, LPPROCESSENTRY32W);

/* psapi.dll: */
typedef BOOL    (WINAPI *PFNENUMPROCESSES)(LPDWORD, DWORD, LPDWORD);
typedef DWORD   (WINAPI *PFNGETMODULEBASENAMEW)(HANDLE, HMODULE, LPWSTR, DWORD);

/* advapi32.dll: */
typedef BOOL    (WINAPI *PFNCREATEPROCESSWITHLOGON)(LPCWSTR, LPCWSTR, LPCWSTR, DWORD, LPCWSTR, LPWSTR, DWORD,
                                                    LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
typedef NTSTATUS (NTAPI *PFNLSALOOKUPNAMES2)(LSA_HANDLE, ULONG, ULONG, PLSA_UNICODE_STRING,
                                             PLSA_REFERENCED_DOMAIN_LIST*, PLSA_TRANSLATED_SID2*);

/* userenv.dll: */
typedef BOOL    (WINAPI *PFNCREATEENVIRONMENTBLOCK)(LPVOID *, HANDLE, BOOL);
typedef BOOL    (WINAPI *PFNPFNDESTROYENVIRONMENTBLOCK)(LPVOID);
typedef BOOL    (WINAPI *PFNLOADUSERPROFILEW)(HANDLE, LPPROFILEINFOW);
typedef BOOL    (WINAPI *PFNUNLOADUSERPROFILE)(HANDLE, HANDLE);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Init once structure. */
static RTONCE       g_rtProcWinInitOnce = RTONCE_INITIALIZER;
/** Critical section protecting the process array. */
static RTCRITSECT   g_CritSect;
/** The number of processes in the array. */
static uint32_t     g_cProcesses;
/** The current allocation size. */
static uint32_t     g_cProcessesAlloc;
/** Array containing the live or non-reaped child processes. */
static struct RTPROCWINENTRY
{
    /** The process ID. */
    ULONG_PTR       pid;
    /** The process handle. */
    HANDLE          hProcess;
}                  *g_paProcesses;

/** Structure for storing a user's account info.
 *  Must be free'd with rtProcWinFreeAccountInfo(). */
typedef struct RTPROCWINACCOUNTINFO
{
    /** User name. */
    PRTUTF16        pwszUserName;
    /** Domain this account is tied to. Can be NULL if no domain is being used. */
    PRTUTF16        pwszDomain;
} RTPROCWINACCOUNTINFO, *PRTPROCWINACCOUNTINFO;

/** @name userenv.dll imports (we don't unload it).
 * They're all optional. So in addition to using g_rtProcWinResolveOnce, the
 * caller must also check if any of the necessary APIs are NULL pointers.
 * @{ */
/** Init once structure for run-as-user functions we need. */
static RTONCE                           g_rtProcWinResolveOnce          = RTONCE_INITIALIZER;
/* kernel32.dll: */
static PFNCREATETOOLHELP32SNAPSHOT      g_pfnCreateToolhelp32Snapshot   = NULL;
static PFNPROCESS32FIRSTW               g_pfnProcess32FirstW            = NULL;
static PFNPROCESS32NEXTW                g_pfnProcess32NextW             = NULL;
/* psapi.dll: */
static PFNGETMODULEBASENAMEW            g_pfnGetModuleBaseNameW         = NULL;
static PFNENUMPROCESSES                 g_pfnEnumProcesses              = NULL;
/* advapi32.dll: */
static PFNCREATEPROCESSWITHLOGON        g_pfnCreateProcessWithLogonW    = NULL;
static decltype(LogonUserW)            *g_pfnLogonUserW                 = NULL;
static decltype(CreateProcessAsUserW)  *g_pfnCreateProcessAsUserW       = NULL;
/* user32.dll: */
static decltype(OpenWindowStationW)    *g_pfnOpenWindowStationW         = NULL;
static decltype(CloseWindowStation)    *g_pfnCloseWindowStation         = NULL;
/* user32.dll: (for reasons of bad AV heuristics) */
static decltype(GetUserObjectSecurity) *g_pfnGetUserObjectSecurity     = NULL;
static decltype(SetUserObjectSecurity) *g_pfnSetUserObjectSecurity     = NULL;
/* userenv.dll: */
static PFNCREATEENVIRONMENTBLOCK        g_pfnCreateEnvironmentBlock     = NULL;
static PFNPFNDESTROYENVIRONMENTBLOCK    g_pfnDestroyEnvironmentBlock    = NULL;
static PFNLOADUSERPROFILEW              g_pfnLoadUserProfileW           = NULL;
static PFNUNLOADUSERPROFILE             g_pfnUnloadUserProfile          = NULL;
/** @} */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int rtProcWinFindExe(uint32_t fFlags, RTENV hEnv, const char *pszExec, PRTUTF16 *ppwszExec);
static int rtProcWinCreateEnvBlockAndFindExe(uint32_t fFlags, RTENV hEnv, const char *pszExec,
                                             PRTUTF16 *ppwszzBlock, PRTUTF16 *ppwszExec);


/**
 * Clean up the globals.
 *
 * @param   enmReason           Ignored.
 * @param   iStatus             Ignored.
 * @param   pvUser              Ignored.
 */
static DECLCALLBACK(void) rtProcWinTerm(RTTERMREASON enmReason, int32_t iStatus, void *pvUser)
{
    NOREF(pvUser); NOREF(iStatus); NOREF(enmReason);

    RTCritSectDelete(&g_CritSect);

    size_t i = g_cProcesses;
    while (i-- > 0)
    {
        CloseHandle(g_paProcesses[i].hProcess);
        g_paProcesses[i].hProcess = NULL;
    }
    RTMemFree(g_paProcesses);

    g_paProcesses     = NULL;
    g_cProcesses      = 0;
    g_cProcessesAlloc = 0;
}


/**
 * Initialize the globals.
 *
 * @returns IPRT status code.
 * @param   pvUser              Ignored.
 */
static DECLCALLBACK(int32_t) rtProcWinInitOnce(void *pvUser)
{
    NOREF(pvUser);

    g_cProcesses        = 0;
    g_cProcessesAlloc   = 0;
    g_paProcesses       = NULL;
    int rc = RTCritSectInit(&g_CritSect);
    if (RT_SUCCESS(rc))
    {
        /** @todo init once, terminate once - this is a generic thing which should
         *        have some kind of static and simpler setup!  */
        rc = RTTermRegisterCallback(rtProcWinTerm, NULL);
        if (RT_SUCCESS(rc))
            return rc;
        RTCritSectDelete(&g_CritSect);
    }
    return rc;
}


/**
 * Gets the process handle for a process from g_paProcesses.
 *
 * @returns Process handle if found, NULL if not.
 * @param   pid                 The process to remove (pid).
 */
static HANDLE rtProcWinFindPid(RTPROCESS pid)
{
    HANDLE hProcess = NULL;

    RTCritSectEnter(&g_CritSect);
    uint32_t i = g_cProcesses;
    while (i-- > 0)
        if (g_paProcesses[i].pid == pid)
        {
            hProcess = g_paProcesses[i].hProcess;
            break;
        }
    RTCritSectLeave(&g_CritSect);

    return hProcess;
}


/**
 * Removes a process from g_paProcesses and closes the process handle.
 *
 * @param   pid                 The process to remove (pid).
 */
static void rtProcWinRemovePid(RTPROCESS pid)
{
    RTCritSectEnter(&g_CritSect);
    uint32_t i = g_cProcesses;
    while (i-- > 0)
        if (g_paProcesses[i].pid == pid)
        {
            HANDLE hProcess = g_paProcesses[i].hProcess;

            g_cProcesses--;
            uint32_t cToMove = g_cProcesses - i;
            if (cToMove)
                memmove(&g_paProcesses[i], &g_paProcesses[i + 1], cToMove * sizeof(g_paProcesses[0]));

            RTCritSectLeave(&g_CritSect);
            CloseHandle(hProcess);
            return;
        }
    RTCritSectLeave(&g_CritSect);
}


/**
 * Adds a process to g_paProcesses.
 *
 * @returns IPRT status code.
 * @param   pid                 The process id.
 * @param   hProcess            The process handle.
 */
static int rtProcWinAddPid(RTPROCESS pid, HANDLE hProcess)
{
    RTCritSectEnter(&g_CritSect);

    uint32_t i = g_cProcesses;
    if (i >= g_cProcessesAlloc)
    {
        void *pvNew = RTMemRealloc(g_paProcesses, (i + 16) * sizeof(g_paProcesses[0]));
        if (RT_UNLIKELY(!pvNew))
        {
            RTCritSectLeave(&g_CritSect);
            return VERR_NO_MEMORY;
        }
        g_paProcesses     = (struct RTPROCWINENTRY *)pvNew;
        g_cProcessesAlloc = i + 16;
    }

    g_paProcesses[i].pid      = pid;
    g_paProcesses[i].hProcess = hProcess;
    g_cProcesses = i + 1;

    RTCritSectLeave(&g_CritSect);
    return VINF_SUCCESS;
}


/**
 * Initialize the import APIs for run-as-user and special environment support.
 *
 * @returns IPRT status code.
 * @param   pvUser              Ignored.
 */
static DECLCALLBACK(int) rtProcWinResolveOnce(void *pvUser)
{
    int      rc;
    RTLDRMOD hMod;
    RT_NOREF_PV(pvUser);

    /*
     * kernel32.dll APIs introduced after NT4.
     */
    g_pfnCreateToolhelp32Snapshot   = (PFNCREATETOOLHELP32SNAPSHOT)GetProcAddress(g_hModKernel32, "CreateToolhelp32Snapshot");
    g_pfnProcess32FirstW            = (PFNPROCESS32FIRSTW         )GetProcAddress(g_hModKernel32, "Process32FirstW");
    g_pfnProcess32NextW             = (PFNPROCESS32NEXTW          )GetProcAddress(g_hModKernel32, "Process32NextW");

    /*
     * psapi.dll APIs, if none of the above are available.
     */
    if (   !g_pfnCreateToolhelp32Snapshot
        || !g_pfnProcess32FirstW
        || !g_pfnProcess32NextW)
    {
        Assert(!g_pfnCreateToolhelp32Snapshot && !g_pfnProcess32FirstW && !g_pfnProcess32NextW);

        rc = RTLdrLoadSystem("psapi.dll", true /*fNoUnload*/, &hMod);
        if (RT_SUCCESS(rc))
        {
            rc = RTLdrGetSymbol(hMod, "GetModuleBaseNameW", (void **)&g_pfnGetModuleBaseNameW);
            AssertStmt(RT_SUCCESS(rc), g_pfnGetModuleBaseNameW = NULL);

            rc = RTLdrGetSymbol(hMod, "EnumProcesses", (void **)&g_pfnEnumProcesses);
            AssertStmt(RT_SUCCESS(rc), g_pfnEnumProcesses = NULL);

            RTLdrClose(hMod);
        }
    }

    /*
     * advapi32.dll APIs.
     */
    rc = RTLdrLoadSystem("advapi32.dll", true /*fNoUnload*/, &hMod);
    if (RT_SUCCESS(rc))
    {
        rc = RTLdrGetSymbol(hMod, "CreateProcessWithLogonW", (void **)&g_pfnCreateProcessWithLogonW);
        if (RT_FAILURE(rc)) { g_pfnCreateProcessWithLogonW = NULL; Assert(g_enmWinVer <= kRTWinOSType_NT4); }

        rc = RTLdrGetSymbol(hMod, "LogonUserW", (void **)&g_pfnLogonUserW);
        if (RT_FAILURE(rc)) { g_pfnLogonUserW = NULL; Assert(g_enmWinVer <= kRTWinOSType_NT350); }

        rc = RTLdrGetSymbol(hMod, "CreateProcessAsUserW", (void **)&g_pfnCreateProcessAsUserW);
        if (RT_FAILURE(rc)) { g_pfnCreateProcessAsUserW = NULL; Assert(g_enmWinVer <= kRTWinOSType_NT350); }

        RTLdrClose(hMod);
    }

    /*
     * user32.dll APIs.
     */
    rc = RTLdrLoadSystem("user32.dll", true /*fNoUnload*/, &hMod);
    if (RT_SUCCESS(rc))
    {
        rc = RTLdrGetSymbol(hMod, "OpenWindowStationW", (void **)&g_pfnOpenWindowStationW);
        if (RT_FAILURE(rc)) { g_pfnOpenWindowStationW = NULL; Assert(g_enmWinVer <= kRTWinOSType_NT310); }

        rc = RTLdrGetSymbol(hMod, "CloseWindowStation", (void **)&g_pfnCloseWindowStation);
        if (RT_FAILURE(rc)) { g_pfnCloseWindowStation = NULL; Assert(g_enmWinVer <= kRTWinOSType_NT310); }

        /* These are only imported to workaround bad AV detection heuristics. */
        rc = RTLdrGetSymbol(hMod, "GetUserObjectSecurity", (void **)&g_pfnGetUserObjectSecurity);
        AssertRC(rc);

        rc = RTLdrGetSymbol(hMod, "SetUserObjectSecurity", (void **)&g_pfnSetUserObjectSecurity);
        AssertRC(rc);

        RTLdrClose(hMod);
    }

    /*
     * userenv.dll APIs.
     */
    rc = RTLdrLoadSystem("userenv.dll", true /*fNoUnload*/, &hMod);
    if (RT_SUCCESS(rc))
    {
        rc = RTLdrGetSymbol(hMod, "LoadUserProfileW", (void **)&g_pfnLoadUserProfileW);
        if (RT_FAILURE(rc)) { g_pfnLoadUserProfileW = NULL; Assert(g_enmWinVer <= kRTWinOSType_NT4); }

        rc = RTLdrGetSymbol(hMod, "UnloadUserProfile", (void **)&g_pfnUnloadUserProfile);
        if (RT_FAILURE(rc)) { g_pfnUnloadUserProfile = NULL; Assert(g_enmWinVer <= kRTWinOSType_NT4); }

        rc = RTLdrGetSymbol(hMod, "CreateEnvironmentBlock", (void **)&g_pfnCreateEnvironmentBlock);
        if (RT_FAILURE(rc)) { g_pfnCreateEnvironmentBlock = NULL; Assert(g_enmWinVer <= kRTWinOSType_NT4); }

        rc = RTLdrGetSymbol(hMod, "DestroyEnvironmentBlock", (void **)&g_pfnDestroyEnvironmentBlock);
        if (RT_FAILURE(rc)) { g_pfnDestroyEnvironmentBlock = NULL; Assert(g_enmWinVer <= kRTWinOSType_NT4); }

        RTLdrClose(hMod);
    }

    return VINF_SUCCESS;
}


RTR3DECL(int) RTProcCreate(const char *pszExec, const char * const *papszArgs, RTENV Env, unsigned fFlags, PRTPROCESS pProcess)
{
    return RTProcCreateEx(pszExec, papszArgs, Env, fFlags,
                          NULL, NULL, NULL,  /* standard handles */
                          NULL /*pszAsUser*/, NULL /* pszPassword*/,
                          NULL /*pvExtraData*/, pProcess);
}


/**
 * The following NT call is for v3.51 and does the equivalent of:
 *      DuplicateTokenEx(hSrcToken, MAXIMUM_ALLOWED, NULL,
 *                       SecurityIdentification, TokenPrimary, phToken);
 */
static int rtProcWinDuplicateToken(HANDLE hSrcToken, PHANDLE phToken)
{
    int rc;
    if (g_pfnNtDuplicateToken)
    {
        SECURITY_QUALITY_OF_SERVICE SecQoS;
        SecQoS.Length              = sizeof(SecQoS);
        SecQoS.ImpersonationLevel  = SecurityIdentification;
        SecQoS.ContextTrackingMode = SECURITY_DYNAMIC_TRACKING;
        SecQoS.EffectiveOnly       = FALSE;

        OBJECT_ATTRIBUTES ObjAttr;
        InitializeObjectAttributes(&ObjAttr, NULL /*Name*/, 0 /*OBJ_XXX*/, NULL /*Root*/, NULL /*SecDesc*/);
        ObjAttr.SecurityQualityOfService = &SecQoS;

        NTSTATUS rcNt = g_pfnNtDuplicateToken(hSrcToken, MAXIMUM_ALLOWED, &ObjAttr, FALSE, TokenPrimary, phToken);
        if (NT_SUCCESS(rcNt))
            rc = VINF_SUCCESS;
        else
            rc = RTErrConvertFromNtStatus(rcNt);
    }
    else
        rc = VERR_SYMBOL_NOT_FOUND; /** @todo do we really need to duplicate the token? */
    return rc;
}


/**
 * Get the token assigned to the thread indicated by @a hThread.
 *
 * Only used when RTPROC_FLAGS_AS_IMPERSONATED_TOKEN is in effect and the
 * purpose is to get a duplicate the impersonated token of the current thread.
 *
 * @returns IPRT status code.
 * @param   hThread         The thread handle (current thread).
 * @param   phToken         Where to return the a duplicate of the thread token
 *                          handle on success. (The caller closes it.)
 */
static int rtProcWinGetThreadTokenHandle(HANDLE hThread, PHANDLE phToken)
{
    AssertPtr(phToken);

    int     rc;
    HANDLE hTokenThread;
    if (OpenThreadToken(hThread,
                        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY | TOKEN_DUPLICATE
                        | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_SESSIONID | TOKEN_READ | TOKEN_WRITE,
                        TRUE /* OpenAsSelf - for impersonation at SecurityIdentification level */,
                        &hTokenThread))
    {
        rc = rtProcWinDuplicateToken(hTokenThread, phToken);
        CloseHandle(hTokenThread);
    }
    else
        rc = RTErrConvertFromWin32(GetLastError());
    return rc;
}


/**
 * Get the token assigned the process indicated by @a hProcess.
 *
 * Only used when pwszUser is NULL and RTPROC_FLAGS_AS_IMPERSONATED_TOKEN isn't
 * set.
 *
 * @returns IPRT status code.
 * @param   hProcess        The process handle (current process).
 * @param   phToken         Where to return the a duplicate of the thread token
 *                          handle on success. (The caller closes it.)
 */
static int rtProcWinGetProcessTokenHandle(HANDLE hProcess, PHANDLE phToken)
{
    AssertPtr(phToken);

    int     rc;
    HANDLE hTokenProcess;
    if (OpenProcessToken(hProcess,
                         TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY | TOKEN_DUPLICATE
                         | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_SESSIONID | TOKEN_READ | TOKEN_WRITE,
                         &hTokenProcess))
    {
        rc = rtProcWinDuplicateToken(hTokenProcess, phToken); /* not sure if this is strictly necessary */
        CloseHandle(hTokenProcess);
    }
    else
        rc = RTErrConvertFromWin32(GetLastError());
    return rc;
}


/**
 * Get the process token of the process indicated by @a dwPID if the @a pSid and
 * @a idSessionDesired matches.
 *
 * @returns IPRT status code.
 * @param   dwPid               The process identifier.
 * @param   pSid                The secure identifier of the user.
 * @param   idDesiredSession    The session the process candidate should
 *                              preferably belong to, UINT32_MAX if anything
 *                              goes.
 * @param   phToken             Where to return the a duplicate of the process token
 *                              handle on success. (The caller closes it.)
 */
static int rtProcWinGetProcessTokenHandle(DWORD dwPid, PSID pSid, DWORD idDesiredSession, PHANDLE phToken)
{
    AssertPtr(pSid);
    AssertPtr(phToken);

    int     rc;
    HANDLE  hProc = OpenProcess(MAXIMUM_ALLOWED, TRUE, dwPid);
    if (hProc != NULL)
    {
        HANDLE hTokenProc;
        if (OpenProcessToken(hProc,
                             TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE
                             | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_SESSIONID | TOKEN_READ | TOKEN_WRITE,
                             &hTokenProc))
        {
            /*
             * Query the user SID from the token.
             */
            SetLastError(NO_ERROR);
            DWORD   dwSize = 0;
            BOOL    fRc    = GetTokenInformation(hTokenProc, TokenUser, NULL, 0, &dwSize);
            DWORD   dwErr  = GetLastError();
            if (   !fRc
                && dwErr == ERROR_INSUFFICIENT_BUFFER
                && dwSize > 0)
            {
                PTOKEN_USER pTokenUser = (PTOKEN_USER)RTMemTmpAllocZ(dwSize);
                if (pTokenUser)
                {
                    if (GetTokenInformation(hTokenProc, TokenUser, pTokenUser, dwSize, &dwSize))
                    {
                        /*
                         * Match token user with the user we're want to create a process as.
                         */
                        if (   IsValidSid(pTokenUser->User.Sid)
                            && EqualSid(pTokenUser->User.Sid, pSid))
                        {
                            /*
                             * Do we need to match the session ID?
                             */
                            rc = VINF_SUCCESS;
                            if (idDesiredSession != UINT32_MAX)
                            {
                                DWORD idCurSession = UINT32_MAX;
                                if (GetTokenInformation(hTokenProc, TokenSessionId, &idCurSession, sizeof(DWORD), &dwSize))
                                    rc = idDesiredSession == idCurSession ? VINF_SUCCESS : VERR_NOT_FOUND;
                                else
                                    rc = RTErrConvertFromWin32(GetLastError());
                            }
                            if (RT_SUCCESS(rc))
                            {
                                /*
                                 * Got a match.  Duplicate the token.  This duplicated token will
                                 * be used for the actual CreateProcessAsUserW() call then.
                                 */
                                rc = rtProcWinDuplicateToken(hTokenProc, phToken);
                            }
                        }
                        else
                            rc = VERR_NOT_FOUND;
                    }
                    else
                        rc = RTErrConvertFromWin32(GetLastError());
                    RTMemTmpFree(pTokenUser);
                }
                else
                    rc = VERR_NO_MEMORY;
            }
            else if (fRc || dwErr == NO_ERROR)
                rc = VERR_IPE_UNEXPECTED_STATUS;
            else
                rc = RTErrConvertFromWin32(dwErr);
            CloseHandle(hTokenProc);
        }
        else
            rc = RTErrConvertFromWin32(GetLastError());
        CloseHandle(hProc);
    }
    else
        rc = RTErrConvertFromWin32(GetLastError());
    return rc;
}


/**
 * Fallback method for rtProcWinFindTokenByProcess that uses the older NT4
 * PSAPI.DLL API.
 *
 * @returns Success indicator.
 * @param   papszNames      The process candidates, in prioritized order.
 * @param   pSid            The secure identifier of the user.
 * @param   phToken         Where to return the token handle - duplicate,
 *                          caller closes it on success.
 *
 * @remarks NT4 needs a copy of "PSAPI.dll" (redistributed by Microsoft and not
 *          part of the OS) in order to get a lookup.  If we don't have this DLL
 *          we are not able to get a token and therefore no UI will be visible.
 */
static bool rtProcWinFindTokenByProcessAndPsApi(const char * const *papszNames, PSID pSid, PHANDLE phToken)
{
    /*
     * Load PSAPI.DLL and resolve the two symbols we need.
     */
    if (   !g_pfnGetModuleBaseNameW
        || !g_pfnEnumProcesses)
        return false;

    /*
     * Get a list of PID.  We retry if it looks like there are more PIDs
     * to be returned than what we supplied buffer space for.
     */
    bool   fFound          = false;
    int    rc              = VINF_SUCCESS;
    DWORD  cbPidsAllocated = 4096;
    DWORD  cbPidsReturned  = 0; /* (MSC maybe used uninitialized) */
    DWORD *paPids;
    for (;;)
    {
        paPids = (DWORD *)RTMemTmpAlloc(cbPidsAllocated);
        AssertBreakStmt(paPids, rc = VERR_NO_TMP_MEMORY);
        cbPidsReturned = 0;
        if (!g_pfnEnumProcesses(paPids, cbPidsAllocated, &cbPidsReturned))
        {
            rc = RTErrConvertFromWin32(GetLastError());
            AssertMsgFailedBreak(("%Rrc\n", rc));
        }
        if (   cbPidsReturned < cbPidsAllocated
            || cbPidsAllocated >= _512K)
            break;
        RTMemTmpFree(paPids);
        cbPidsAllocated *= 2;
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Search for the process.
         *
         * We ASSUME that the caller won't be specifying any names longer
         * than RTPATH_MAX.
         */
        PRTUTF16 pwszProcName = (PRTUTF16)RTMemTmpAllocZ(RTPATH_MAX * sizeof(pwszProcName[0]));
        if (pwszProcName)
        {
            for (size_t i = 0; papszNames[i] && !fFound; i++)
            {
                const DWORD cPids = cbPidsReturned / sizeof(DWORD);
                for (DWORD iPid = 0; iPid < cPids && !fFound; iPid++)
                {
                    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, paPids[iPid]);
                    if (hProc)
                    {
                        *pwszProcName = '\0';
                        DWORD cbRet = g_pfnGetModuleBaseNameW(hProc, 0 /*hModule = exe */, pwszProcName, RTPATH_MAX);
                        if (   cbRet > 0
                            && RTUtf16ICmpAscii(pwszProcName, papszNames[i]) == 0
                            && RT_SUCCESS(rtProcWinGetProcessTokenHandle(paPids[iPid], pSid, UINT32_MAX, phToken)))
                            fFound = true;
                        CloseHandle(hProc);
                    }
                }
            }
            RTMemTmpFree(pwszProcName);
        }
        else
            rc = VERR_NO_TMP_MEMORY;
    }
    RTMemTmpFree(paPids);

    return fFound;
}


/**
 * Finds a one of the processes in @a papszNames running with user @a pSid and possibly
 * in the required windows session. Returns a duplicate handle to its token.
 *
 * @returns Success indicator.
 * @param   papszNames          The process candidates, in prioritized order.
 * @param   pSid                The secure identifier of the user.
 * @param   idDesiredSession    The session the process candidate should
 *                              belong to if possible, UINT32_MAX if anything
 *                              goes.
 * @param   phToken             Where to return the token handle - duplicate,
 *                              caller closes it on success.
 */
static bool rtProcWinFindTokenByProcess(const char * const *papszNames, PSID pSid, uint32_t idDesiredSession, PHANDLE phToken)
{
    AssertPtr(papszNames);
    AssertPtr(pSid);
    AssertPtr(phToken);

    bool fFound = false;

    /*
     * On modern systems (W2K+) try the Toolhelp32 API first; this is more stable
     * and reliable.  Fallback to EnumProcess on NT4.
     */
    bool fFallback = true;
    if (g_pfnProcess32NextW && g_pfnProcess32FirstW && g_pfnCreateToolhelp32Snapshot)
    {
        HANDLE hSnap = g_pfnCreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        Assert(hSnap != INVALID_HANDLE_VALUE);
        if (hSnap != INVALID_HANDLE_VALUE)
        {
            fFallback = false;
            for (size_t i = 0; papszNames[i] && !fFound; i++)
            {
                PROCESSENTRY32W ProcEntry;
                ProcEntry.dwSize = sizeof(ProcEntry);
                ProcEntry.szExeFile[0] = '\0';
                if (g_pfnProcess32FirstW(hSnap, &ProcEntry))
                {
                    do
                    {
                        if (RTUtf16ICmpAscii(ProcEntry.szExeFile, papszNames[i]) == 0)
                        {
                            int rc = rtProcWinGetProcessTokenHandle(ProcEntry.th32ProcessID, pSid, idDesiredSession, phToken);
                            if (RT_SUCCESS(rc))
                            {
                                fFound = true;
                                break;
                            }
                        }
                    } while (g_pfnProcess32NextW(hSnap, &ProcEntry));
                }
                else
                    AssertMsgFailed(("dwErr=%u (%x)\n", GetLastError(), GetLastError()));
            }
            CloseHandle(hSnap);
        }
    }

    /* If we couldn't take a process snapshot for some reason or another, fall
       back on the NT4 compatible API. */
    if (fFallback)
        fFound = rtProcWinFindTokenByProcessAndPsApi(papszNames, pSid, phToken);
    return fFound;
}


/**
 * Logs on a specified user and returns its primary token.
 *
 * @returns IPRT status code.
 * @param   pwszUser            User name. A domain name can be specified (as part of a UPN, User Principal Name),
 *                              e.g. "joedoe@example.com".
 * @param   pwszPassword        Password.
 * @param   phToken             Pointer to store the logon token.
 */
static int rtProcWinUserLogon(PRTUTF16 pwszUser, PRTUTF16 pwszPassword, HANDLE *phToken)
{
    AssertPtrReturn(pwszUser,     VERR_INVALID_POINTER);
    AssertPtrReturn(pwszPassword, VERR_INVALID_POINTER);
    AssertPtrReturn(phToken,      VERR_INVALID_POINTER);
    if (!g_pfnLogonUserW)
        return VERR_NOT_SUPPORTED;

    /*
     * Because we have to deal with http://support.microsoft.com/kb/245683
     * for NULL domain names when running on NT4 here, pass an empty string if so.
     * However, passing FQDNs should work!
     *
     * The SE_TCB_NAME (Policy: Act as part of the operating system) right
     * is required on older windows versions (NT4, W2K, possibly XP).
     */
    PCRTUTF16 pwszDomainNone = g_enmWinVer < kRTWinOSType_2K ? L"" /* NT4 and older */ : NULL /* Windows 2000 and up */;
    BOOL fRc = g_pfnLogonUserW(pwszUser,
                               /* The domain always is passed as part of the UPN (user name). */
                               pwszDomainNone,
                               pwszPassword,
                               LOGON32_LOGON_INTERACTIVE,
                               LOGON32_PROVIDER_DEFAULT,
                               phToken);
    if (fRc)
        return VINF_SUCCESS;

    DWORD dwErr = GetLastError();
    int rc = dwErr == ERROR_PRIVILEGE_NOT_HELD ? VERR_PROC_TCB_PRIV_NOT_HELD : RTErrConvertFromWin32(dwErr);
    if (rc == VERR_UNRESOLVED_ERROR)
        LogRelFunc(("dwErr=%u (%#x), rc=%Rrc\n", dwErr, dwErr, rc));
    return rc;
}


/**
 * Returns the environment to use for the child process.
 *
 * This implements the RTPROC_FLAGS_ENV_CHANGE_RECORD and environment related
 * parts of RTPROC_FLAGS_PROFILE.
 *
 * @returns IPRT status code.
 * @param   hToken      The user token to use if RTPROC_FLAGS_PROFILE is given.
 *                      The caller must have loaded profile for this.
 * @param   hEnv        The environment passed in by the RTProcCreateEx caller.
 * @param   fFlags      The process creation flags passed in by the
 *                      RTProcCreateEx caller (RTPROC_FLAGS_XXX).
 * @param   phEnv       Where to return the environment to use.  This can either
 *                      be a newly created environment block or @a hEnv.  In the
 *                      former case, the caller must destroy it.
 */
static int rtProcWinCreateEnvFromToken(HANDLE hToken, RTENV hEnv, uint32_t fFlags, PRTENV phEnv)
{
    int rc;

    /*
     * Query the environment from the user profile associated with the token if
     * the caller has specified it directly or indirectly.
     */
    if (   (fFlags & RTPROC_FLAGS_PROFILE)
        && (   hEnv == RTENV_DEFAULT
            || (fFlags & RTPROC_FLAGS_ENV_CHANGE_RECORD)) )
    {
        if (g_pfnCreateEnvironmentBlock && g_pfnDestroyEnvironmentBlock)
        {
            LPVOID pvEnvBlockProfile = NULL;
            if (g_pfnCreateEnvironmentBlock(&pvEnvBlockProfile, hToken, FALSE /* Don't inherit from parent. */))
            {
                rc = RTEnvCloneUtf16Block(phEnv, (PCRTUTF16)pvEnvBlockProfile, 0 /*fFlags*/);
                if (   (fFlags & RTPROC_FLAGS_ENV_CHANGE_RECORD)
                    && RT_SUCCESS(rc)
                    && hEnv != RTENV_DEFAULT)
                {
                    rc = RTEnvApplyChanges(*phEnv, hEnv);
                    if (RT_FAILURE(rc))
                        RTEnvDestroy(*phEnv);
                }
                g_pfnDestroyEnvironmentBlock(pvEnvBlockProfile);
            }
            else
                rc = RTErrConvertFromWin32(GetLastError());
        }
        else
            rc = VERR_SYMBOL_NOT_FOUND;
    }
    /*
     * We we've got an incoming change record, combine it with the default environment.
     */
    else if (hEnv != RTENV_DEFAULT && (fFlags & RTPROC_FLAGS_ENV_CHANGE_RECORD))
    {
        rc = RTEnvClone(phEnv, RTENV_DEFAULT);
        if (RT_SUCCESS(rc))
        {
            rc = RTEnvApplyChanges(*phEnv, hEnv);
            if (RT_FAILURE(rc))
                RTEnvDestroy(*phEnv);
        }
    }
    /*
     * Otherwise we can return the incoming environment directly.
     */
    else
    {
        *phEnv = hEnv;
        rc = VINF_SUCCESS;
    }

    return rc;
}


/**
 * Figures which privilege we're missing for success application of
 * CreateProcessAsUserW.
 *
 * @returns IPRT error status.
 */
static int rtProcWinFigureWhichPrivilegeNotHeld2(void)
{
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hToken))
    {
        static struct
        {
            const char *pszName;
            int         rc;
        } const s_aPrivileges[] =
        {
            { SE_TCB_NAME,                      VERR_PROC_TCB_PRIV_NOT_HELD },
            { SE_ASSIGNPRIMARYTOKEN_NAME,       VERR_PROC_APT_PRIV_NOT_HELD },
            { SE_INCREASE_QUOTA_NAME,           VERR_PROC_IQ_PRIV_NOT_HELD  },
        };
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aPrivileges); i++)
        {
            union
            {
                TOKEN_PRIVILEGES TokPriv;
                char abAlloced[sizeof(TOKEN_PRIVILEGES) + sizeof(LUID_AND_ATTRIBUTES)];
            } uNew, uOld;
            uNew.TokPriv.PrivilegeCount = 1;
            uNew.TokPriv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AssertContinue(LookupPrivilegeValue(NULL, s_aPrivileges[i].pszName, &uNew.TokPriv.Privileges[0].Luid));
            uOld = uNew;
            SetLastError(NO_ERROR);
            DWORD cbActual = RT_UOFFSETOF(TOKEN_PRIVILEGES, Privileges[1]);
            AdjustTokenPrivileges(hToken, FALSE /*fDisableAllPrivileges*/, &uNew.TokPriv, cbActual, &uOld.TokPriv, &cbActual);
            if (GetLastError() != NO_ERROR)
            {
                CloseHandle(hToken);
                return s_aPrivileges[i].rc;
            }
            if (uOld.TokPriv.Privileges[0].Attributes == 0)
                AdjustTokenPrivileges(hToken, FALSE /*fDisableAllPrivileges*/, &uOld.TokPriv, 0, NULL, NULL);
        }
        AssertFailed();
        CloseHandle(hToken);
    }
    else
        AssertFailed();
    return VERR_PRIVILEGE_NOT_HELD;
}

#if 0 /* debug code */

static char *rtProcWinSidToString(char *psz, PSID pSid)
{
     char *pszRet = psz;

     *psz++ = 'S';
     *psz++ = '-';
     *psz++ = '1';
     *psz++ = '-';

     PISID pISid = (PISID)pSid;

     psz += RTStrFormatU32(psz, 32, RT_MAKE_U32_FROM_U8(pISid->IdentifierAuthority.Value[5],
                                                        pISid->IdentifierAuthority.Value[4],
                                                        pISid->IdentifierAuthority.Value[3],
                                                        pISid->IdentifierAuthority.Value[2]),
                           10, 0, 0, 0);
     for (unsigned i = 0; i < pISid->SubAuthorityCount; i++)
     {
          *psz++ = '-';
          psz += RTStrFormatU32(psz, 32, pISid->SubAuthority[i], 10, 0, 0, 0);
     }
     *psz++ = '\0';
     return pszRet;
}

static void rtProcWinLogAcl(PACL pAcl)
{
    if (!pAcl)
        RTAssertMsg2("ACL is NULL\n");
    else
    {
        RTAssertMsg2("AceCount=%d AclSize=%#x AclRevision=%d\n", pAcl->AceCount, pAcl->AclSize, pAcl->AclRevision);
        for (uint32_t i = 0; i < pAcl->AceCount; i++)
        {
            PACE_HEADER pAceHdr = NULL;
            if (GetAce(pAcl, i, (PVOID *)&pAceHdr))
            {
                RTAssertMsg2(" ACE[%u]: Flags=%#x Type=%#x Size=%#x", i, pAceHdr->AceFlags, pAceHdr->AceType, pAceHdr->AceSize);
                char szTmp[256];
                if (pAceHdr->AceType == ACCESS_ALLOWED_ACE_TYPE)
                    RTAssertMsg2(" Mask=%#x %s\n", ((ACCESS_ALLOWED_ACE *)pAceHdr)->Mask,
                                 rtProcWinSidToString(szTmp, &((ACCESS_ALLOWED_ACE *)pAceHdr)->SidStart));
                else
                    RTAssertMsg2(" ACE[%u]: Flags=%#x Type=%#x Size=%#x\n", i, pAceHdr->AceFlags, pAceHdr->AceType, pAceHdr->AceSize);
            }
        }
    }
}

static bool rtProcWinLogSecAttr(HANDLE hUserObj)
{
    /*
     * Get the security descriptor for the user interface object.
     */
    uint32_t             cbSecDesc = _64K;
    PSECURITY_DESCRIPTOR pSecDesc  = (PSECURITY_DESCRIPTOR)RTMemTmpAlloc(cbSecDesc);
    SECURITY_INFORMATION SecInfo   = DACL_SECURITY_INFORMATION;
    DWORD                cbNeeded;
    AssertReturn(pSecDesc, false);
    AssertReturn(g_pfnGetUserObjectSecurity, false);
    if (!g_pfnGetUserObjectSecurity(hUserObj, &SecInfo, pSecDesc, cbSecDesc, &cbNeeded))
    {
        RTMemTmpFree(pSecDesc);
        AssertReturn(GetLastError() == ERROR_INSUFFICIENT_BUFFER, false);
        cbSecDesc = cbNeeded + 128;
        pSecDesc  = (PSECURITY_DESCRIPTOR)RTMemTmpAlloc(cbSecDesc);
        AssertReturn(pSecDesc, false);
        if (!g_pfnGetUserObjectSecurity(hUserObj, &SecInfo, pSecDesc, cbSecDesc, &cbNeeded))
        {
            RTMemTmpFree(pSecDesc);
            AssertFailedReturn(false);
        }
    }

    /*
     * Get the discretionary access control list (if we have one).
     */
    BOOL fDaclDefaulted;
    BOOL fDaclPresent;
    PACL pDacl;
    if (GetSecurityDescriptorDacl(pSecDesc, &fDaclPresent, &pDacl, &fDaclDefaulted))
        rtProcWinLogAcl(pDacl);
    else
        RTAssertMsg2("GetSecurityDescriptorDacl failed\n");

    RTMemFree(pSecDesc);
    return true;
}

#endif /* debug */

/**
 * Get the user SID from a token.
 *
 * @returns Pointer to the SID on success. Free by calling RTMemFree.
 * @param   hToken              The token..
 * @param   prc                 Optional return code.
 */
static PSID rtProcWinGetTokenUserSid(HANDLE hToken, int *prc)
{
    int rcIgn;
    if (!prc)
        prc = &rcIgn;
    *prc = VERR_NO_MEMORY;

    /*
     * Get the groups associated with the token.  We just try a size first then
     * reallocates if it's insufficient.
     */
    DWORD       cbUser = _1K;
    PTOKEN_USER pUser  = (PTOKEN_USER)RTMemTmpAlloc(cbUser);
    AssertReturn(pUser, NULL);
    DWORD cbNeeded = 0;
    if (!GetTokenInformation(hToken, TokenUser, pUser, cbUser, &cbNeeded))
    {
        DWORD dwErr = GetLastError();
        RTMemTmpFree(pUser);
        AssertLogRelMsgReturnStmt(dwErr == ERROR_INSUFFICIENT_BUFFER,
                                  ("rtProcWinGetTokenUserSid: GetTokenInformation failed with %u\n", dwErr),
                                  *prc = RTErrConvertFromWin32(dwErr), NULL);
        cbUser = cbNeeded + 128;
        pUser = (PTOKEN_USER)RTMemTmpAlloc(cbUser);
        AssertReturn(pUser, NULL);
        if (!GetTokenInformation(hToken, TokenUser, pUser, cbUser, &cbNeeded))
        {
            dwErr = GetLastError();
            *prc = RTErrConvertFromWin32(dwErr);
            RTMemTmpFree(pUser);
            AssertLogRelMsgFailedReturn(("rtProcWinGetTokenUserSid: GetTokenInformation failed with %u\n", dwErr), NULL);
        }
    }

    DWORD cbSid = GetLengthSid(pUser->User.Sid);
    PSID pSidRet = RTMemDup(pUser->User.Sid, cbSid);
    Assert(pSidRet);
    RTMemTmpFree(pUser);
    *prc = VINF_SUCCESS;
    return pSidRet;
}


#if 0 /* not used */
/**
 * Get the login SID from a token.
 *
 * @returns Pointer to the SID on success. Free by calling RTMemFree.
 * @param   hToken              The token..
 */
static PSID rtProcWinGetTokenLogonSid(HANDLE hToken)
{
    /*
     * Get the groups associated with the token.  We just try a size first then
     * reallocates if it's insufficient.
     */
    DWORD         cbGroups = _1K;
    PTOKEN_GROUPS pGroups = (PTOKEN_GROUPS)RTMemTmpAlloc(cbGroups);
    AssertReturn(pGroups, NULL);
    DWORD cbNeeded = 0;
    if (!GetTokenInformation(hToken, TokenGroups, pGroups, cbGroups, &cbNeeded))
    {
        RTMemTmpFree(pGroups);
        AssertReturn(GetLastError() == ERROR_INSUFFICIENT_BUFFER, NULL);
        cbGroups = cbNeeded + 128;
        pGroups = (PTOKEN_GROUPS)RTMemTmpAlloc(cbGroups);
        AssertReturn(pGroups, NULL);
        if (!GetTokenInformation(hToken, TokenGroups, pGroups, cbGroups, &cbNeeded))
        {
            RTMemTmpFree(pGroups);
            AssertFailedReturn(NULL);
        }
    }

    /*
     * Locate the logon sid.
     */
    PSID     pSidRet = NULL;
    uint32_t i = pGroups->GroupCount;
    while (i-- > 0)
        if ((pGroups->Groups[i].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
        {
            DWORD cbSid = GetLengthSid(pGroups->Groups[i].Sid);
            pSidRet = RTMemDup(pGroups->Groups[i].Sid, cbSid);
            break;
        }

    RTMemTmpFree(pGroups);
    Assert(pSidRet);
    return pSidRet;
}
#endif /* unused */


/**
 * Retrieves the DACL security descriptor of the give GUI object.
 *
 * @returns Pointer to the security descriptor.
 * @param   hUserObj        The GUI object handle.
 * @param   pcbSecDesc      Where to return the size of the security descriptor.
 * @param   ppDacl          Where to return the DACL pointer.
 * @param   pfDaclPresent   Where to return the DACL-present indicator.
 * @param   pDaclSizeInfo   Where to return the DACL size information.
 */
static PSECURITY_DESCRIPTOR rtProcWinGetUserObjDacl(HANDLE hUserObj, uint32_t *pcbSecDesc, PACL *ppDacl,
                                                    BOOL *pfDaclPresent, ACL_SIZE_INFORMATION *pDaclSizeInfo)
{
    /*
     * Get the security descriptor for the user interface object.
     */
    uint32_t             cbSecDesc = _1K;
    PSECURITY_DESCRIPTOR pSecDesc  = (PSECURITY_DESCRIPTOR)RTMemTmpAlloc(cbSecDesc);
    SECURITY_INFORMATION SecInfo   = DACL_SECURITY_INFORMATION;
    DWORD                cbNeeded;
    AssertReturn(pSecDesc, NULL);
    AssertReturn(g_pfnGetUserObjectSecurity, NULL);
    if (!g_pfnGetUserObjectSecurity(hUserObj, &SecInfo, pSecDesc, cbSecDesc, &cbNeeded))
    {
        RTMemTmpFree(pSecDesc);
        AssertReturn(GetLastError() == ERROR_INSUFFICIENT_BUFFER, NULL);
        cbSecDesc = cbNeeded + 128;
        pSecDesc  = (PSECURITY_DESCRIPTOR)RTMemTmpAlloc(cbSecDesc);
        AssertReturn(pSecDesc, NULL);
        if (!g_pfnGetUserObjectSecurity(hUserObj, &SecInfo, pSecDesc, cbSecDesc, &cbNeeded))
        {
            RTMemTmpFree(pSecDesc);
            AssertFailedReturn(NULL);
        }
    }
    *pcbSecDesc = cbNeeded;

    /*
     * Get the discretionary access control list (if we have one).
     */
    BOOL fDaclDefaulted;
    if (GetSecurityDescriptorDacl(pSecDesc, pfDaclPresent, ppDacl, &fDaclDefaulted))
    {
        RT_ZERO(*pDaclSizeInfo);
        pDaclSizeInfo->AclBytesInUse = sizeof(ACL);
        if (   !*ppDacl
            || GetAclInformation(*ppDacl, pDaclSizeInfo, sizeof(*pDaclSizeInfo), AclSizeInformation))
            return pSecDesc;
        AssertFailed();
    }
    else
        AssertFailed();
    RTMemTmpFree(pSecDesc);
    return NULL;
}


/**
 * Copy ACEs from one ACL to another.
 *
 * @returns true on success, false on failure.
 * @param   pDst                The destination ACL.
 * @param   pSrc                The source ACL.
 * @param   cAces               The number of ACEs to copy.
 */
static bool rtProcWinCopyAces(PACL pDst, PACL pSrc, uint32_t cAces)
{
    for (uint32_t i = 0; i < cAces; i++)
    {
        PACE_HEADER pAceHdr;
        AssertReturn(GetAce(pSrc, i, (PVOID *)&pAceHdr), false);
        AssertReturn(AddAce(pDst, ACL_REVISION, MAXDWORD, pAceHdr, pAceHdr->AceSize), false);
    }
    return true;
}


/**
 * Adds an access-allowed access control entry to an ACL.
 *
 * @returns true on success, false on failure.
 * @param   pDstAcl             The ACL.
 * @param   fAceFlags           The ACE flags.
 * @param   fMask               The ACE access mask.
 * @param   pSid                The SID to go with the ACE.
 * @param   cbSid               The size of the SID.
 */
static bool rtProcWinAddAccessAllowedAce(PACL pDstAcl, uint32_t fAceFlags, uint32_t fMask, PSID pSid, uint32_t cbSid)
{
    struct
    {
        ACCESS_ALLOWED_ACE  Core;
        DWORD               abPadding[128]; /* More than enough, AFAIK. */
    } AceBuf;
    RT_ZERO(AceBuf);
    uint32_t const cbAllowedAce = RT_UOFFSETOF(ACCESS_ALLOWED_ACE, SidStart) + cbSid;
    AssertReturn(cbAllowedAce <= sizeof(AceBuf), false);

    AceBuf.Core.Header.AceSize     = cbAllowedAce;
    AceBuf.Core.Header.AceType     = ACCESS_ALLOWED_ACE_TYPE;
    AceBuf.Core.Header.AceFlags    = fAceFlags;
    AceBuf.Core.Mask               = fMask;
    AssertReturn(CopySid(cbSid, &AceBuf.Core.SidStart, pSid), false);

    uint32_t i = pDstAcl->AceCount;
    while (i-- > 0)
    {
        PACE_HEADER pAceHdr;
        AssertContinue(GetAce(pDstAcl, i, (PVOID *)&pAceHdr));
        if (   pAceHdr->AceSize == cbAllowedAce
            && memcmp(pAceHdr, &AceBuf.Core, cbAllowedAce) == 0)
            return true;

    }
    AssertMsgReturn(AddAce(pDstAcl, ACL_REVISION, MAXDWORD, &AceBuf.Core, cbAllowedAce), ("%u\n", GetLastError()), false);
    return true;
}


/** All window station rights we know about   */
#define MY_WINSTATION_ALL_RIGHTS (  WINSTA_ACCESSCLIPBOARD | WINSTA_ACCESSGLOBALATOMS | WINSTA_CREATEDESKTOP \
                                  | WINSTA_ENUMDESKTOPS | WINSTA_ENUMERATE | WINSTA_EXITWINDOWS | WINSTA_READATTRIBUTES \
                                  | WINSTA_READSCREEN | WINSTA_WRITEATTRIBUTES | DELETE | READ_CONTROL | WRITE_DAC | WRITE_OWNER )
/** All desktop rights we know about   */
#define MY_DESKTOP_ALL_RIGHTS    (  DESKTOP_CREATEMENU | DESKTOP_CREATEWINDOW | DESKTOP_ENUMERATE | DESKTOP_HOOKCONTROL \
                                  | DESKTOP_JOURNALPLAYBACK | DESKTOP_JOURNALRECORD | DESKTOP_READOBJECTS \
                                  | DESKTOP_SWITCHDESKTOP | DESKTOP_WRITEOBJECTS | DELETE | READ_CONTROL | WRITE_DAC \
                                  | WRITE_OWNER )
/** Generic rights. */
#define MY_GENERIC_ALL_RIGHTS    ( GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL )


/**
 * Grants the given SID full access to the given window station.
 *
 * @returns true on success, false on failure.
 * @param   hWinStation         The window station.
 * @param   pSid                The SID.
 */
static bool rtProcWinAddSidToWinStation(HWINSTA hWinStation, PSID pSid)
{
    bool fRet = false;
    AssertReturn(g_pfnSetUserObjectSecurity, fRet);

    /*
     * Get the current DACL.
     */
    uint32_t                cbSecDesc;
    PACL                    pDacl;
    ACL_SIZE_INFORMATION    DaclSizeInfo;
    BOOL                    fDaclPresent;
    PSECURITY_DESCRIPTOR    pSecDesc = rtProcWinGetUserObjDacl(hWinStation, &cbSecDesc, &pDacl, &fDaclPresent, &DaclSizeInfo);
    if (pSecDesc)
    {
        /*
         * Create a new DACL. This will contain two extra ACEs.
         */
        PSECURITY_DESCRIPTOR pNewSecDesc = (PSECURITY_DESCRIPTOR)RTMemTmpAlloc(cbSecDesc);
        if (   pNewSecDesc
            && InitializeSecurityDescriptor(pNewSecDesc, SECURITY_DESCRIPTOR_REVISION))
        {
            uint32_t const cbSid     = GetLengthSid(pSid);
            uint32_t const cbNewDacl = DaclSizeInfo.AclBytesInUse + (sizeof(ACCESS_ALLOWED_ACE) + cbSid) * 2;
            PACL pNewDacl = (PACL)RTMemTmpAlloc(cbNewDacl);
            if (   pNewDacl
                && InitializeAcl(pNewDacl, cbNewDacl, ACL_REVISION)
                && rtProcWinCopyAces(pNewDacl, pDacl, fDaclPresent ? DaclSizeInfo.AceCount : 0))
            {
                /*
                 * Add the two new SID ACEs.
                 */
                if (   rtProcWinAddAccessAllowedAce(pNewDacl, CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE | OBJECT_INHERIT_ACE,
                                                    MY_GENERIC_ALL_RIGHTS, pSid, cbSid)
                    && rtProcWinAddAccessAllowedAce(pNewDacl, NO_PROPAGATE_INHERIT_ACE, MY_WINSTATION_ALL_RIGHTS, pSid, cbSid))
                {
                    /*
                     * Now mate the new DECL with the security descriptor and set it.
                     */
                    if (SetSecurityDescriptorDacl(pNewSecDesc, TRUE /*fDaclPresent*/, pNewDacl, FALSE /*fDaclDefaulted*/))
                    {
                        SECURITY_INFORMATION SecInfo = DACL_SECURITY_INFORMATION;
                        if (g_pfnSetUserObjectSecurity(hWinStation, &SecInfo, pNewSecDesc))
                            fRet = true;
                        else
                            AssertFailed();
                    }
                    else
                        AssertFailed();
                }
                else
                    AssertFailed();
            }
            else
                AssertFailed();
            RTMemTmpFree(pNewDacl);
        }
        else
            AssertFailed();
        RTMemTmpFree(pNewSecDesc);
        RTMemTmpFree(pSecDesc);
    }
    return fRet;
}


/**
 * Grants the given SID full access to the given desktop.
 *
 * @returns true on success, false on failure.
 * @param   hDesktop            The desktop handle.
 * @param   pSid                The SID.
 */
static bool rtProcWinAddSidToDesktop(HDESK hDesktop, PSID pSid)
{
    bool fRet = false;
    AssertReturn(g_pfnSetUserObjectSecurity, fRet);

    /*
     * Get the current DACL.
     */
    uint32_t                cbSecDesc;
    PACL                    pDacl;
    ACL_SIZE_INFORMATION    DaclSizeInfo;
    BOOL                    fDaclPresent;
    PSECURITY_DESCRIPTOR    pSecDesc = rtProcWinGetUserObjDacl(hDesktop, &cbSecDesc, &pDacl, &fDaclPresent, &DaclSizeInfo);
    if (pSecDesc)
    {
        /*
         * Create a new DACL. This will contain one extra ACE.
         */
        PSECURITY_DESCRIPTOR pNewSecDesc = (PSECURITY_DESCRIPTOR)RTMemTmpAlloc(cbSecDesc);
        if (   pNewSecDesc
            && InitializeSecurityDescriptor(pNewSecDesc, SECURITY_DESCRIPTOR_REVISION))
        {
            uint32_t const cbSid     = GetLengthSid(pSid);
            uint32_t const cbNewDacl = DaclSizeInfo.AclBytesInUse + (sizeof(ACCESS_ALLOWED_ACE) + cbSid) * 1;
            PACL pNewDacl = (PACL)RTMemTmpAlloc(cbNewDacl);
            if (   pNewDacl
                && InitializeAcl(pNewDacl, cbNewDacl, ACL_REVISION)
                && rtProcWinCopyAces(pNewDacl, pDacl, fDaclPresent ? DaclSizeInfo.AceCount : 0))
            {
                /*
                 * Add the new SID ACE.
                 */
                if (rtProcWinAddAccessAllowedAce(pNewDacl, 0 /*fAceFlags*/, MY_DESKTOP_ALL_RIGHTS, pSid, cbSid))
                {
                    /*
                     * Now mate the new DECL with the security descriptor and set it.
                     */
                    if (SetSecurityDescriptorDacl(pNewSecDesc, TRUE /*fDaclPresent*/, pNewDacl, FALSE /*fDaclDefaulted*/))
                    {
                        SECURITY_INFORMATION SecInfo = DACL_SECURITY_INFORMATION;
                        if (g_pfnSetUserObjectSecurity(hDesktop, &SecInfo, pNewSecDesc))
                            fRet = true;
                        else
                            AssertFailed();
                    }
                    else
                        AssertFailed();
                }
                else
                    AssertFailed();
            }
            else
                AssertFailed();
            RTMemTmpFree(pNewDacl);
        }
        else
            AssertFailed();
        RTMemTmpFree(pNewSecDesc);
        RTMemTmpFree(pSecDesc);
    }
    return fRet;
}


/**
 * Preps the window station and desktop for the new app.
 *
 * EXPERIMENTAL. Thus no return code.
 *
 * @param   hTokenToUse     The access token of the new process.
 * @param   pStartupInfo    The startup info (we'll change lpDesktop, maybe).
 * @param   phWinStationOld Where to return an window station handle to restore.
 *                          Pass this to SetProcessWindowStation if not NULL.
 */
static void rtProcWinStationPrep(HANDLE hTokenToUse, STARTUPINFOW *pStartupInfo, HWINSTA *phWinStationOld)
{
    /** @todo Always mess with the interactive one? Maybe it's not there...  */
    *phWinStationOld = GetProcessWindowStation();
    HWINSTA hWinStation0;
    if (g_pfnOpenWindowStationW)
        hWinStation0 = g_pfnOpenWindowStationW(L"winsta0", FALSE /*fInherit*/, READ_CONTROL | WRITE_DAC);
    else
        hWinStation0 = OpenWindowStationA("winsta0", FALSE /*fInherit*/, READ_CONTROL | WRITE_DAC); /* (for NT3.1) */
    if (hWinStation0)
    {
        if (SetProcessWindowStation(hWinStation0))
        {
            HDESK hDesktop = OpenDesktop("default", 0 /*fFlags*/, FALSE /*fInherit*/,
                                         READ_CONTROL | WRITE_DAC | DESKTOP_WRITEOBJECTS | DESKTOP_READOBJECTS);
            if (hDesktop)
            {
                /*PSID pSid = rtProcWinGetTokenLogonSid(hTokenToUse); - Better to use the user SID. Avoid overflowing the ACL. */
                PSID pSid = rtProcWinGetTokenUserSid(hTokenToUse, NULL /*prc*/);
                if (pSid)
                {
                    if (   rtProcWinAddSidToWinStation(hWinStation0, pSid)
                        && rtProcWinAddSidToDesktop(hDesktop, pSid))
                    {
                        pStartupInfo->lpDesktop = (PWSTR)L"winsta0\\default";
                    }
                    RTMemFree(pSid);
                }
                CloseDesktop(hDesktop);
            }
            else
                AssertFailed();
        }
        else
            AssertFailed();
        if (g_pfnCloseWindowStation)
            g_pfnCloseWindowStation(hWinStation0);
    }
    else
        AssertFailed();
}


/**
 * Extracts the user name + domain from a given UPN (User Principal Name, "joedoe@example.com") or
 * Down-Level Logon Name format ("example.com\\joedoe") string.
 *
 * @return  IPRT status code.
 * @param   pwszString      Pointer to string to extract the account info from.
 * @param   pAccountInfo    Where to store the parsed account info.
 *                          Must be free'd with rtProcWinFreeAccountInfo().
 */
static int rtProcWinParseAccountInfo(PRTUTF16 pwszString, PRTPROCWINACCOUNTINFO pAccountInfo)
{
    AssertPtrReturn(pwszString,   VERR_INVALID_POINTER);
    AssertPtrReturn(pAccountInfo, VERR_INVALID_POINTER);

    /*
     * Note: UPN handling is defined in RFC 822. We only implement very rudimentary parsing for the user
     *       name and domain fields though.
     */
    char *pszString;
    int rc = RTUtf16ToUtf8(pwszString, &pszString);
    if (RT_SUCCESS(rc))
    {
        do
        {
            /* UPN or FQDN handling needed? */
            /** @todo Add more validation here as needed. Regular expressions would be nice. */
            char *pszDelim = strchr(pszString, '@');
            if (pszDelim) /* UPN name? */
            {
                rc = RTStrToUtf16Ex(pszString, pszDelim - pszString, &pAccountInfo->pwszUserName, 0, NULL);
                if (RT_FAILURE(rc))
                    break;

                rc = RTStrToUtf16Ex(pszDelim + 1, RTSTR_MAX, &pAccountInfo->pwszDomain, 0, NULL);
                if (RT_FAILURE(rc))
                    break;
            }
            else if (pszDelim = strchr(pszString, '\\')) /* FQDN name? */
            {
                rc = RTStrToUtf16Ex(pszString, pszDelim - pszString, &pAccountInfo->pwszDomain, 0, NULL);
                if (RT_FAILURE(rc))
                    break;

                rc = RTStrToUtf16Ex(pszDelim + 1, RTSTR_MAX, &pAccountInfo->pwszUserName, 0, NULL);
                if (RT_FAILURE(rc))
                    break;
            }
            else
                rc = VERR_NOT_SUPPORTED;

        } while (0);

        RTStrFree(pszString);
    }

#ifdef DEBUG
    LogRelFunc(("Name  : %ls\n", pAccountInfo->pwszUserName));
    LogRelFunc(("Domain: %ls\n", pAccountInfo->pwszDomain));
#endif

    if (RT_FAILURE(rc))
        LogRelFunc(("Parsing \"%ls\" failed with rc=%Rrc\n", pwszString, rc));
    return rc;
}


static void rtProcWinFreeAccountInfo(PRTPROCWINACCOUNTINFO pAccountInfo)
{
    if (!pAccountInfo)
        return;

    if (pAccountInfo->pwszUserName)
    {
        RTUtf16Free(pAccountInfo->pwszUserName);
        pAccountInfo->pwszUserName = NULL;
    }

    if (pAccountInfo->pwszDomain)
    {
        RTUtf16Free(pAccountInfo->pwszDomain);
        pAccountInfo->pwszDomain = NULL;
    }
}


/**
 * Tries to resolve the name of the SID.
 *
 * @returns IPRT status code.
 * @param   pSid        The SID to resolve.
 * @param   ppwszName   Where to return the name. Use RTUtf16Free to free.
 */
static int rtProcWinSidToName(PSID pSid, PRTUTF16 *ppwszName)
{
    *ppwszName = NULL;

    /*
     * Use large initial buffers here to try avoid having to repeat the call.
     */
    DWORD cwcAllocated = 512;
    while (cwcAllocated < _32K)
    {
        PRTUTF16 pwszName = RTUtf16Alloc(cwcAllocated * sizeof(RTUTF16));
        AssertReturn(pwszName, VERR_NO_UTF16_MEMORY);
        PRTUTF16 pwszDomain = RTUtf16Alloc(cwcAllocated * sizeof(RTUTF16));
        AssertReturnStmt(pwszDomain, RTUtf16Free(pwszName), VERR_NO_UTF16_MEMORY);

        DWORD cwcName   = cwcAllocated;
        DWORD cwcDomain = cwcAllocated;
        SID_NAME_USE SidNameUse = SidTypeUser;
        if (LookupAccountSidW(NULL /*lpSystemName*/, pSid, pwszName, &cwcName, pwszDomain, &cwcDomain, &SidNameUse))
        {
            *ppwszName = pwszName;
            RTUtf16Free(pwszDomain); /* may need this later. */
            return VINF_SUCCESS;
        }

        DWORD const dwErr = GetLastError();
        RTUtf16Free(pwszName);
        RTUtf16Free(pwszDomain);
        if (dwErr != ERROR_INSUFFICIENT_BUFFER)
            return RTErrConvertFromWin32(dwErr);
        cwcAllocated = RT_MAX(cwcName, cwcDomain) + 1;
    }

    return RTErrConvertFromWin32(ERROR_INSUFFICIENT_BUFFER);
}


/**
 * Tries to resolve the user name for the token.
 *
 * @returns IPRT status code.
 * @param   hToken      The token.
 * @param   ppwszUser   Where to return the username. Use RTUtf16Free to free.
 */
static int rtProcWinTokenToUsername(HANDLE hToken, PRTUTF16 *ppwszUser)
{
    int rc = VINF_SUCCESS;
    PSID pSid = rtProcWinGetTokenUserSid(hToken, &rc);
    if (pSid)
    {
        rc = rtProcWinSidToName(pSid, ppwszUser);
        RTMemFree(pSid);
    }
    else
        *ppwszUser = NULL;
    return rc;
}


/**
 * Method \#2.
 *
 * @note pwszUser can be NULL when RTPROC_FLAGS_AS_IMPERSONATED_TOKEN is set.
 */
static int rtProcWinCreateAsUser2(PRTUTF16 pwszUser, PRTUTF16 pwszPassword, PRTUTF16 *ppwszExec, PRTUTF16 pwszCmdLine,
                                  RTENV hEnv, DWORD dwCreationFlags,
                                  STARTUPINFOW *pStartupInfo, PROCESS_INFORMATION *pProcInfo,
                                  uint32_t fFlags, const char *pszExec, uint32_t idDesiredSession,
                                  HANDLE hUserToken, PRTUTF16 pwszCwd)
{
    /*
     * So if we want to start a process from a service (RTPROC_FLAGS_SERVICE),
     * we have to do the following:
     * - Check the credentials supplied and get the user SID.
     * - If valid get the correct Explorer/VBoxTray instance corresponding to that
     *   user. This of course is only possible if that user is logged in (over
     *   physical console or terminal services).
     * - If we found the user's Explorer/VBoxTray app, use and modify the token to
     *   use it in order to allow the newly started process to access the user's
     *   desktop. If there's no Explorer/VBoxTray app we cannot display the started
     *   process (but run it without UI).
     *
     * The following restrictions apply:
     * - A process only can show its UI when the user the process should run
     *   under is logged in (has a desktop).
     * - We do not want to display a process of user A run on the desktop
     *   of user B on multi session systems.
     *
     * The following rights are needed in order to use LogonUserW and
     * CreateProcessAsUserW, so the local policy has to be modified to:
     *  - SE_TCB_NAME                = Act as part of the operating system
     *  - SE_ASSIGNPRIMARYTOKEN_NAME = Create/replace a (process) token object
     *  - SE_INCREASE_QUOTA_NAME     = Increase quotas
     *
     * We may fail here with ERROR_PRIVILEGE_NOT_HELD.
     */
    DWORD   dwErr       = NO_ERROR;
    HANDLE  hTokenLogon = INVALID_HANDLE_VALUE;
    int rc = VINF_SUCCESS;
    if (fFlags & RTPROC_FLAGS_TOKEN_SUPPLIED)
        hTokenLogon = hUserToken;
    else if (fFlags & RTPROC_FLAGS_AS_IMPERSONATED_TOKEN)
        rc = rtProcWinGetThreadTokenHandle(GetCurrentThread(), &hTokenLogon);
    else if (pwszUser == NULL)
        rc = rtProcWinGetProcessTokenHandle(GetCurrentProcess(), &hTokenLogon);
    else
        rc = rtProcWinUserLogon(pwszUser, pwszPassword, &hTokenLogon);
    if (RT_SUCCESS(rc))
    {
        BOOL   fRc;
        bool   fFound = false;
        HANDLE hTokenUserDesktop = INVALID_HANDLE_VALUE;

        /*
         * If the SERVICE flag is specified, we do something rather ugly to
         * make things work at all.  We search for a known desktop process
         * belonging to the user, grab its token and use it for launching
         * the new process.  That way the process will have desktop access.
         */
        if (fFlags & RTPROC_FLAGS_SERVICE)
        {
            /*
             * For the token search we need a SID.
             */
            PSID pSid = rtProcWinGetTokenUserSid(hTokenLogon, &rc);

            /*
             * If we got a valid SID, search the running processes.
             */
            /*
             * If we got a valid SID, search the running processes.
             */
            if (pSid)
            {
                if (IsValidSid(pSid))
                {
                    /* Array of process names we want to look for. */
                    static const char * const s_papszProcNames[] =
                    {
#ifdef VBOX             /* The explorer entry is a fallback in case GA aren't installed. */
                        { "VBoxTray.exe" },
# ifndef IN_GUEST
                        { "VirtualBox.exe" },
# endif
#endif
                        { "explorer.exe" },
                        NULL
                    };
                    fFound = rtProcWinFindTokenByProcess(s_papszProcNames, pSid, idDesiredSession, &hTokenUserDesktop);
                    dwErr  = 0;
                }
                else
                {
                    dwErr = GetLastError();
                    LogRelFunc(("SID is invalid: %ld\n", dwErr));
                    rc = dwErr != NO_ERROR ? RTErrConvertFromWin32(dwErr) : VERR_INTERNAL_ERROR_3;
                }

                RTMemFree(pSid);
            }
        }
        /* else: !RTPROC_FLAGS_SERVICE: Nothing to do here right now. */

#if 0
        /*
         * If we make LogonUserW to return an impersonation token, enable this
         * to convert it into a primary token.
         */
        if (!fFound && detect-impersonation-token)
        {
            HANDLE hNewToken;
            if (DuplicateTokenEx(hTokenLogon, MAXIMUM_ALLOWED, NULL /*SecurityAttribs*/,
                                 SecurityIdentification, TokenPrimary, &hNewToken))
            {
                CloseHandle(hTokenLogon);
                hTokenLogon = hNewToken;
            }
            else
                AssertMsgFailed(("%d\n", GetLastError()));
        }
#endif

        if (RT_SUCCESS(rc))
        {
            /*
             * If we didn't find a matching VBoxTray, just use the token we got
             * above from LogonUserW().  This enables us to at least run processes
             * with desktop interaction without UI.
             */
            HANDLE hTokenToUse = fFound ? hTokenUserDesktop : hTokenLogon;
            if (   !(fFlags & RTPROC_FLAGS_PROFILE)
                || (g_pfnUnloadUserProfile && g_pfnLoadUserProfileW) )
            {
                /*
                 * Load the profile, if requested.  (Must be done prior to creating the enviornment.)
                 *
                 * Note! We don't have sufficient rights when impersonating a user, but we can
                 *       ASSUME the user is logged on and has its profile loaded into HKEY_USERS already.
                 */
                PROFILEINFOW ProfileInfo;
                PRTUTF16     pwszUserFree = NULL;
                RT_ZERO(ProfileInfo);
                /** @todo r=bird: We probably don't need to load anything if pwszUser is NULL... */
                if ((fFlags & (RTPROC_FLAGS_PROFILE | RTPROC_FLAGS_AS_IMPERSONATED_TOKEN)) == RTPROC_FLAGS_PROFILE)
                {
                    if (!pwszUser)
                    {
                        Assert(fFlags & RTPROC_FLAGS_AS_IMPERSONATED_TOKEN);
                        rc = rtProcWinTokenToUsername(hTokenToUse, &pwszUserFree);
                        pwszUser = pwszUserFree;
                    }
                    if (RT_SUCCESS(rc))
                    {
                        ProfileInfo.dwSize     = sizeof(ProfileInfo);
                        ProfileInfo.dwFlags    = PI_NOUI; /* Prevents the display of profile error messages. */
                        ProfileInfo.lpUserName = pwszUser;
                        if (!g_pfnLoadUserProfileW(hTokenToUse, &ProfileInfo))
                            rc = RTErrConvertFromWin32(GetLastError());
                    }
                }
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Create the environment.
                     */
                    RTENV hEnvFinal;
                    rc = rtProcWinCreateEnvFromToken(hTokenToUse, hEnv, fFlags, &hEnvFinal);
                    if (RT_SUCCESS(rc))
                    {
                        PRTUTF16 pwszzBlock;
                        rc = RTEnvQueryUtf16Block(hEnvFinal, &pwszzBlock);
                        if (RT_SUCCESS(rc))
                        {
                            rc = rtProcWinFindExe(fFlags, hEnv, pszExec, ppwszExec);
                            if (RT_SUCCESS(rc))
                            {
                                HWINSTA hOldWinStation = NULL;
                                if (   !fFound
                                    && g_enmWinVer <= kRTWinOSType_NT4) /** @todo test newer versions... */
                                    rtProcWinStationPrep(hTokenToUse, pStartupInfo, &hOldWinStation);

                                /*
                                 * Useful KB articles:
                                 *      http://support.microsoft.com/kb/165194/
                                 *      http://support.microsoft.com/kb/184802/
                                 *      http://support.microsoft.com/kb/327618/
                                 */
                                if (g_pfnCreateProcessAsUserW)
                                {
                                    fRc = g_pfnCreateProcessAsUserW(hTokenToUse,
                                                                    *ppwszExec,
                                                                    pwszCmdLine,
                                                                    NULL,         /* pProcessAttributes */
                                                                    NULL,         /* pThreadAttributes */
                                                                    TRUE,         /* fInheritHandles */
                                                                    dwCreationFlags,
                                                                    /** @todo Warn about exceeding 8192 bytes
                                                                     *        on XP and up. */
                                                                    pwszzBlock,   /* lpEnvironment */
                                                                    pwszCwd,       /* pCurrentDirectory */
                                                                    pStartupInfo,
                                                                    pProcInfo);
                                    if (fRc)
                                        rc = VINF_SUCCESS;
                                    else
                                    {
                                        dwErr = GetLastError();
                                        if (dwErr == ERROR_PRIVILEGE_NOT_HELD)
                                            rc = rtProcWinFigureWhichPrivilegeNotHeld2();
                                        else
                                            rc = RTErrConvertFromWin32(dwErr);
                                    }
                                }
                                else
                                    rc = VERR_NOT_SUPPORTED;

                                if (hOldWinStation)
                                    SetProcessWindowStation(hOldWinStation);
                            }
                            RTEnvFreeUtf16Block(pwszzBlock);
                        }

                        if (hEnvFinal != hEnv)
                            RTEnvDestroy(hEnvFinal);
                    }

                    if ((fFlags & RTPROC_FLAGS_PROFILE) && ProfileInfo.hProfile)
                    {
                        fRc = g_pfnUnloadUserProfile(hTokenToUse, ProfileInfo.hProfile);
#ifdef RT_STRICT
                        if (!fRc)
                        {
                            DWORD dwErr2 = GetLastError();
                            AssertMsgFailed(("Unloading user profile failed with error %u (%#x) - Are all handles closed? (dwErr=%u)",
                                             dwErr2, dwErr2, dwErr));
                        }
#endif
                    }
                    if (pwszUserFree)
                        RTUtf16Free(pwszUserFree);
                }
            }
            else
                rc = VERR_SYMBOL_NOT_FOUND;
        } /* Account lookup succeeded? */

        if (hTokenUserDesktop != INVALID_HANDLE_VALUE)
            CloseHandle(hTokenUserDesktop);
        if (   !(fFlags & RTPROC_FLAGS_TOKEN_SUPPLIED)
            && hTokenLogon != INVALID_HANDLE_VALUE)
            CloseHandle(hTokenLogon);

        if (rc == VERR_UNRESOLVED_ERROR)
            LogRelFunc(("dwErr=%u (%#x), rc=%Rrc\n", dwErr, dwErr, rc));
    }

    return rc;
}


/**
 * Plants a standard handle into a child process on older windows versions.
 *
 * This is only needed when using CreateProcessWithLogonW on older windows
 * versions.  It would appear that newer versions of windows does this for us.
 *
 * @param   hSrcHandle              The source handle.
 * @param   hDstProcess             The child process handle.
 * @param   offProcParamMember      The offset to RTL_USER_PROCESS_PARAMETERS.
 * @param   ppvDstProcParamCache    Where where cached the address of
 *                                  RTL_USER_PROCESS_PARAMETERS in the child.
 */
static void rtProcWinDupStdHandleIntoChild(HANDLE hSrcHandle, HANDLE hDstProcess, uint32_t offProcParamMember,
                                           PVOID *ppvDstProcParamCache)
{
    if (hSrcHandle != NULL && hSrcHandle != INVALID_HANDLE_VALUE)
    {
        HANDLE hDstHandle;
        if (DuplicateHandle(GetCurrentProcess(), hSrcHandle, hDstProcess, &hDstHandle,
                            0 /*IgnoredDesiredAccess*/, FALSE /*fInherit*/, DUPLICATE_SAME_ACCESS))
        {
            if (hSrcHandle == hDstHandle)
                return;

            if (!*ppvDstProcParamCache)
            {
                PROCESS_BASIC_INFORMATION BasicInfo;
                ULONG cbIgn;
                NTSTATUS rcNt = NtQueryInformationProcess(hDstProcess, ProcessBasicInformation,
                                                          &BasicInfo, sizeof(BasicInfo), &cbIgn);
                if (NT_SUCCESS(rcNt))
                {
                    SIZE_T cbCopied = 0;
                    if (!ReadProcessMemory(hDstProcess,
                                           (char *)BasicInfo.PebBaseAddress + RT_UOFFSETOF(PEB_COMMON, ProcessParameters),
                                           ppvDstProcParamCache, sizeof(*ppvDstProcParamCache), &cbCopied))
                    {
                        AssertMsgFailed(("PebBaseAddress=%p %d\n", BasicInfo.PebBaseAddress, GetLastError()));
                        *ppvDstProcParamCache = NULL;
                    }
                }
                else
                    AssertMsgFailed(("rcNt=%#x\n", rcNt));
            }
            if (*ppvDstProcParamCache)
            {
                if (WriteProcessMemory(hDstProcess, (char *)*ppvDstProcParamCache + offProcParamMember,
                                       &hDstHandle, sizeof(hDstHandle), NULL))
                    return;
            }

            /*
             * Close the handle.
             */
            HANDLE hSrcHandle2;
            if (DuplicateHandle(hDstProcess, hDstHandle, GetCurrentProcess(), &hSrcHandle2,
                                0 /*IgnoredDesiredAccess*/, FALSE /*fInherit*/, DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE))
                CloseHandle(hSrcHandle2);
            else
                AssertMsgFailed(("hDstHandle=%p %u\n", hDstHandle, GetLastError()));
        }
        else
            AssertMsg(GetLastError() == ERROR_INVALID_PARAMETER, ("%u\n", GetLastError()));
    }
}


/**
 * Method \#1.
 *
 * This method requires Windows 2000 or later.  It may fail if the process is
 * running under the SYSTEM account (like a service, ERROR_ACCESS_DENIED) on
 * newer platforms (however, this works on W2K!).
 */
static int rtProcWinCreateAsUser1(PRTUTF16 pwszUser, PRTUTF16 pwszPassword, PRTUTF16 *ppwszExec, PRTUTF16 pwszCmdLine,
                                  RTENV hEnv, DWORD dwCreationFlags,
                                  STARTUPINFOW *pStartupInfo, PROCESS_INFORMATION *pProcInfo,
                                  uint32_t fFlags, const char *pszExec, PRTUTF16 pwszCwd)
{
    /* The CreateProcessWithLogonW API was introduced with W2K and later.  It uses a service
       for launching the process. */
    if (!g_pfnCreateProcessWithLogonW)
        return VERR_SYMBOL_NOT_FOUND;

    /*
     * Create the environment block and find the executable first.
     *
     * We try to skip this when RTPROC_FLAGS_PROFILE is set so we can sidestep
     * potential missing TCB privilege issues when calling UserLogonW.  At least
     * NT4 and W2K requires the trusted code base (TCB) privilege for logon use.
     * Passing pwszzBlock=NULL and LOGON_WITH_PROFILE means the child process
     * gets the environment specified by the user profile.
     */
    int      rc;
    PRTUTF16 pwszzBlock = NULL;

    /* Eliminating the path search flags simplifies things a little. */
    if (   (fFlags & RTPROC_FLAGS_SEARCH_PATH)
        && (RTPathHasPath(pszExec) || RTPathExists(pszExec)))
        fFlags &= ~RTPROC_FLAGS_SEARCH_PATH;

    /*
     * No profile is simple, as is a user specified environment (no change record).
     */
    if (   !(fFlags & RTPROC_FLAGS_PROFILE)
        || (   !(fFlags & RTPROC_FLAGS_ENV_CHANGE_RECORD)
            && hEnv != RTENV_DEFAULT))
        rc = rtProcWinCreateEnvBlockAndFindExe(fFlags, hEnv, pszExec, &pwszzBlock, ppwszExec);
    /*
     * Default profile environment without changes or path searching we leave
     * to the service that implements the API.
     */
    else if (   hEnv == RTENV_DEFAULT
             && !(fFlags & (RTPROC_FLAGS_ENV_CHANGE_RECORD | RTPROC_FLAGS_SEARCH_PATH)))
    {
        pwszzBlock = NULL;
        rc = VINF_SUCCESS;
    }
    /*
     * Otherwise, we need to get the user profile environment.
     */
    else
    {
        RTENV  hEnvToUse   = NIL_RTENV;
        HANDLE hTokenLogon = INVALID_HANDLE_VALUE;
        rc = rtProcWinUserLogon(pwszUser, pwszPassword, &hTokenLogon);
        if (RT_SUCCESS(rc))
        {
            /* CreateEnvFromToken docs says we should load the profile, though
               we haven't observed any difference when not doing it.  Maybe it's
               only an issue with roaming profiles or something similar... */
            PROFILEINFOW ProfileInfo;
            RT_ZERO(ProfileInfo);
            ProfileInfo.dwSize     = sizeof(ProfileInfo);
            ProfileInfo.lpUserName = pwszUser;
            ProfileInfo.dwFlags    = PI_NOUI; /* Prevents the display of profile error messages. */

            if (g_pfnLoadUserProfileW(hTokenLogon, &ProfileInfo))
            {
                /*
                 * Do what we need to do.  Don't keep any temp environment object.
                 */
                rc = rtProcWinCreateEnvFromToken(hTokenLogon, hEnv, fFlags, &hEnvToUse);
                if (RT_SUCCESS(rc))
                {
                    rc = rtProcWinFindExe(fFlags, hEnv, pszExec, ppwszExec);
                    if (RT_SUCCESS(rc))
                        rc = RTEnvQueryUtf16Block(hEnvToUse, &pwszzBlock);
                    if (hEnvToUse != hEnv)
                        RTEnvDestroy(hEnvToUse);
                }

                if (!g_pfnUnloadUserProfile(hTokenLogon, ProfileInfo.hProfile))
                    AssertFailed();
            }
            else
                rc = RTErrConvertFromWin32(GetLastError());

            if (hTokenLogon != INVALID_HANDLE_VALUE)
                CloseHandle(hTokenLogon);
        }
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Create the process.
         */
        Assert(!(dwCreationFlags & CREATE_SUSPENDED));
        bool const fCreatedSuspended = g_enmWinVer < kRTWinOSType_XP;
        BOOL fRc = g_pfnCreateProcessWithLogonW(pwszUser,
                                                NULL,                       /* lpDomain*/
                                                pwszPassword,
                                                fFlags & RTPROC_FLAGS_PROFILE ? 1 /*LOGON_WITH_PROFILE*/ : 0,
                                                *ppwszExec,
                                                pwszCmdLine,
                                                dwCreationFlags | (fCreatedSuspended ? CREATE_SUSPENDED : 0),
                                                pwszzBlock,
                                                pwszCwd,                     /* pCurrentDirectory */
                                                pStartupInfo,
                                                pProcInfo);
        if (fRc)
        {
            if (!fCreatedSuspended)
                rc = VINF_SUCCESS;
            else
            {
                /*
                 * Duplicate standard handles into the child process, we ignore failures here as it's
                 * legal to have bad standard handle values and we cannot dup console I/O handles.*
                 */
                PVOID pvDstProcParamCache = NULL;
                rtProcWinDupStdHandleIntoChild(pStartupInfo->hStdInput, pProcInfo->hProcess,
                                               RT_UOFFSETOF(RTL_USER_PROCESS_PARAMETERS, StandardInput), &pvDstProcParamCache);
                rtProcWinDupStdHandleIntoChild(pStartupInfo->hStdOutput, pProcInfo->hProcess,
                                               RT_UOFFSETOF(RTL_USER_PROCESS_PARAMETERS, StandardOutput), &pvDstProcParamCache);
                rtProcWinDupStdHandleIntoChild(pStartupInfo->hStdError,  pProcInfo->hProcess,
                                               RT_UOFFSETOF(RTL_USER_PROCESS_PARAMETERS, StandardError), &pvDstProcParamCache);

                if (ResumeThread(pProcInfo->hThread) != ~(DWORD)0)
                    rc = VINF_SUCCESS;
                else
                    rc = RTErrConvertFromWin32(GetLastError());
                if (RT_FAILURE(rc))
                {
                    TerminateProcess(pProcInfo->hProcess, 127);
                    CloseHandle(pProcInfo->hThread);
                    CloseHandle(pProcInfo->hProcess);
                }
            }
        }
        else
        {
            DWORD dwErr = GetLastError();
            rc = RTErrConvertFromWin32(dwErr);
            if (rc == VERR_UNRESOLVED_ERROR)
                LogRelFunc(("CreateProcessWithLogonW failed: dwErr=%u (%#x), rc=%Rrc\n", dwErr, dwErr, rc));
        }
        if (pwszzBlock)
            RTEnvFreeUtf16Block(pwszzBlock);
    }
    return rc;
}


static int rtProcWinCreateAsUser(PRTUTF16 pwszUser, PRTUTF16 pwszPassword, PRTUTF16 *ppwszExec, PRTUTF16 pwszCmdLine,
                                 RTENV hEnv, DWORD dwCreationFlags,
                                 STARTUPINFOW *pStartupInfo, PROCESS_INFORMATION *pProcInfo,
                                 uint32_t fFlags, const char *pszExec, uint32_t idDesiredSession,
                                 HANDLE hUserToken, PRTUTF16 pwszCwd)
{
    /*
     * If we run as a service CreateProcessWithLogon will fail, so don't even
     * try it (because of Local System context).  If we got an impersonated token
     * we should use, we also have to have to skip over this approach.
     * Note! This method is very slow on W2K.
     */
    if (!(fFlags & (RTPROC_FLAGS_SERVICE | RTPROC_FLAGS_AS_IMPERSONATED_TOKEN | RTPROC_FLAGS_TOKEN_SUPPLIED)))
    {
        AssertPtr(pwszUser);
        int rc = rtProcWinCreateAsUser1(pwszUser, pwszPassword, ppwszExec, pwszCmdLine,
                                        hEnv, dwCreationFlags, pStartupInfo, pProcInfo, fFlags, pszExec, pwszCwd);
        if (RT_SUCCESS(rc))
            return rc;
    }
    return rtProcWinCreateAsUser2(pwszUser, pwszPassword, ppwszExec, pwszCmdLine, hEnv, dwCreationFlags,
                                  pStartupInfo, pProcInfo, fFlags, pszExec, idDesiredSession, hUserToken, pwszCwd);
}


/**
 * RTPathTraverseList callback used by rtProcWinFindExe to locate the
 * executable.
 */
static DECLCALLBACK(int) rtPathFindExec(char const *pchPath, size_t cchPath, void *pvUser1, void *pvUser2)
{
    const char *pszExec     = (const char *)pvUser1;
    char       *pszRealExec = (char *)pvUser2;
    int rc = RTPathJoinEx(pszRealExec, RTPATH_MAX, pchPath, cchPath, pszExec, RTSTR_MAX, RTPATH_STR_F_STYLE_HOST);
    if (RT_FAILURE(rc))
        return rc;
    if (RTFileExists(pszRealExec))
        return VINF_SUCCESS;
    return VERR_TRY_AGAIN;
}


/**
 * Locate the executable file if necessary.
 *
 * @returns IPRT status code.
 * @param   pszExec         The UTF-8 executable string passed in by the user.
 * @param   fFlags          The process creation flags pass in by the user.
 * @param   hEnv            The environment to get the path variabel from.
 * @param   ppwszExec       Pointer to the variable pointing to the UTF-16
 *                          converted string.  If we find something, the current
 *                          pointer will be free (RTUtf16Free) and
 *                          replaced by a new one.
 */
static int rtProcWinFindExe(uint32_t fFlags, RTENV hEnv, const char *pszExec, PRTUTF16 *ppwszExec)
{
    /*
     * Return immediately if we're not asked to search, or if the file has a
     * path already or if it actually exists in the current directory.
     */
    if (   !(fFlags & RTPROC_FLAGS_SEARCH_PATH)
        || RTPathHavePath(pszExec)
        || RTPathExists(pszExec) )
        return VINF_SUCCESS;

    /*
     * Search the Path or PATH variable for the file.
     */
    char *pszPath;
    if (RTEnvExistEx(hEnv, "PATH"))
        pszPath = RTEnvDupEx(hEnv, "PATH");
    else if (RTEnvExistEx(hEnv, "Path"))
        pszPath = RTEnvDupEx(hEnv, "Path");
    else
        return VERR_FILE_NOT_FOUND;

    char szRealExec[RTPATH_MAX];
    int rc = RTPathTraverseList(pszPath, ';', rtPathFindExec, (void *)pszExec, &szRealExec[0]);
    RTStrFree(pszPath);
    if (RT_SUCCESS(rc))
    {
        /*
         * Replace the executable string.
         */
        RTPathWinFree(*ppwszExec);
        *ppwszExec = NULL;
        rc = RTPathWinFromUtf8(ppwszExec, szRealExec, 0 /*fFlags*/);
    }
    else if (rc == VERR_END_OF_STRING)
        rc = VERR_FILE_NOT_FOUND;
    return rc;
}


/**
 * Creates the UTF-16 environment block and, if necessary, find the executable.
 *
 * @returns IPRT status code.
 * @param   fFlags          The process creation flags pass in by the user.
 * @param   hEnv            The environment handle passed by the user.
 * @param   pszExec         See rtProcWinFindExe.
 * @param   ppwszzBlock     Where RTEnvQueryUtf16Block returns the block.
 * @param   ppwszExec       See rtProcWinFindExe.
 */
static int rtProcWinCreateEnvBlockAndFindExe(uint32_t fFlags, RTENV hEnv, const char *pszExec,
                                             PRTUTF16 *ppwszzBlock, PRTUTF16 *ppwszExec)
{
    int rc;

    /*
     * In most cases, we just need to convert the incoming enviornment to a
     * UTF-16 environment block.
     */
    RTENV hEnvToUse = NIL_RTENV; /* (MSC maybe used uninitialized) */
    if (   !(fFlags & (RTPROC_FLAGS_PROFILE | RTPROC_FLAGS_ENV_CHANGE_RECORD))
        || (hEnv == RTENV_DEFAULT && !(fFlags & RTPROC_FLAGS_PROFILE))
        || (hEnv != RTENV_DEFAULT && !(fFlags & RTPROC_FLAGS_ENV_CHANGE_RECORD)) )
    {
        hEnvToUse = hEnv;
        rc = VINF_SUCCESS;
    }
    else if (fFlags & RTPROC_FLAGS_PROFILE)
    {
        /*
         * We need to get the profile environment for the current user.
         */
        Assert((fFlags & RTPROC_FLAGS_ENV_CHANGE_RECORD) || hEnv == RTENV_DEFAULT);
        AssertReturn(g_pfnCreateEnvironmentBlock && g_pfnDestroyEnvironmentBlock, VERR_SYMBOL_NOT_FOUND);
        AssertReturn(g_pfnLoadUserProfileW && g_pfnUnloadUserProfile, VERR_SYMBOL_NOT_FOUND);
        HANDLE hToken;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE, &hToken))
        {
            rc = rtProcWinCreateEnvFromToken(hToken, hEnv, fFlags, &hEnvToUse);
            CloseHandle(hToken);
        }
        else
            rc = RTErrConvertFromWin32(GetLastError());
    }
    else
    {
        /*
         * Apply hEnv as a change record on top of the default environment.
         */
        Assert(fFlags & RTPROC_FLAGS_ENV_CHANGE_RECORD);
        rc = RTEnvClone(&hEnvToUse, RTENV_DEFAULT);
        if (RT_SUCCESS(rc))
        {
            rc = RTEnvApplyChanges(hEnvToUse, hEnv);
            if (RT_FAILURE(rc))
                RTEnvDestroy(hEnvToUse);
        }
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Query the UTF-16 environment block and locate the executable (if needed).
         */
        rc = RTEnvQueryUtf16Block(hEnvToUse, ppwszzBlock);
        if (RT_SUCCESS(rc))
            rc = rtProcWinFindExe(fFlags, hEnvToUse, pszExec, ppwszExec);

        if (hEnvToUse != hEnv)
            RTEnvDestroy(hEnvToUse);
    }

    return rc;
}


RTR3DECL(int)   RTProcCreateEx(const char *pszExec, const char * const *papszArgs, RTENV hEnv, uint32_t fFlags,
                               PCRTHANDLE phStdIn, PCRTHANDLE phStdOut, PCRTHANDLE phStdErr, const char *pszAsUser,
                               const char *pszPassword, void *pvExtraData, PRTPROCESS phProcess)
{
    /*
     * Input validation
     */
    AssertPtrReturn(pszExec, VERR_INVALID_POINTER);
    AssertReturn(*pszExec, VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & ~RTPROC_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & RTPROC_FLAGS_DETACHED) || !phProcess, VERR_INVALID_PARAMETER);
    AssertReturn(hEnv != NIL_RTENV, VERR_INVALID_PARAMETER);
    AssertPtrReturn(papszArgs, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pszAsUser, VERR_INVALID_POINTER);
    AssertReturn(!pszAsUser || *pszAsUser, VERR_INVALID_PARAMETER);
    AssertReturn(!pszPassword || pszAsUser, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pszPassword, VERR_INVALID_POINTER);

    /* Extra data: */
    AssertReturn(   pvExtraData == NULL
                 || (fFlags & (   RTPROC_FLAGS_DESIRED_SESSION_ID
                               | RTPROC_FLAGS_TOKEN_SUPPLIED
                               | RTPROC_FLAGS_CWD)),
                 VERR_INVALID_PARAMETER);

    uint32_t idDesiredSession = UINT32_MAX;
    if (   (fFlags & (RTPROC_FLAGS_DESIRED_SESSION_ID | RTPROC_FLAGS_SERVICE))
        ==           (RTPROC_FLAGS_DESIRED_SESSION_ID | RTPROC_FLAGS_SERVICE))
    {
        AssertReturn(!(fFlags & (  RTPROC_FLAGS_TOKEN_SUPPLIED
                                 | RTPROC_FLAGS_CWD)),
                     VERR_INVALID_PARAMETER);
        AssertPtrReturn(pvExtraData, VERR_INVALID_POINTER);
        idDesiredSession = *(uint32_t *)pvExtraData;
    }
    else
        AssertReturn(!(fFlags & RTPROC_FLAGS_DESIRED_SESSION_ID), VERR_INVALID_FLAGS);

    if (fFlags & RTPROC_FLAGS_CWD)
    {
        AssertReturn(!(fFlags & (  RTPROC_FLAGS_DESIRED_SESSION_ID
                                 | RTPROC_FLAGS_TOKEN_SUPPLIED)),
                     VERR_INVALID_PARAMETER);
        AssertPtrReturn(pvExtraData, VERR_INVALID_POINTER);
    }

    HANDLE hUserToken = NULL;
    if (fFlags & RTPROC_FLAGS_TOKEN_SUPPLIED)
        hUserToken = *(HANDLE *)pvExtraData;

    /*
     * Initialize the globals.
     */
    int rc = RTOnce(&g_rtProcWinInitOnce, rtProcWinInitOnce, NULL);
    AssertRCReturn(rc, rc);
    if (   pszAsUser
        || (fFlags & (RTPROC_FLAGS_PROFILE | RTPROC_FLAGS_SERVICE | RTPROC_FLAGS_AS_IMPERSONATED_TOKEN
                      | RTPROC_FLAGS_TOKEN_SUPPLIED)))
    {
        rc = RTOnce(&g_rtProcWinResolveOnce, rtProcWinResolveOnce, NULL);
        AssertRCReturn(rc, rc);
    }

    /*
     * Get the file descriptors for the handles we've been passed.
     *
     * It seems there is no point in trying to convince a child process's CRT
     * that any of the standard file handles is non-TEXT.  So, we don't...
     */
    STARTUPINFOW StartupInfo;
    RT_ZERO(StartupInfo);
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags   = STARTF_USESTDHANDLES;
#if 1 /* The CRT should keep the standard handles up to date. */
    StartupInfo.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    StartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    StartupInfo.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
#else
    StartupInfo.hStdInput  = _get_osfhandle(0);
    StartupInfo.hStdOutput = _get_osfhandle(1);
    StartupInfo.hStdError  = _get_osfhandle(2);
#endif
    /* If we want to have a hidden process (e.g. not visible to
     * to the user) use the STARTUPINFO flags. */
    if (fFlags & RTPROC_FLAGS_HIDDEN)
    {
        StartupInfo.dwFlags |= STARTF_USESHOWWINDOW;
        StartupInfo.wShowWindow = SW_HIDE;
    }

    PCRTHANDLE  paHandles[3] = { phStdIn,                   phStdOut,                   phStdErr };
    HANDLE     *aphStds[3]   = { &StartupInfo.hStdInput,    &StartupInfo.hStdOutput,    &StartupInfo.hStdError };
    DWORD       afInhStds[3] = { 0xffffffff,                0xffffffff,                 0xffffffff };
    HANDLE      ahStdDups[3] = { INVALID_HANDLE_VALUE,      INVALID_HANDLE_VALUE,       INVALID_HANDLE_VALUE };
    for (int i = 0; i < 3; i++)
    {
        if (paHandles[i])
        {
            AssertPtrReturn(paHandles[i], VERR_INVALID_POINTER);
            switch (paHandles[i]->enmType)
            {
                case RTHANDLETYPE_FILE:
                {
                    HANDLE hNativeFile = paHandles[i]->u.hFile != NIL_RTFILE
                                       ? (HANDLE)RTFileToNative(paHandles[i]->u.hFile)
                                       : INVALID_HANDLE_VALUE;
                    if (   hNativeFile == *aphStds[i]
                        && g_enmWinVer == kRTWinOSType_NT310)
                        continue;
                    *aphStds[i] = hNativeFile;
                    break;
                }

                case RTHANDLETYPE_PIPE:
                    *aphStds[i] = paHandles[i]->u.hPipe != NIL_RTPIPE
                                ? (HANDLE)RTPipeToNative(paHandles[i]->u.hPipe)
                                : INVALID_HANDLE_VALUE;
                    if (   g_enmWinVer == kRTWinOSType_NT310
                        && *aphStds[i] == INVALID_HANDLE_VALUE)
                    {
                        AssertMsgReturn(RTPipeGetCreationInheritability(paHandles[i]->u.hPipe), ("%Rrc %p\n", rc, *aphStds[i]),
                                        VERR_INVALID_STATE);
                        continue;
                    }
                    break;

                case RTHANDLETYPE_SOCKET:
                    *aphStds[i] = paHandles[i]->u.hSocket != NIL_RTSOCKET
                                ? (HANDLE)RTSocketToNative(paHandles[i]->u.hSocket)
                                : INVALID_HANDLE_VALUE;
                    break;

                default:
                    AssertMsgFailedReturn(("%d: %d\n", i, paHandles[i]->enmType), VERR_INVALID_PARAMETER);
            }

            /* Get the inheritability of the handle. */
            if (*aphStds[i] != INVALID_HANDLE_VALUE)
            {
                if (!g_pfnGetHandleInformation)
                    afInhStds[i] = 0; /* No handle info on NT 3.1, so ASSUME it is not inheritable. */
                else if (!g_pfnGetHandleInformation(*aphStds[i], &afInhStds[i]))
                {
                    rc = RTErrConvertFromWin32(GetLastError());
                    AssertMsgFailedReturn(("%Rrc aphStds[%d] => %p paHandles[%d]={%d,%p}\n",
                                           rc, i, *aphStds[i], i, paHandles[i]->enmType, paHandles[i]->u.uInt),
                                          rc);
                }
            }
        }
    }

    /*
     * Set the inheritability any handles we're handing the child.
     *
     * Note! On NT 3.1 there is no SetHandleInformation, so we have to duplicate
     *       the handles to make sure they are inherited by the child.
     */
    rc = VINF_SUCCESS;
    for (int i = 0; i < 3; i++)
        if (   (afInhStds[i] != 0xffffffff)
            && !(afInhStds[i] & HANDLE_FLAG_INHERIT))
        {
            if (!g_pfnSetHandleInformation)
            {
                if (DuplicateHandle(GetCurrentProcess(), *aphStds[i], GetCurrentProcess(), &ahStdDups[i],
                                    i == 0 ? GENERIC_READ : GENERIC_WRITE, TRUE /*fInheritHandle*/, DUPLICATE_SAME_ACCESS))
                    *aphStds[i] = ahStdDups[i];
                else
                {
                    rc = RTErrConvertFromWin32(GetLastError());
                    AssertMsgFailedBreak(("%Rrc aphStds[%u] => %p\n", rc, i, *aphStds[i]));
                }
            }
            else if (!g_pfnSetHandleInformation(*aphStds[i], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
            {
                rc = RTErrConvertFromWin32(GetLastError());
                if (rc == VERR_INVALID_FUNCTION && g_enmWinVer == kRTWinOSType_NT310)
                    rc = VINF_SUCCESS;
                else
                    AssertMsgFailedBreak(("%Rrc aphStds[%u] => %p\n", rc, i, *aphStds[i]));
            }
        }

    /*
     * Create the command line and convert the executable name.
     */
    PRTUTF16 pwszCmdLine = NULL; /* Shut up, MSC! */
    if (RT_SUCCESS(rc))
        rc = RTGetOptArgvToUtf16String(&pwszCmdLine, papszArgs,
                                       !(fFlags & RTPROC_FLAGS_UNQUOTED_ARGS)
                                       ? RTGETOPTARGV_CNV_QUOTE_MS_CRT : RTGETOPTARGV_CNV_UNQUOTED);
    if (RT_SUCCESS(rc))
    {
        PRTUTF16 pwszExec = NULL;
        rc = RTPathWinFromUtf8(&pwszExec, pszExec, 0 /*fFlags*/);
        if (RT_SUCCESS(rc))
        {
            PRTUTF16 pwszCwd = NULL;
            if (fFlags & RTPROC_FLAGS_CWD)
                rc = RTPathWinFromUtf8(&pwszCwd, (const char *)pvExtraData, 0);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Get going...
                 */
                PROCESS_INFORMATION ProcInfo;
                RT_ZERO(ProcInfo);
                DWORD dwCreationFlags = CREATE_UNICODE_ENVIRONMENT;
                if (fFlags & RTPROC_FLAGS_DETACHED)
                    dwCreationFlags |= DETACHED_PROCESS;
                if (fFlags & RTPROC_FLAGS_NO_WINDOW)
                    dwCreationFlags |= CREATE_NO_WINDOW;

                /*
                 * Only use the normal CreateProcess stuff if we have no user name
                 * and we are not running from a (Windows) service. Otherwise use
                 * the more advanced version in rtProcWinCreateAsUser().
                 */
                if (   pszAsUser == NULL
                    && !(fFlags & (RTPROC_FLAGS_SERVICE | RTPROC_FLAGS_AS_IMPERSONATED_TOKEN | RTPROC_FLAGS_TOKEN_SUPPLIED)))
                {
                    /* Create the environment block first. */
                    PRTUTF16 pwszzBlock;
                    rc = rtProcWinCreateEnvBlockAndFindExe(fFlags, hEnv, pszExec, &pwszzBlock, &pwszExec);
                    if (RT_SUCCESS(rc))
                    {
                        if (CreateProcessW(pwszExec,
                                           pwszCmdLine,
                                           NULL,         /* pProcessAttributes */
                                           NULL,         /* pThreadAttributes */
                                           TRUE,         /* fInheritHandles */
                                           dwCreationFlags,
                                           pwszzBlock,
                                           pwszCwd,       /* pCurrentDirectory */
                                           &StartupInfo,
                                           &ProcInfo))
                            rc = VINF_SUCCESS;
                        else
                            rc = RTErrConvertFromWin32(GetLastError());
                        RTEnvFreeUtf16Block(pwszzBlock);
                    }
                }
                else
                {
                    /*
                     * Convert the additional parameters and use a helper
                     * function to do the actual work.
                     */
                    PRTUTF16 pwszUser = NULL;
                    if (pszAsUser)
                        rc = RTStrToUtf16(pszAsUser, &pwszUser);
                    if (RT_SUCCESS(rc))
                    {
                        PRTUTF16 pwszPassword;
                        rc = RTStrToUtf16(pszPassword ? pszPassword : "", &pwszPassword);
                        if (RT_SUCCESS(rc))
                        {
                            rc = rtProcWinCreateAsUser(pwszUser, pwszPassword, &pwszExec, pwszCmdLine, hEnv, dwCreationFlags,
                                                       &StartupInfo, &ProcInfo, fFlags, pszExec, idDesiredSession,
                                                       hUserToken, pwszCwd);

                            if (pwszPassword && *pwszPassword)
                                RTMemWipeThoroughly(pwszPassword, RTUtf16Len(pwszPassword), 5);
                            RTUtf16Free(pwszPassword);
                        }
                        RTUtf16Free(pwszUser);
                    }
                }
                if (RT_SUCCESS(rc))
                {
                    CloseHandle(ProcInfo.hThread);
                    if (phProcess)
                    {
                        /*
                         * Add the process to the child process list so RTProcWait can reuse and close
                         * the process handle, unless, of course, the caller has no intention waiting.
                         */
                        if (!(fFlags & RTPROC_FLAGS_NO_WAIT))
                            rtProcWinAddPid(ProcInfo.dwProcessId, ProcInfo.hProcess);
                        else
                            CloseHandle(ProcInfo.hProcess);
                        *phProcess = ProcInfo.dwProcessId;
                    }
                    else
                        CloseHandle(ProcInfo.hProcess);
                    rc = VINF_SUCCESS;
                }

                RTPathWinFree(pwszCwd);
            }
            RTPathWinFree(pwszExec);
        }
        RTUtf16Free(pwszCmdLine);
    }

    if (g_pfnSetHandleInformation)
    {
        /* Undo any handle inherit changes. */
        for (int i = 0; i < 3; i++)
            if (   (afInhStds[i] != 0xffffffff)
                && !(afInhStds[i] & HANDLE_FLAG_INHERIT))
            {
                if (   !g_pfnSetHandleInformation(*aphStds[i], HANDLE_FLAG_INHERIT, 0)
                    && (   GetLastError() != ERROR_INVALID_FUNCTION
                        || g_enmWinVer != kRTWinOSType_NT310) )
                    AssertMsgFailed(("%Rrc %p\n", RTErrConvertFromWin32(GetLastError()), *aphStds[i]));
            }
    }
    else
    {
        /* Close handles duplicated for correct inheritance. */
        for (int i = 0; i < 3; i++)
            if (ahStdDups[i] != INVALID_HANDLE_VALUE)
                CloseHandle(ahStdDups[i]);
    }

    return rc;
}



RTR3DECL(int) RTProcWait(RTPROCESS Process, unsigned fFlags, PRTPROCSTATUS pProcStatus)
{
    AssertReturn(!(fFlags & ~(RTPROCWAIT_FLAGS_BLOCK | RTPROCWAIT_FLAGS_NOBLOCK)), VERR_INVALID_PARAMETER);
    int rc = RTOnce(&g_rtProcWinInitOnce, rtProcWinInitOnce, NULL);
    AssertRCReturn(rc, rc);

    /*
     * Try find the process among the ones we've spawned, otherwise, attempt
     * opening the specified process.
     */
    HANDLE hOpenedProc = NULL;
    HANDLE hProcess = rtProcWinFindPid(Process);
    if (hProcess == NULL)
    {
        hProcess = hOpenedProc = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, Process);
        if (hProcess == NULL)
        {
            DWORD dwErr = GetLastError();
            if (dwErr == ERROR_INVALID_PARAMETER)
                return VERR_PROCESS_NOT_FOUND;
            return RTErrConvertFromWin32(dwErr);
        }
    }

    /*
     * Wait for it to terminate.
     */
    DWORD Millies = fFlags == RTPROCWAIT_FLAGS_BLOCK ? INFINITE : 0;
    DWORD WaitRc = WaitForSingleObjectEx(hProcess, Millies, TRUE);
    while (WaitRc == WAIT_IO_COMPLETION)
        WaitRc = WaitForSingleObjectEx(hProcess, Millies, TRUE);
    switch (WaitRc)
    {
        /*
         * It has terminated.
         */
        case WAIT_OBJECT_0:
        {
            DWORD dwExitCode;
            if (GetExitCodeProcess(hProcess, &dwExitCode))
            {
                /** @todo the exit code can be special statuses. */
                if (pProcStatus)
                {
                    pProcStatus->enmReason = RTPROCEXITREASON_NORMAL;
                    pProcStatus->iStatus = (int)dwExitCode;
                }
                if (hOpenedProc == NULL)
                    rtProcWinRemovePid(Process);
                rc = VINF_SUCCESS;
            }
            else
                rc = RTErrConvertFromWin32(GetLastError());
            break;
        }

        /*
         * It hasn't terminated just yet.
         */
        case WAIT_TIMEOUT:
            rc = VERR_PROCESS_RUNNING;
            break;

        /*
         * Something went wrong...
         */
        case WAIT_FAILED:
            rc = RTErrConvertFromWin32(GetLastError());
            break;

        case WAIT_ABANDONED:
            AssertFailed();
            rc = VERR_GENERAL_FAILURE;
            break;

        default:
            AssertMsgFailed(("WaitRc=%RU32\n", WaitRc));
            rc = VERR_GENERAL_FAILURE;
            break;
    }

    if (hOpenedProc != NULL)
        CloseHandle(hOpenedProc);
    return rc;
}


RTR3DECL(int) RTProcWaitNoResume(RTPROCESS Process, unsigned fFlags, PRTPROCSTATUS pProcStatus)
{
    /** @todo this isn't quite right. */
    return RTProcWait(Process, fFlags, pProcStatus);
}


RTR3DECL(int) RTProcTerminate(RTPROCESS Process)
{
    if (Process == NIL_RTPROCESS)
        return VINF_SUCCESS;

    int rc = RTOnce(&g_rtProcWinInitOnce, rtProcWinInitOnce, NULL);
    AssertRCReturn(rc, rc);

    /*
     * Try find the process among the ones we've spawned, otherwise, attempt
     * opening the specified process.
     */
    HANDLE hProcess = rtProcWinFindPid(Process);
    if (hProcess != NULL)
    {
        if (!TerminateProcess(hProcess, 127))
            rc = RTErrConvertFromWin32(GetLastError());
    }
    else
    {
        hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, Process);
        if (hProcess != NULL)
        {
            BOOL  fRc   = TerminateProcess(hProcess, 127);
            DWORD dwErr = GetLastError();
            CloseHandle(hProcess);
            if (!fRc)
                rc = RTErrConvertFromWin32(dwErr);
        }
    }
    return rc;
}


RTR3DECL(uint64_t) RTProcGetAffinityMask(void)
{
    DWORD_PTR dwProcessAffinityMask = 0xffffffff;
    DWORD_PTR dwSystemAffinityMask;

    BOOL fRc = GetProcessAffinityMask(GetCurrentProcess(), &dwProcessAffinityMask, &dwSystemAffinityMask);
    Assert(fRc); NOREF(fRc);

    return dwProcessAffinityMask;
}


RTR3DECL(int) RTProcQueryUsername(RTPROCESS hProcess, char *pszUser, size_t cbUser, size_t *pcbUser)
{
    AssertReturn(   (pszUser && cbUser > 0)
                 || (!pszUser && !cbUser), VERR_INVALID_PARAMETER);
    AssertReturn(pcbUser || pszUser, VERR_INVALID_PARAMETER);

    int rc;
    if (   hProcess == NIL_RTPROCESS
        || hProcess == RTProcSelf())
    {
        RTUTF16 wszUsername[UNLEN + 1];
        DWORD   cwcUsername = RT_ELEMENTS(wszUsername);
        if (GetUserNameW(&wszUsername[0], &cwcUsername))
        {
            if (pszUser)
            {
                rc = RTUtf16ToUtf8Ex(wszUsername, cwcUsername, &pszUser, cbUser, pcbUser);
                if (pcbUser)
                    *pcbUser += 1;
            }
            else
            {
                *pcbUser = RTUtf16CalcUtf8Len(wszUsername) + 1;
                rc = VERR_BUFFER_OVERFLOW;
            }
        }
        else
            rc = RTErrConvertFromWin32(GetLastError());
    }
    else
        rc = VERR_NOT_SUPPORTED;
    return rc;
}


RTR3DECL(int) RTProcQueryUsernameA(RTPROCESS hProcess, char **ppszUser)
{
    AssertPtrReturn(ppszUser, VERR_INVALID_POINTER);
    int rc;
    if (   hProcess == NIL_RTPROCESS
        || hProcess == RTProcSelf())
    {
        RTUTF16 wszUsername[UNLEN + 1];
        DWORD   cwcUsername = RT_ELEMENTS(wszUsername);
        if (GetUserNameW(&wszUsername[0], &cwcUsername))
            rc = RTUtf16ToUtf8(wszUsername, ppszUser);
        else
            rc = RTErrConvertFromWin32(GetLastError());
    }
    else
        rc = VERR_NOT_SUPPORTED;
    return rc;
}

