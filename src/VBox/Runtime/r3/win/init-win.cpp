/* $Id: init-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Init Ring-3, Windows Specific Code.
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
#define LOG_GROUP RTLOGGROUP_DEFAULT
#include <iprt/nt/nt-and-windows.h>
#ifndef LOAD_LIBRARY_SEARCH_APPLICATION_DIR
# define LOAD_LIBRARY_SEARCH_APPLICATION_DIR    0x200
# define LOAD_LIBRARY_SEARCH_SYSTEM32           0x800
#endif

#include "internal-r3-win.h"
#include <iprt/initterm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/ldr.h>
#include <iprt/log.h>
#include <iprt/param.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include "../init.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef VOID (WINAPI *PFNGETCURRENTTHREADSTACKLIMITS)(PULONG_PTR puLow, PULONG_PTR puHigh);
typedef LPTOP_LEVEL_EXCEPTION_FILTER (WINAPI * PFNSETUNHANDLEDEXCEPTIONFILTER)(LPTOP_LEVEL_EXCEPTION_FILTER);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Windows DLL loader protection level. */
DECL_HIDDEN_DATA(RTR3WINLDRPROT)      g_enmWinLdrProt = RTR3WINLDRPROT_NONE;
/** Our simplified windows version.    */
DECL_HIDDEN_DATA(RTWINOSTYPE)         g_enmWinVer = kRTWinOSType_UNKNOWN;
/** Extended windows version information. */
DECL_HIDDEN_DATA(OSVERSIONINFOEXW)    g_WinOsInfoEx;

/** The native kernel32.dll handle. */
DECL_HIDDEN_DATA(HMODULE)                       g_hModKernel32 = NULL;
/** GetSystemWindowsDirectoryW or GetWindowsDirectoryW (NT4). */
DECL_HIDDEN_DATA(PFNGETWINSYSDIR)               g_pfnGetSystemWindowsDirectoryW = NULL;
/** The GetCurrentThreadStackLimits API. */
static PFNGETCURRENTTHREADSTACKLIMITS           g_pfnGetCurrentThreadStackLimits = NULL;
/** The previous unhandled exception filter. */
static LPTOP_LEVEL_EXCEPTION_FILTER             g_pfnUnhandledXcptFilter = NULL;
/** SystemTimeToTzSpecificLocalTime. */
DECL_HIDDEN_DATA(decltype(SystemTimeToTzSpecificLocalTime) *)   g_pfnSystemTimeToTzSpecificLocalTime = NULL;
/** CreateWaitableTimerEx . */
DECL_HIDDEN_DATA(PFNCREATEWAITABLETIMEREX)                      g_pfnCreateWaitableTimerExW = NULL;
DECL_HIDDEN_DATA(decltype(GetHandleInformation) *)              g_pfnGetHandleInformation = NULL;
DECL_HIDDEN_DATA(decltype(SetHandleInformation) *)              g_pfnSetHandleInformation = NULL;
DECL_HIDDEN_DATA(decltype(IsDebuggerPresent) *)                 g_pfnIsDebuggerPresent = NULL;
DECL_HIDDEN_DATA(decltype(GetSystemTimeAsFileTime) *)           g_pfnGetSystemTimeAsFileTime = NULL;
DECL_HIDDEN_DATA(decltype(GetProcessAffinityMask) *)            g_pfnGetProcessAffinityMask = NULL;
DECL_HIDDEN_DATA(decltype(SetThreadAffinityMask) *)             g_pfnSetThreadAffinityMask = NULL;
DECL_HIDDEN_DATA(decltype(CreateIoCompletionPort) *)            g_pfnCreateIoCompletionPort = NULL;
DECL_HIDDEN_DATA(decltype(GetQueuedCompletionStatus) *)         g_pfnGetQueuedCompletionStatus = NULL;
DECL_HIDDEN_DATA(decltype(PostQueuedCompletionStatus) *)        g_pfnPostQueuedCompletionStatus = NULL;
DECL_HIDDEN_DATA(decltype(IsProcessorFeaturePresent) *)         g_pfnIsProcessorFeaturePresent = NULL;
DECL_HIDDEN_DATA(decltype(SetUnhandledExceptionFilter) *)       g_pfnSetUnhandledExceptionFilter = NULL;
DECL_HIDDEN_DATA(decltype(UnhandledExceptionFilter) *)          g_pfnUnhandledExceptionFilter = NULL;

/** The native ntdll.dll handle. */
DECL_HIDDEN_DATA(HMODULE)                       g_hModNtDll = NULL;
/** NtQueryFullAttributesFile   */
DECL_HIDDEN_DATA(PFNNTQUERYFULLATTRIBUTESFILE)  g_pfnNtQueryFullAttributesFile = NULL;
/** NtDuplicateToken (NT 3.51). */
DECL_HIDDEN_DATA(PFNNTDUPLICATETOKEN)           g_pfnNtDuplicateToken = NULL;
/** NtAlertThread (NT 3.51). */
DECL_HIDDEN_DATA(decltype(NtAlertThread) *)     g_pfnNtAlertThread = NULL;

/** Either ws2_32.dll (NT4+) or wsock32.dll (NT3.x). */
DECL_HIDDEN_DATA(HMODULE)                       g_hModWinSock = NULL;
/** Set if we're dealing with old winsock.   */
DECL_HIDDEN_DATA(bool)                          g_fOldWinSock = false;
/** WSAStartup   */
DECL_HIDDEN_DATA(PFNWSASTARTUP)                 g_pfnWSAStartup = NULL;
/** WSACleanup */
DECL_HIDDEN_DATA(PFNWSACLEANUP)                 g_pfnWSACleanup = NULL;
/** Pointner to WSAGetLastError (for RTErrVarsSave). */
DECL_HIDDEN_DATA(PFNWSAGETLASTERROR)            g_pfnWSAGetLastError = NULL;
/** Pointner to WSASetLastError (for RTErrVarsRestore). */
DECL_HIDDEN_DATA(PFNWSASETLASTERROR)            g_pfnWSASetLastError = NULL;
/** WSACreateEvent */
DECL_HIDDEN_DATA(PFNWSACREATEEVENT)             g_pfnWSACreateEvent = NULL;
/** WSACloseEvent  */
DECL_HIDDEN_DATA(PFNWSACLOSEEVENT)              g_pfnWSACloseEvent = NULL;
/** WSASetEvent */
DECL_HIDDEN_DATA(PFNWSASETEVENT)                g_pfnWSASetEvent = NULL;
/** WSAEventSelect   */
DECL_HIDDEN_DATA(PFNWSAEVENTSELECT)             g_pfnWSAEventSelect = NULL;
/** WSAEnumNetworkEvents */
DECL_HIDDEN_DATA(PFNWSAENUMNETWORKEVENTS)       g_pfnWSAEnumNetworkEvents = NULL;
/** WSASocketW */
DECL_HIDDEN_DATA(PFNWSASOCKETW)                 g_pfnWSASocketW = NULL;
/** WSASend */
DECL_HIDDEN_DATA(PFNWSASEND)                    g_pfnWSASend = NULL;
/** WSAIoctl */
DECL_HIDDEN_DATA(PFNWSAIOCTL)                   g_pfnWSAIoctl = NULL;
/** socket */
DECL_HIDDEN_DATA(PFNWINSOCKSOCKET)              g_pfnsocket = NULL;
/** closesocket */
DECL_HIDDEN_DATA(PFNWINSOCKCLOSESOCKET)         g_pfnclosesocket = NULL;
/** recv */
DECL_HIDDEN_DATA(PFNWINSOCKRECV)                g_pfnrecv = NULL;
/** send */
DECL_HIDDEN_DATA(PFNWINSOCKSEND)                g_pfnsend = NULL;
/** recvfrom */
DECL_HIDDEN_DATA(PFNWINSOCKRECVFROM)            g_pfnrecvfrom = NULL;
/** sendto */
DECL_HIDDEN_DATA(PFNWINSOCKSENDTO)              g_pfnsendto = NULL;
/** bind */
DECL_HIDDEN_DATA(PFNWINSOCKBIND)                g_pfnbind = NULL;
/** listen  */
DECL_HIDDEN_DATA(PFNWINSOCKLISTEN)              g_pfnlisten = NULL;
/** accept */
DECL_HIDDEN_DATA(PFNWINSOCKACCEPT)              g_pfnaccept = NULL;
/** connect */
DECL_HIDDEN_DATA(PFNWINSOCKCONNECT)             g_pfnconnect = NULL;
/** shutdown */
DECL_HIDDEN_DATA(PFNWINSOCKSHUTDOWN)            g_pfnshutdown = NULL;
/** getsockopt */
DECL_HIDDEN_DATA(PFNWINSOCKGETSOCKOPT)          g_pfngetsockopt = NULL;
/** setsockopt */
DECL_HIDDEN_DATA(PFNWINSOCKSETSOCKOPT)          g_pfnsetsockopt = NULL;
/** ioctlsocket */
DECL_HIDDEN_DATA(PFNWINSOCKIOCTLSOCKET)         g_pfnioctlsocket = NULL;
/** getpeername   */
DECL_HIDDEN_DATA(PFNWINSOCKGETPEERNAME)         g_pfngetpeername = NULL;
/** getsockname */
DECL_HIDDEN_DATA(PFNWINSOCKGETSOCKNAME)         g_pfngetsockname = NULL;
/** __WSAFDIsSet */
DECL_HIDDEN_DATA(PFNWINSOCK__WSAFDISSET)        g_pfn__WSAFDIsSet = NULL;
/** select */
DECL_HIDDEN_DATA(PFNWINSOCKSELECT)              g_pfnselect = NULL;
/** gethostbyname */
DECL_HIDDEN_DATA(PFNWINSOCKGETHOSTBYNAME)       g_pfngethostbyname = NULL;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static LONG CALLBACK rtR3WinUnhandledXcptFilter(PEXCEPTION_POINTERS);


/**
 * Translates OSVERSIONINOFEX into a Windows OS type.
 *
 * @returns The Windows OS type.
 * @param   pOSInfoEx       The OS info returned by Windows.
 *
 * @remarks This table has been assembled from Usenet postings, personal
 *          observations, and reading other people's code.  Please feel
 *          free to add to it or correct it.
 * <pre>
         dwPlatFormID  dwMajorVersion  dwMinorVersion  dwBuildNumber
95             1              4               0             950
95 SP1         1              4               0        >950 && <=1080
95 OSR2        1              4             <10           >1080
98             1              4              10            1998
98 SP1         1              4              10       >1998 && <2183
98 SE          1              4              10          >=2183
ME             1              4              90            3000

NT 3.51        2              3              51            1057
NT 4           2              4               0            1381
2000           2              5               0            2195
XP             2              5               1            2600
2003           2              5               2            3790
Vista          2              6               0

CE 1.0         3              1               0
CE 2.0         3              2               0
CE 2.1         3              2               1
CE 3.0         3              3               0
</pre>
 */
static RTWINOSTYPE rtR3InitWinSimplifiedVersion(OSVERSIONINFOEXW const *pOSInfoEx)
{
    RTWINOSTYPE enmVer         = kRTWinOSType_UNKNOWN;
    BYTE  const bProductType   = pOSInfoEx->wProductType;
    DWORD const dwPlatformId   = pOSInfoEx->dwPlatformId;
    DWORD const dwMinorVersion = pOSInfoEx->dwMinorVersion;
    DWORD const dwMajorVersion = pOSInfoEx->dwMajorVersion;
    DWORD const dwBuildNumber  = pOSInfoEx->dwBuildNumber & 0xFFFF;   /* Win 9x needs this. */

    if (    dwPlatformId == VER_PLATFORM_WIN32_WINDOWS
        &&  dwMajorVersion == 4)
    {
        if (        dwMinorVersion < 10
                 && dwBuildNumber == 950)
            enmVer = kRTWinOSType_95;
        else if (   dwMinorVersion < 10
                 && dwBuildNumber > 950
                 && dwBuildNumber <= 1080)
            enmVer = kRTWinOSType_95SP1;
        else if (   dwMinorVersion < 10
                 && dwBuildNumber > 1080)
            enmVer = kRTWinOSType_95OSR2;
        else if (   dwMinorVersion == 10
                 && dwBuildNumber == 1998)
            enmVer = kRTWinOSType_98;
        else if (   dwMinorVersion == 10
                 && dwBuildNumber > 1998
                 && dwBuildNumber < 2183)
            enmVer = kRTWinOSType_98SP1;
        else if (   dwMinorVersion == 10
                 && dwBuildNumber >= 2183)
            enmVer = kRTWinOSType_98SE;
        else if (dwMinorVersion == 90)
            enmVer = kRTWinOSType_ME;
    }
    else if (dwPlatformId == VER_PLATFORM_WIN32_NT)
    {
        if (dwMajorVersion == 3)
        {
            if (     dwMinorVersion < 50)
                enmVer = kRTWinOSType_NT310;
            else if (dwMinorVersion == 50)
                enmVer = kRTWinOSType_NT350;
            else
                enmVer = kRTWinOSType_NT351;
        }
        else if (dwMajorVersion == 4)
            enmVer = kRTWinOSType_NT4;
        else if (dwMajorVersion == 5)
        {
            if (dwMinorVersion == 0)
                enmVer = kRTWinOSType_2K;
            else if (dwMinorVersion == 1)
                enmVer = kRTWinOSType_XP;
            else
                enmVer = kRTWinOSType_2003;
        }
        else if (dwMajorVersion == 6)
        {
            if (dwMinorVersion == 0)
                enmVer = bProductType != VER_NT_WORKSTATION ? kRTWinOSType_2008   : kRTWinOSType_VISTA;
            else if (dwMinorVersion == 1)
                enmVer = bProductType != VER_NT_WORKSTATION ? kRTWinOSType_2008R2 : kRTWinOSType_7;
            else if (dwMinorVersion == 2)
                enmVer = bProductType != VER_NT_WORKSTATION ? kRTWinOSType_2012   : kRTWinOSType_8;
            else if (dwMinorVersion == 3)
                enmVer = bProductType != VER_NT_WORKSTATION ? kRTWinOSType_2012R2 : kRTWinOSType_81;
            else if (dwMinorVersion == 4)
                enmVer = bProductType != VER_NT_WORKSTATION ? kRTWinOSType_2016   : kRTWinOSType_10;
            else
                enmVer = kRTWinOSType_NT_UNKNOWN;
        }
        else if (dwMajorVersion == 10)
        {
            if (dwMinorVersion == 0)
            {
                /* The version detection for server 2019, server 2022 and windows 11
                   are by build number.  Stupid, stupid, Microsoft. */
                if (bProductType == VER_NT_WORKSTATION)
                    enmVer = dwBuildNumber >= 22000 ? kRTWinOSType_11 : kRTWinOSType_10;
                else
                    enmVer = dwBuildNumber >= 20348 ? kRTWinOSType_2022
                           : dwBuildNumber >= 17763 ? kRTWinOSType_2019 : kRTWinOSType_2016;
            }
            else
                enmVer = kRTWinOSType_NT_UNKNOWN;
        }
        else
            enmVer = kRTWinOSType_NT_UNKNOWN;
    }

    return enmVer;
}


/**
 * Initializes the global variables related to windows version.
 */
static void rtR3InitWindowsVersion(void)
{
    Assert(g_hModNtDll != NULL);

    /*
     * ASSUMES OSVERSIONINFOEX starts with the exact same layout as OSVERSIONINFO (safe).
     */
    AssertCompileMembersSameSizeAndOffset(OSVERSIONINFOEX, szCSDVersion, OSVERSIONINFO, szCSDVersion);
    AssertCompileMemberOffset(OSVERSIONINFOEX, wServicePackMajor, sizeof(OSVERSIONINFO));

    /*
     * Use the NT version of RtlGetVersion (since w2k) so we don't get fooled
     * by the standard compatibility shims.  (Sandboxes may still fool us.)
     *
     * Note! This API was added in windows 2000 together with the extended
     *       version info structure (OSVERSIONINFOEXW), so there is no need
     *       to retry with the smaller version (OSVERSIONINFOW).
     */
    RT_ZERO(g_WinOsInfoEx);
    g_WinOsInfoEx.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);

    LONG (__stdcall *pfnRtlGetVersion)(OSVERSIONINFOEXW *);
    *(FARPROC *)&pfnRtlGetVersion = GetProcAddress(g_hModNtDll, "RtlGetVersion");
    LONG rcNt = -1;
    if (pfnRtlGetVersion)
        rcNt = pfnRtlGetVersion(&g_WinOsInfoEx);
    if (rcNt != 0)
    {
        /*
         * Couldn't find it or it failed, try the windows version of the API.
         * The GetVersionExW API was added in NT 3.51, however only the small
         * structure version existed till windows 2000.  We'll try the larger
         * structure version first, anyway, just in case.
         */
        RT_ZERO(g_WinOsInfoEx);
        g_WinOsInfoEx.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);

        BOOL (__stdcall *pfnGetVersionExW)(OSVERSIONINFOW *);
        *(FARPROC *)&pfnGetVersionExW = GetProcAddress(g_hModKernel32, "GetVersionExW");

        if (!pfnGetVersionExW || !pfnGetVersionExW((POSVERSIONINFOW)&g_WinOsInfoEx))
        {
            /*
             * If that didn't work either, just get the basic version bits.
             */
            RT_ZERO(g_WinOsInfoEx);
            g_WinOsInfoEx.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
            if (pfnGetVersionExW && pfnGetVersionExW((POSVERSIONINFOW)&g_WinOsInfoEx))
                Assert(g_WinOsInfoEx.dwPlatformId != VER_PLATFORM_WIN32_NT || g_WinOsInfoEx.dwMajorVersion < 5);
            else
            {
                /*
                 * Okay, nothing worked, so use GetVersion.
                 *
                 * This should only happen if we're on NT 3.1 or NT 3.50.
                 * It should never happen for 64-bit builds.
                 */
#ifdef RT_ARCH_X86
                RT_ZERO(g_WinOsInfoEx);
                DWORD const dwVersion = GetVersion();

                /* Common fields: */
                g_WinOsInfoEx.dwMajorVersion        = dwVersion & 0xff;
                g_WinOsInfoEx.dwMinorVersion        = (dwVersion >> 8) & 0xff;
                if (!(dwVersion & RT_BIT_32(31)))
                    g_WinOsInfoEx.dwBuildNumber     = dwVersion >> 16;
                else
                    g_WinOsInfoEx.dwBuildNumber     = 511;
                g_WinOsInfoEx.dwPlatformId          = VER_PLATFORM_WIN32_NT;
                /** @todo get CSD from registry. */
#else
                AssertBreakpoint();
                RT_ZERO(g_WinOsInfoEx);
#endif
            }

#ifdef RT_ARCH_X86
            /*
             * Fill in some of the extended info too.
             */
            g_WinOsInfoEx.dwOSVersionInfoSize       = sizeof(OSVERSIONINFOEXW); /* Pretend. */
            g_WinOsInfoEx.wProductType              = VER_NT_WORKSTATION;
            NT_PRODUCT_TYPE enmProdType = NtProductWinNt;
            if (RtlGetNtProductType(&enmProdType))
                g_WinOsInfoEx.wProductType = (BYTE)enmProdType;
            /** @todo parse the CSD string to figure that version. */
#endif
        }
    }

    if (g_WinOsInfoEx.dwOSVersionInfoSize)
        g_enmWinVer = rtR3InitWinSimplifiedVersion(&g_WinOsInfoEx);
}


/**
 * Resolves the winsock error APIs.
 */
static void rtR3InitWinSockApis(void)
{
    /*
     * Try get ws2_32.dll, then try load it, then finally fall back to the old
     * wsock32.dll.  We use RTLdrLoadSystem to the loading as it has all the fancy
     * logic for safely doing that.
     */
    g_hModWinSock = GetModuleHandleW(L"ws2_32.dll");
    if (g_hModWinSock == NULL)
    {
        RTLDRMOD hLdrMod;
        int rc = RTLdrLoadSystem("ws2_32.dll", true /*fNoUnload*/, &hLdrMod);
        if (RT_FAILURE(rc))
        {
            rc = RTLdrLoadSystem("wsock32.dll", true /*fNoUnload*/, &hLdrMod);
            if (RT_FAILURE(rc))
            {
                AssertMsgFailed(("rc=%Rrc\n", rc));
                return;
            }
            g_fOldWinSock = true;
        }
        g_hModWinSock = (HMODULE)RTLdrGetNativeHandle(hLdrMod);
        RTLdrClose(hLdrMod);
    }

    g_pfnWSAStartup           = (decltype(g_pfnWSAStartup))         GetProcAddress(g_hModWinSock, "WSAStartup");
    g_pfnWSACleanup           = (decltype(g_pfnWSACleanup))         GetProcAddress(g_hModWinSock, "WSACleanup");
    g_pfnWSAGetLastError      = (decltype(g_pfnWSAGetLastError))    GetProcAddress(g_hModWinSock, "WSAGetLastError");
    g_pfnWSASetLastError      = (decltype(g_pfnWSASetLastError))    GetProcAddress(g_hModWinSock, "WSASetLastError");
    g_pfnWSACreateEvent       = (decltype(g_pfnWSACreateEvent))     GetProcAddress(g_hModWinSock, "WSACreateEvent");
    g_pfnWSACloseEvent        = (decltype(g_pfnWSACloseEvent))      GetProcAddress(g_hModWinSock, "WSACloseEvent");
    g_pfnWSASetEvent          = (decltype(g_pfnWSASetEvent))        GetProcAddress(g_hModWinSock, "WSASetEvent");
    g_pfnWSAEventSelect       = (decltype(g_pfnWSAEventSelect))     GetProcAddress(g_hModWinSock, "WSAEventSelect");
    g_pfnWSAEnumNetworkEvents = (decltype(g_pfnWSAEnumNetworkEvents))GetProcAddress(g_hModWinSock,"WSAEnumNetworkEvents");
    g_pfnWSASocketW           = (decltype(g_pfnWSASocketW))         GetProcAddress(g_hModWinSock, "WSASocketW");
    g_pfnWSASend              = (decltype(g_pfnWSASend))            GetProcAddress(g_hModWinSock, "WSASend");
    g_pfnWSAIoctl             = (decltype(g_pfnWSAIoctl))           GetProcAddress(g_hModWinSock, "WSAIoctl");
    g_pfnsocket               = (decltype(g_pfnsocket))             GetProcAddress(g_hModWinSock, "socket");
    g_pfnclosesocket          = (decltype(g_pfnclosesocket))        GetProcAddress(g_hModWinSock, "closesocket");
    g_pfnrecv                 = (decltype(g_pfnrecv))               GetProcAddress(g_hModWinSock, "recv");
    g_pfnsend                 = (decltype(g_pfnsend))               GetProcAddress(g_hModWinSock, "send");
    g_pfnrecvfrom             = (decltype(g_pfnrecvfrom))           GetProcAddress(g_hModWinSock, "recvfrom");
    g_pfnsendto               = (decltype(g_pfnsendto))             GetProcAddress(g_hModWinSock, "sendto");
    g_pfnbind                 = (decltype(g_pfnbind))               GetProcAddress(g_hModWinSock, "bind");
    g_pfnlisten               = (decltype(g_pfnlisten))             GetProcAddress(g_hModWinSock, "listen");
    g_pfnaccept               = (decltype(g_pfnaccept))             GetProcAddress(g_hModWinSock, "accept");
    g_pfnconnect              = (decltype(g_pfnconnect))            GetProcAddress(g_hModWinSock, "connect");
    g_pfnshutdown             = (decltype(g_pfnshutdown))           GetProcAddress(g_hModWinSock, "shutdown");
    g_pfngetsockopt           = (decltype(g_pfngetsockopt))         GetProcAddress(g_hModWinSock, "getsockopt");
    g_pfnsetsockopt           = (decltype(g_pfnsetsockopt))         GetProcAddress(g_hModWinSock, "setsockopt");
    g_pfnioctlsocket          = (decltype(g_pfnioctlsocket))        GetProcAddress(g_hModWinSock, "ioctlsocket");
    g_pfngetpeername          = (decltype(g_pfngetpeername))        GetProcAddress(g_hModWinSock, "getpeername");
    g_pfngetsockname          = (decltype(g_pfngetsockname))        GetProcAddress(g_hModWinSock, "getsockname");
    g_pfn__WSAFDIsSet         = (decltype(g_pfn__WSAFDIsSet))       GetProcAddress(g_hModWinSock, "__WSAFDIsSet");
    g_pfnselect               = (decltype(g_pfnselect))             GetProcAddress(g_hModWinSock, "select");
    g_pfngethostbyname        = (decltype(g_pfngethostbyname))      GetProcAddress(g_hModWinSock, "gethostbyname");

    Assert(g_pfnWSAStartup);
    Assert(g_pfnWSACleanup);
    Assert(g_pfnWSAGetLastError);
    Assert(g_pfnWSASetLastError);
    Assert(g_pfnWSACreateEvent       || g_fOldWinSock);
    Assert(g_pfnWSACloseEvent        || g_fOldWinSock);
    Assert(g_pfnWSASetEvent          || g_fOldWinSock);
    Assert(g_pfnWSAEventSelect       || g_fOldWinSock);
    Assert(g_pfnWSAEnumNetworkEvents || g_fOldWinSock);
    Assert(g_pfnWSASocketW           || g_fOldWinSock);
    Assert(g_pfnWSASend              || g_fOldWinSock);
    Assert(g_pfnWSAIoctl             || g_fOldWinSock);
    Assert(g_pfnsocket);
    Assert(g_pfnclosesocket);
    Assert(g_pfnrecv);
    Assert(g_pfnsend);
    Assert(g_pfnrecvfrom);
    Assert(g_pfnsendto);
    Assert(g_pfnbind);
    Assert(g_pfnlisten);
    Assert(g_pfnaccept);
    Assert(g_pfnconnect);
    Assert(g_pfnshutdown);
    Assert(g_pfngetsockopt);
    Assert(g_pfnsetsockopt);
    Assert(g_pfnioctlsocket);
    Assert(g_pfngetpeername);
    Assert(g_pfngetsockname);
    Assert(g_pfn__WSAFDIsSet);
    Assert(g_pfnselect);
    Assert(g_pfngethostbyname);
}


static int rtR3InitNativeObtrusiveWorker(uint32_t fFlags)
{
    /*
     * Disable error popups.
     */
    UINT fOldErrMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX | fOldErrMode);

    /*
     * Restrict DLL searching for the process on windows versions which allow
     * us to do so.
     *  - The first trick works on XP SP1+ and disables the searching of the
     *    current directory.
     *  - The second trick is W7 w/ KB2533623 and W8+, it restrict the DLL
     *    searching to the application directory (except when
     *    RTR3INIT_FLAGS_STANDALONE_APP is given) and the System32 directory.
     */
    int rc = VINF_SUCCESS;

    typedef BOOL (WINAPI *PFNSETDLLDIRECTORY)(LPCWSTR);
    PFNSETDLLDIRECTORY pfnSetDllDir = (PFNSETDLLDIRECTORY)GetProcAddress(g_hModKernel32, "SetDllDirectoryW");
    if (pfnSetDllDir)
    {
        if (pfnSetDllDir(L""))
            g_enmWinLdrProt = RTR3WINLDRPROT_NO_CWD;
        else
            rc = VERR_INTERNAL_ERROR_3;
    }

    /** @bugref{6861} Observed GUI issues on Vista (32-bit and 64-bit) when using
     *                SetDefaultDllDirectories.
     *  @bugref{8194} Try use SetDefaultDllDirectories on Vista for standalone apps
     *                despite potential GUI issues. */
    if (   g_enmWinVer > kRTWinOSType_VISTA
        || (fFlags & RTR3INIT_FLAGS_STANDALONE_APP))
    {
        typedef BOOL(WINAPI *PFNSETDEFAULTDLLDIRECTORIES)(DWORD);
        PFNSETDEFAULTDLLDIRECTORIES pfnSetDefDllDirs;
        pfnSetDefDllDirs = (PFNSETDEFAULTDLLDIRECTORIES)GetProcAddress(g_hModKernel32, "SetDefaultDllDirectories");
        if (pfnSetDefDllDirs)
        {
            DWORD fDllDirs = LOAD_LIBRARY_SEARCH_SYSTEM32;
            if (!(fFlags & RTR3INIT_FLAGS_STANDALONE_APP))
                fDllDirs |= LOAD_LIBRARY_SEARCH_APPLICATION_DIR;
            if (pfnSetDefDllDirs(fDllDirs))
                g_enmWinLdrProt = fDllDirs & LOAD_LIBRARY_SEARCH_APPLICATION_DIR ? RTR3WINLDRPROT_SAFE : RTR3WINLDRPROT_SAFER;
            else if (RT_SUCCESS(rc))
                rc = VERR_INTERNAL_ERROR_4;
        }
    }

    /*
     * Register an unhandled exception callback if we can.
     */
    g_pfnGetCurrentThreadStackLimits = (PFNGETCURRENTTHREADSTACKLIMITS)GetProcAddress(g_hModKernel32, "GetCurrentThreadStackLimits");
    g_pfnSetUnhandledExceptionFilter = (decltype(SetUnhandledExceptionFilter) *)GetProcAddress(g_hModKernel32, "SetUnhandledExceptionFilter");
    g_pfnUnhandledExceptionFilter    = (decltype(UnhandledExceptionFilter) *)   GetProcAddress(g_hModKernel32, "UnhandledExceptionFilter");
    if (g_pfnSetUnhandledExceptionFilter && !g_pfnUnhandledXcptFilter)
    {
        g_pfnUnhandledXcptFilter = g_pfnSetUnhandledExceptionFilter(rtR3WinUnhandledXcptFilter);
        AssertStmt(g_pfnUnhandledXcptFilter != rtR3WinUnhandledXcptFilter, g_pfnUnhandledXcptFilter = NULL);
    }

    return rc;
}


DECLHIDDEN(int) rtR3InitNativeFirst(uint32_t fFlags)
{
    /*
     * Make sure we've got the handles of the two main Windows NT dlls.
     */
    g_hModKernel32 = GetModuleHandleW(L"kernel32.dll");
    if (g_hModKernel32 == NULL)
        return VERR_INTERNAL_ERROR_2;
    g_hModNtDll    = GetModuleHandleW(L"ntdll.dll");
    if (g_hModNtDll == NULL)
        return VERR_INTERNAL_ERROR_2;

    rtR3InitWindowsVersion();

    int rc = VINF_SUCCESS;
    if (!(fFlags & RTR3INIT_FLAGS_UNOBTRUSIVE))
        rc = rtR3InitNativeObtrusiveWorker(fFlags);

    /*
     * Resolve some kernel32.dll APIs we may need but aren't necessarily
     * present in older windows versions.
     */
    g_pfnGetSystemWindowsDirectoryW      = (PFNGETWINSYSDIR)GetProcAddress(g_hModKernel32, "GetSystemWindowsDirectoryW");
    if (g_pfnGetSystemWindowsDirectoryW)
        g_pfnGetSystemWindowsDirectoryW  = (PFNGETWINSYSDIR)GetProcAddress(g_hModKernel32, "GetWindowsDirectoryW");
    g_pfnSystemTimeToTzSpecificLocalTime = (decltype(SystemTimeToTzSpecificLocalTime) *)GetProcAddress(g_hModKernel32, "SystemTimeToTzSpecificLocalTime");
    g_pfnCreateWaitableTimerExW     = (PFNCREATEWAITABLETIMEREX)              GetProcAddress(g_hModKernel32, "CreateWaitableTimerExW");
    g_pfnGetHandleInformation       = (decltype(GetHandleInformation) *)      GetProcAddress(g_hModKernel32, "GetHandleInformation");
    g_pfnSetHandleInformation       = (decltype(SetHandleInformation) *)      GetProcAddress(g_hModKernel32, "SetHandleInformation");
    g_pfnIsDebuggerPresent          = (decltype(IsDebuggerPresent) *)         GetProcAddress(g_hModKernel32, "IsDebuggerPresent");
    g_pfnGetSystemTimeAsFileTime    = (decltype(GetSystemTimeAsFileTime) *)   GetProcAddress(g_hModKernel32, "GetSystemTimeAsFileTime");
    g_pfnGetProcessAffinityMask     = (decltype(GetProcessAffinityMask) *)    GetProcAddress(g_hModKernel32, "GetProcessAffinityMask");
    g_pfnSetThreadAffinityMask      = (decltype(SetThreadAffinityMask) *)     GetProcAddress(g_hModKernel32, "SetThreadAffinityMask");
    g_pfnCreateIoCompletionPort     = (decltype(CreateIoCompletionPort) *)    GetProcAddress(g_hModKernel32, "CreateIoCompletionPort");
    g_pfnGetQueuedCompletionStatus  = (decltype(GetQueuedCompletionStatus) *) GetProcAddress(g_hModKernel32, "GetQueuedCompletionStatus");
    g_pfnPostQueuedCompletionStatus = (decltype(PostQueuedCompletionStatus) *)GetProcAddress(g_hModKernel32, "PostQueuedCompletionStatus");
    g_pfnIsProcessorFeaturePresent  = (decltype(IsProcessorFeaturePresent) *) GetProcAddress(g_hModKernel32, "IsProcessorFeaturePresent");

    Assert(g_pfnGetHandleInformation       || g_enmWinVer < kRTWinOSType_NT351);
    Assert(g_pfnSetHandleInformation       || g_enmWinVer < kRTWinOSType_NT351);
    Assert(g_pfnIsDebuggerPresent          || g_enmWinVer < kRTWinOSType_NT4);
    Assert(g_pfnGetSystemTimeAsFileTime    || g_enmWinVer < kRTWinOSType_NT4);
    Assert(g_pfnGetProcessAffinityMask     || g_enmWinVer < kRTWinOSType_NT350);
    Assert(g_pfnSetThreadAffinityMask      || g_enmWinVer < kRTWinOSType_NT350);
    Assert(g_pfnCreateIoCompletionPort     || g_enmWinVer < kRTWinOSType_NT350);
    Assert(g_pfnGetQueuedCompletionStatus  || g_enmWinVer < kRTWinOSType_NT350);
    Assert(g_pfnPostQueuedCompletionStatus || g_enmWinVer < kRTWinOSType_NT350);
    Assert(g_pfnIsProcessorFeaturePresent  || g_enmWinVer < kRTWinOSType_NT4);

    /*
     * Resolve some ntdll.dll APIs that weren't there in early NT versions.
     */
    g_pfnNtQueryFullAttributesFile  = (PFNNTQUERYFULLATTRIBUTESFILE)GetProcAddress(g_hModNtDll, "NtQueryFullAttributesFile");
    g_pfnNtDuplicateToken           = (PFNNTDUPLICATETOKEN)GetProcAddress(         g_hModNtDll, "NtDuplicateToken");
    g_pfnNtAlertThread              = (decltype(NtAlertThread) *)GetProcAddress(   g_hModNtDll, "NtAlertThread");

    /*
     * Resolve the winsock error getter and setter so assertions can save those too.
     */
    rtR3InitWinSockApis();

    return rc;
}


DECLHIDDEN(void) rtR3InitNativeObtrusive(uint32_t fFlags)
{
    rtR3InitNativeObtrusiveWorker(fFlags);
}


DECLHIDDEN(int) rtR3InitNativeFinal(uint32_t fFlags)
{
    /* Nothing to do here. */
    RT_NOREF_PV(fFlags);
    return VINF_SUCCESS;
}


/**
 * Unhandled exception filter callback.
 *
 * Will try log stuff.
 */
static LONG CALLBACK rtR3WinUnhandledXcptFilter(PEXCEPTION_POINTERS pPtrs)
{
    /*
     * Try get the logger and log exception details.
     *
     * Note! We'll be using RTLogLoggerWeak for now, though we should probably add
     *       a less deadlock prone API here and gives up pretty fast if it
     *       cannot get the lock...
     */
    PRTLOGGER pLogger = RTLogRelGetDefaultInstanceWeak();
    if (!pLogger)
        pLogger = RTLogGetDefaultInstanceWeak();
    if (pLogger)
    {
        RTLogLoggerWeak(pLogger, NULL, "\n!!! rtR3WinUnhandledXcptFilter caught an exception on thread %p in %u !!!\n",
                        RTThreadNativeSelf(), RTProcSelf());

        /*
         * Dump the exception record.
         */
        uintptr_t         uXcptPC  = 0;
        PEXCEPTION_RECORD pXcptRec = RT_VALID_PTR(pPtrs) && RT_VALID_PTR(pPtrs->ExceptionRecord) ? pPtrs->ExceptionRecord : NULL;
        if (pXcptRec)
        {
            RTLogLoggerWeak(pLogger, NULL, "\nExceptionCode=%#010x ExceptionFlags=%#010x ExceptionAddress=%p\n",
                            pXcptRec->ExceptionCode, pXcptRec->ExceptionFlags, pXcptRec->ExceptionAddress);
            for (uint32_t i = 0; i < RT_MIN(pXcptRec->NumberParameters, EXCEPTION_MAXIMUM_PARAMETERS); i++)
                RTLogLoggerWeak(pLogger, NULL, "ExceptionInformation[%d]=%p\n", i, pXcptRec->ExceptionInformation[i]);
            uXcptPC = (uintptr_t)pXcptRec->ExceptionAddress;

            /* Nested? Display one level only. */
            PEXCEPTION_RECORD pNestedRec = pXcptRec->ExceptionRecord;
            if (RT_VALID_PTR(pNestedRec))
            {
                RTLogLoggerWeak(pLogger, NULL, "Nested: ExceptionCode=%#010x ExceptionFlags=%#010x ExceptionAddress=%p (nested %p)\n",
                                pNestedRec->ExceptionCode, pNestedRec->ExceptionFlags, pNestedRec->ExceptionAddress,
                                pNestedRec->ExceptionRecord);
                for (uint32_t i = 0; i < RT_MIN(pNestedRec->NumberParameters, EXCEPTION_MAXIMUM_PARAMETERS); i++)
                    RTLogLoggerWeak(pLogger, NULL, "Nested: ExceptionInformation[%d]=%p\n", i, pNestedRec->ExceptionInformation[i]);
                uXcptPC = (uintptr_t)pNestedRec->ExceptionAddress;
            }
        }

        /*
         * Dump the context record.
         */
        volatile char   szMarker[] = "stackmarker";
        uintptr_t       uXcptSP = (uintptr_t)&szMarker[0];
        PCONTEXT pXcptCtx = RT_VALID_PTR(pPtrs) && RT_VALID_PTR(pPtrs->ContextRecord)   ? pPtrs->ContextRecord   : NULL;
        if (pXcptCtx)
        {
#ifdef RT_ARCH_AMD64
            RTLogLoggerWeak(pLogger, NULL, "\ncs:rip=%04x:%016RX64\n", pXcptCtx->SegCs, pXcptCtx->Rip);
            RTLogLoggerWeak(pLogger, NULL, "ss:rsp=%04x:%016RX64 rbp=%016RX64\n", pXcptCtx->SegSs, pXcptCtx->Rsp, pXcptCtx->Rbp);
            RTLogLoggerWeak(pLogger, NULL, "rax=%016RX64 rcx=%016RX64 rdx=%016RX64 rbx=%016RX64\n",
                            pXcptCtx->Rax, pXcptCtx->Rcx, pXcptCtx->Rdx, pXcptCtx->Rbx);
            RTLogLoggerWeak(pLogger, NULL, "rsi=%016RX64 rdi=%016RX64 rsp=%016RX64 rbp=%016RX64\n",
                            pXcptCtx->Rsi, pXcptCtx->Rdi, pXcptCtx->Rsp, pXcptCtx->Rbp);
            RTLogLoggerWeak(pLogger, NULL, "r8 =%016RX64 r9 =%016RX64 r10=%016RX64 r11=%016RX64\n",
                            pXcptCtx->R8,  pXcptCtx->R9,  pXcptCtx->R10, pXcptCtx->R11);
            RTLogLoggerWeak(pLogger, NULL, "r12=%016RX64 r13=%016RX64 r14=%016RX64 r15=%016RX64\n",
                            pXcptCtx->R12,  pXcptCtx->R13,  pXcptCtx->R14, pXcptCtx->R15);
            RTLogLoggerWeak(pLogger, NULL, "ds=%04x es=%04x fs=%04x gs=%04x eflags=%08x\n",
                            pXcptCtx->SegDs, pXcptCtx->SegEs, pXcptCtx->SegFs, pXcptCtx->SegGs, pXcptCtx->EFlags);
            RTLogLoggerWeak(pLogger, NULL, "p1home=%016RX64 p2home=%016RX64 pe3home=%016RX64\n",
                            pXcptCtx->P1Home, pXcptCtx->P2Home, pXcptCtx->P3Home);
            RTLogLoggerWeak(pLogger, NULL, "p4home=%016RX64 p5home=%016RX64 pe6home=%016RX64\n",
                            pXcptCtx->P4Home, pXcptCtx->P5Home, pXcptCtx->P6Home);
            RTLogLoggerWeak(pLogger, NULL, "   LastBranchToRip=%016RX64    LastBranchFromRip=%016RX64\n",
                            pXcptCtx->LastBranchToRip, pXcptCtx->LastBranchFromRip);
            RTLogLoggerWeak(pLogger, NULL, "LastExceptionToRip=%016RX64 LastExceptionFromRip=%016RX64\n",
                            pXcptCtx->LastExceptionToRip, pXcptCtx->LastExceptionFromRip);
            uXcptSP = pXcptCtx->Rsp;
            uXcptPC = pXcptCtx->Rip;

#elif defined(RT_ARCH_X86)
            RTLogLoggerWeak(pLogger, NULL, "\ncs:eip=%04x:%08RX32\n", pXcptCtx->SegCs, pXcptCtx->Eip);
            RTLogLoggerWeak(pLogger, NULL, "ss:esp=%04x:%08RX32 ebp=%08RX32\n", pXcptCtx->SegSs, pXcptCtx->Esp, pXcptCtx->Ebp);
            RTLogLoggerWeak(pLogger, NULL, "eax=%08RX32 ecx=%08RX32 edx=%08RX32 ebx=%08RX32\n",
                            pXcptCtx->Eax, pXcptCtx->Ecx,  pXcptCtx->Edx,  pXcptCtx->Ebx);
            RTLogLoggerWeak(pLogger, NULL, "esi=%08RX32 edi=%08RX32 esp=%08RX32 ebp=%08RX32\n",
                            pXcptCtx->Esi, pXcptCtx->Edi,  pXcptCtx->Esp,  pXcptCtx->Ebp);
            RTLogLoggerWeak(pLogger, NULL, "ds=%04x es=%04x fs=%04x gs=%04x eflags=%08x\n",
                            pXcptCtx->SegDs, pXcptCtx->SegEs, pXcptCtx->SegFs, pXcptCtx->SegGs, pXcptCtx->EFlags);
            uXcptSP = pXcptCtx->Esp;
            uXcptPC = pXcptCtx->Eip;
#endif
        }

        /*
         * Dump stack.
         */
        uintptr_t uStack = (uintptr_t)(void *)&szMarker[0];
        uStack -= uStack & 15;

        size_t cbToDump = PAGE_SIZE - (uStack & PAGE_OFFSET_MASK);
        if (cbToDump < 512)
            cbToDump += PAGE_SIZE;
        size_t cbToXcpt = uXcptSP - uStack;
        while (cbToXcpt > cbToDump && cbToXcpt <= _16K)
            cbToDump += PAGE_SIZE;
        ULONG_PTR uLow  = (uintptr_t)&szMarker[0];
        ULONG_PTR uHigh = (uintptr_t)&szMarker[0];
        if (g_pfnGetCurrentThreadStackLimits)
        {
            g_pfnGetCurrentThreadStackLimits(&uLow, &uHigh);
            size_t cbToTop = RT_MAX(uLow, uHigh) - uStack;
            if (cbToTop < _1M)
                cbToDump = cbToTop;
        }

        RTLogLoggerWeak(pLogger, NULL, "\nStack %p, dumping %#x bytes (low=%p, high=%p)\n", uStack, cbToDump, uLow, uHigh);
        RTLogLoggerWeak(pLogger, NULL, "%.*RhxD\n", cbToDump, uStack);

        /*
         * Try figure the thread name.
         *
         * Note! This involves the thread db lock, so it may deadlock, which
         *       is why it's at the end.
         */
        RTLogLoggerWeak(pLogger, NULL,  "Thread ID:   %p\n", RTThreadNativeSelf());
        RTLogLoggerWeak(pLogger, NULL,  "Thread name: %s\n", RTThreadSelfName());
        RTLogLoggerWeak(pLogger, NULL,  "Thread IPRT: %p\n", RTThreadSelf());

        /*
         * Try dump the load information.
         */
        PPEB pPeb = RTNtCurrentPeb();
        if (RT_VALID_PTR(pPeb))
        {
            PPEB_LDR_DATA pLdrData = pPeb->Ldr;
            if (RT_VALID_PTR(pLdrData))
            {
                PLDR_DATA_TABLE_ENTRY pFound     = NULL;
                LIST_ENTRY * const    pList      = &pLdrData->InMemoryOrderModuleList;
                LIST_ENTRY           *pListEntry = pList->Flink;
                uint32_t              cLoops     = 0;
                RTLogLoggerWeak(pLogger, NULL,
                                "\nLoaded Modules:\n"
                                "%-*s[*] Timestamp Path\n", sizeof(void *) * 4 + 2 - 1, "Address range"
                                );
                while (pListEntry != pList && RT_VALID_PTR(pListEntry) && cLoops < 1024)
                {
                    PLDR_DATA_TABLE_ENTRY pLdrEntry = RT_FROM_MEMBER(pListEntry, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
                    uint32_t const        cbLength  = (uint32_t)(uintptr_t)pLdrEntry->Reserved3[1];
                    char                  chInd     = ' ';
                    if (uXcptPC - (uintptr_t)pLdrEntry->DllBase < cbLength)
                    {
                        chInd = '*';
                        pFound = pLdrEntry;
                    }

                    if (   RT_VALID_PTR(pLdrEntry->FullDllName.Buffer)
                        && pLdrEntry->FullDllName.Length > 0
                        && pLdrEntry->FullDllName.Length < _8K
                        && (pLdrEntry->FullDllName.Length & 1) == 0
                        && pLdrEntry->FullDllName.Length <= pLdrEntry->FullDllName.MaximumLength)
                        RTLogLoggerWeak(pLogger, NULL, "%p..%p%c  %08RX32  %.*ls\n",
                                        pLdrEntry->DllBase, (uintptr_t)pLdrEntry->DllBase + cbLength - 1, chInd,
                                        pLdrEntry->TimeDateStamp, pLdrEntry->FullDllName.Length / sizeof(RTUTF16),
                                        pLdrEntry->FullDllName.Buffer);
                    else
                        RTLogLoggerWeak(pLogger, NULL, "%p..%p%c  %08RX32  <bad or missing: %p LB %#x max %#x\n",
                                        pLdrEntry->DllBase, (uintptr_t)pLdrEntry->DllBase + cbLength - 1, chInd,
                                        pLdrEntry->TimeDateStamp, pLdrEntry->FullDllName.Buffer, pLdrEntry->FullDllName.Length,
                                        pLdrEntry->FullDllName.MaximumLength);

                    /* advance */
                    pListEntry = pListEntry->Flink;
                    cLoops++;
                }

                /*
                 * Use the above to pick out code addresses on the stack.
                 */
                if (   cLoops < 1024
                    && uXcptSP - uStack < cbToDump)
                {
                    RTLogLoggerWeak(pLogger, NULL, "\nPotential code addresses on the stack:\n");
                    if (pFound)
                    {
                        if (   RT_VALID_PTR(pFound->FullDllName.Buffer)
                            && pFound->FullDllName.Length > 0
                            && pFound->FullDllName.Length < _8K
                            && (pFound->FullDllName.Length & 1) == 0
                            && pFound->FullDllName.Length <= pFound->FullDllName.MaximumLength)
                            RTLogLoggerWeak(pLogger, NULL, "%-*s: %p - %#010RX32 bytes into %.*ls\n",
                                            sizeof(void *) * 2, "Xcpt PC", uXcptPC, (uint32_t)(uXcptPC - (uintptr_t)pFound->DllBase),
                                            pFound->FullDllName.Length / sizeof(RTUTF16), pFound->FullDllName.Buffer);
                        else
                            RTLogLoggerWeak(pLogger, NULL, "%-*s: %p - %08RX32 into module at %p\n",
                                            sizeof(void *) * 2, "Xcpt PC", uXcptPC, (uint32_t)(uXcptPC - (uintptr_t)pFound->DllBase),
                                            pFound->DllBase);
                    }

                    uintptr_t const *puStack = (uintptr_t const *)uXcptSP;
                    uintptr_t        cLeft   = (cbToDump - (uXcptSP - uStack)) / sizeof(uintptr_t);
                    while (cLeft-- > 0)
                    {
                        uintptr_t uPtr = *puStack;
                        if (RT_VALID_PTR(uPtr))
                        {
                            /* Search the module table. */
                            pFound     = NULL;
                            cLoops     = 0;
                            pListEntry = pList->Flink;
                            while (pListEntry != pList && RT_VALID_PTR(pListEntry) && cLoops < 1024)
                            {
                                PLDR_DATA_TABLE_ENTRY pLdrEntry = RT_FROM_MEMBER(pListEntry, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
                                uint32_t const        cbLength  = (uint32_t)(uintptr_t)pLdrEntry->Reserved3[1];
                                if (uPtr - (uintptr_t)pLdrEntry->DllBase < cbLength)
                                {
                                    pFound = pLdrEntry;
                                    break;
                                }

                                /* advance */
                                pListEntry = pListEntry->Flink;
                                cLoops++;
                            }

                            if (pFound)
                            {
                                if (   RT_VALID_PTR(pFound->FullDllName.Buffer)
                                    && pFound->FullDllName.Length > 0
                                    && pFound->FullDllName.Length < _8K
                                    && (pFound->FullDllName.Length & 1) == 0
                                    && pFound->FullDllName.Length <= pFound->FullDllName.MaximumLength)
                                    RTLogLoggerWeak(pLogger, NULL, "%p: %p - %#010RX32 bytes into %.*ls\n",
                                                    puStack, uPtr, (uint32_t)(uPtr - (uintptr_t)pFound->DllBase),
                                                    pFound->FullDllName.Length / sizeof(RTUTF16), pFound->FullDllName.Buffer);
                                else
                                    RTLogLoggerWeak(pLogger, NULL, "%p: %p - %08RX32 into module at %p\n",
                                                    puStack, uPtr, (uint32_t)(uPtr - (uintptr_t)pFound->DllBase), pFound->DllBase);
                            }
                        }

                        puStack++;
                    }
                }
            }

            /*
             * Dump the command line if we have one. We do this last in case it crashes.
             */
            PRTL_USER_PROCESS_PARAMETERS pProcParams = pPeb->ProcessParameters;
            if (RT_VALID_PTR(pProcParams))
            {
                if (RT_VALID_PTR(pProcParams->CommandLine.Buffer)
                    && pProcParams->CommandLine.Length > 0
                    && pProcParams->CommandLine.Length <= pProcParams->CommandLine.MaximumLength
                    && !(pProcParams->CommandLine.Length & 1)
                    && !(pProcParams->CommandLine.MaximumLength & 1))
                    RTLogLoggerWeak(pLogger, NULL, "PEB/CommandLine: %.*ls\n",
                                    pProcParams->CommandLine.Length / sizeof(RTUTF16), pProcParams->CommandLine.Buffer);
            }
        }
    }

    /*
     * Do the default stuff, never mind us.
     */
    if (g_pfnUnhandledXcptFilter)
        return g_pfnUnhandledXcptFilter(pPtrs);
    return EXCEPTION_CONTINUE_SEARCH;
}

