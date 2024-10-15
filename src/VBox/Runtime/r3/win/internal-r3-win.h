/* $Id: internal-r3-win.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - some Windows OS type constants.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_SRC_r3_win_internal_r3_win_h
#define IPRT_INCLUDED_SRC_r3_win_internal_r3_win_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "internal/iprt.h"
#include <iprt/types.h>


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * Windows OS type as determined by rtSystemWinOSType().
 *
 * @note ASSUMPTIONS are made regarding ordering. Win 9x should come first, then
 *       NT. The Win9x and NT versions should internally be ordered in ascending
 *       version/code-base order.
 */
typedef enum RTWINOSTYPE
{
    kRTWinOSType_UNKNOWN    = 0,
    kRTWinOSType_9XFIRST    = 1,
    kRTWinOSType_95         = kRTWinOSType_9XFIRST,
    kRTWinOSType_95SP1,
    kRTWinOSType_95OSR2,
    kRTWinOSType_98,
    kRTWinOSType_98SP1,
    kRTWinOSType_98SE,
    kRTWinOSType_ME,
    kRTWinOSType_9XLAST     = 99,
    kRTWinOSType_NTFIRST    = 100,
    kRTWinOSType_NT310      = kRTWinOSType_NTFIRST,
    kRTWinOSType_NT350,
    kRTWinOSType_NT351,
    kRTWinOSType_NT4,
    kRTWinOSType_2K,                        /* 5.0 */
    kRTWinOSType_XP,                        /* 5.1 */
    kRTWinOSType_XP64,                      /* 5.2, workstation */
    kRTWinOSType_2003,                      /* 5.2 */
    kRTWinOSType_VISTA,                     /* 6.0, workstation */
    kRTWinOSType_2008,                      /* 6.0, server */
    kRTWinOSType_7,                         /* 6.1, workstation */
    kRTWinOSType_2008R2,                    /* 6.1, server */
    kRTWinOSType_8,                         /* 6.2, workstation */
    kRTWinOSType_2012,                      /* 6.2, server */
    kRTWinOSType_81,                        /* 6.3, workstation */
    kRTWinOSType_2012R2,                    /* 6.3, server */
    kRTWinOSType_10,                        /* 10.0, workstation */
    kRTWinOSType_2016,                      /* 10.0 1607, server */
    kRTWinOSType_2019,                      /* 10.0 1809, server */
    kRTWinOSType_2022,                      /* 10.0 21H2, server */
    kRTWinOSType_11,                        /* 11.0, workstation */
    kRTWinOSType_NT_UNKNOWN = 199,
    kRTWinOSType_NT_LAST    = kRTWinOSType_UNKNOWN
} RTWINOSTYPE;

/**
 * Windows loader protection level.
 */
typedef enum RTR3WINLDRPROT
{
    RTR3WINLDRPROT_INVALID = 0,
    RTR3WINLDRPROT_NONE,
    RTR3WINLDRPROT_NO_CWD,
    RTR3WINLDRPROT_SAFE,
    RTR3WINLDRPROT_SAFER
} RTR3WINLDRPROT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
extern DECL_HIDDEN_DATA(RTR3WINLDRPROT)                 g_enmWinLdrProt;
extern DECL_HIDDEN_DATA(RTWINOSTYPE)                    g_enmWinVer;
#ifdef _WINDEF_
extern DECL_HIDDEN_DATA(OSVERSIONINFOEXW)               g_WinOsInfoEx;

extern DECL_HIDDEN_DATA(HMODULE)                        g_hModKernel32;
typedef UINT (WINAPI *PFNGETWINSYSDIR)(LPWSTR,UINT);
extern DECL_HIDDEN_DATA(PFNGETWINSYSDIR)                                g_pfnGetSystemWindowsDirectoryW;
typedef HANDLE (WINAPI *PFNCREATEWAITABLETIMEREX)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
extern DECL_HIDDEN_DATA(PFNCREATEWAITABLETIMEREX)                       g_pfnCreateWaitableTimerExW;
extern DECL_HIDDEN_DATA(decltype(SystemTimeToTzSpecificLocalTime) *)    g_pfnSystemTimeToTzSpecificLocalTime;
extern DECL_HIDDEN_DATA(decltype(GetHandleInformation) *)               g_pfnGetHandleInformation;
extern DECL_HIDDEN_DATA(decltype(SetHandleInformation) *)               g_pfnSetHandleInformation;
extern DECL_HIDDEN_DATA(decltype(IsDebuggerPresent) *)                  g_pfnIsDebuggerPresent;
extern DECL_HIDDEN_DATA(decltype(GetSystemTimeAsFileTime) *)            g_pfnGetSystemTimeAsFileTime;
extern DECL_HIDDEN_DATA(decltype(GetProcessAffinityMask) *)             g_pfnGetProcessAffinityMask;
extern DECL_HIDDEN_DATA(decltype(SetThreadAffinityMask) *)              g_pfnSetThreadAffinityMask;
extern DECL_HIDDEN_DATA(decltype(CreateIoCompletionPort) *)             g_pfnCreateIoCompletionPort;
extern DECL_HIDDEN_DATA(decltype(GetQueuedCompletionStatus) *)          g_pfnGetQueuedCompletionStatus;
extern DECL_HIDDEN_DATA(decltype(PostQueuedCompletionStatus) *)         g_pfnPostQueuedCompletionStatus;
extern DECL_HIDDEN_DATA(decltype(SetUnhandledExceptionFilter) *)        g_pfnSetUnhandledExceptionFilter;
extern DECL_HIDDEN_DATA(decltype(UnhandledExceptionFilter) *)           g_pfnUnhandledExceptionFilter;
extern DECL_HIDDEN_DATA(decltype(IsProcessorFeaturePresent) *)          g_pfnIsProcessorFeaturePresent;


extern DECL_HIDDEN_DATA(HMODULE)                        g_hModNtDll;
typedef NTSTATUS (NTAPI *PFNNTQUERYFULLATTRIBUTESFILE)(struct _OBJECT_ATTRIBUTES *, struct _FILE_NETWORK_OPEN_INFORMATION *);
extern DECL_HIDDEN_DATA(PFNNTQUERYFULLATTRIBUTESFILE)   g_pfnNtQueryFullAttributesFile;
typedef NTSTATUS (NTAPI *PFNNTDUPLICATETOKEN)(HANDLE, ACCESS_MASK, struct _OBJECT_ATTRIBUTES *, BOOLEAN, TOKEN_TYPE, PHANDLE);
extern DECL_HIDDEN_DATA(PFNNTDUPLICATETOKEN)            g_pfnNtDuplicateToken;
#ifdef IPRT_INCLUDED_nt_nt_h
extern DECL_HIDDEN_DATA(decltype(NtAlertThread) *)      g_pfnNtAlertThread;
#endif

extern DECL_HIDDEN_DATA(HMODULE)                        g_hModWinSock;

/** WSAStartup */
typedef int             (WINAPI *PFNWSASTARTUP)(WORD, struct WSAData *);
/** WSACleanup */
typedef int             (WINAPI *PFNWSACLEANUP)(void);
/** WSAGetLastError */
typedef int             (WINAPI *PFNWSAGETLASTERROR)(void);
/** WSASetLastError */
typedef int             (WINAPI *PFNWSASETLASTERROR)(int);
/** WSACreateEvent */
typedef HANDLE          (WINAPI *PFNWSACREATEEVENT)(void);
/** WSASetEvent */
typedef BOOL            (WINAPI *PFNWSASETEVENT)(HANDLE);
/** WSACloseEvent */
typedef BOOL            (WINAPI *PFNWSACLOSEEVENT)(HANDLE);
/** WSAEventSelect */
typedef BOOL            (WINAPI *PFNWSAEVENTSELECT)(UINT_PTR, HANDLE, LONG);
/** WSAEnumNetworkEvents */
typedef int             (WINAPI *PFNWSAENUMNETWORKEVENTS)(UINT_PTR, HANDLE, struct _WSANETWORKEVENTS *);
/** WSASocketW */
typedef UINT_PTR        (WINAPI *PFNWSASOCKETW)(int, int, int, struct _WSAPROTOCOL_INFOW *, unsigned, DWORD);
/** WSASend */
typedef int             (WINAPI *PFNWSASEND)(UINT_PTR, struct _WSABUF *, DWORD, LPDWORD, DWORD dwFlags,
                                             struct _OVERLAPPED *, uintptr_t /*LPWSAOVERLAPPED_COMPLETION_ROUTINE*/);
/** WSAIoctl */
typedef int             (WINAPI *PFNWSAIOCTL)(UINT_PTR, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD,
                                             struct _OVERLAPPED *, uintptr_t /*LPWSAOVERLAPPED_COMPLETION_ROUTINE*/);

/** socket */
typedef UINT_PTR        (WINAPI *PFNWINSOCKSOCKET)(int, int, int);
/** closesocket */
typedef int             (WINAPI *PFNWINSOCKCLOSESOCKET)(UINT_PTR);
/** recv */
typedef int             (WINAPI *PFNWINSOCKRECV)(UINT_PTR, char *, int, int);
/** send */
typedef int             (WINAPI *PFNWINSOCKSEND)(UINT_PTR, const char *, int, int);
/** recvfrom */
typedef int             (WINAPI *PFNWINSOCKRECVFROM)(UINT_PTR, char *, int, int, struct sockaddr *, int *);
/** sendto */
typedef int             (WINAPI *PFNWINSOCKSENDTO)(UINT_PTR, const char *, int, int, const struct sockaddr *, int);
/** bind */
typedef int             (WINAPI *PFNWINSOCKBIND)(UINT_PTR, const struct sockaddr *, int);
/** listen  */
typedef int             (WINAPI *PFNWINSOCKLISTEN)(UINT_PTR, int);
/** accept */
typedef UINT_PTR        (WINAPI *PFNWINSOCKACCEPT)(UINT_PTR, struct sockaddr *, int *);
/** connect */
typedef int             (WINAPI *PFNWINSOCKCONNECT)(UINT_PTR, const struct sockaddr *, int);
/** shutdown */
typedef int             (WINAPI *PFNWINSOCKSHUTDOWN)(UINT_PTR, int);
/** getsockopt */
typedef int             (WINAPI *PFNWINSOCKGETSOCKOPT)(UINT_PTR, int, int, char *, int *);
/** setsockopt */
typedef int             (WINAPI *PFNWINSOCKSETSOCKOPT)(UINT_PTR, int, int, const char *, int);
/** ioctlsocket */
typedef int             (WINAPI *PFNWINSOCKIOCTLSOCKET)(UINT_PTR, long, unsigned long *);
/** getpeername   */
typedef int             (WINAPI *PFNWINSOCKGETPEERNAME)(UINT_PTR, struct sockaddr *, int *);
/** getsockname */
typedef int             (WINAPI *PFNWINSOCKGETSOCKNAME)(UINT_PTR, struct sockaddr *, int *);
/** __WSAFDIsSet */
typedef int             (WINAPI *PFNWINSOCK__WSAFDISSET)(UINT_PTR, struct fd_set *);
/** select */
typedef int             (WINAPI *PFNWINSOCKSELECT)(int, struct fd_set *, struct fd_set *, struct fd_set *, const struct timeval *);
/** gethostbyname */
typedef struct hostent *(WINAPI *PFNWINSOCKGETHOSTBYNAME)(const char *);

extern DECL_HIDDEN_DATA(PFNWSASTARTUP)                   g_pfnWSAStartup;
extern DECL_HIDDEN_DATA(PFNWSACLEANUP)                   g_pfnWSACleanup;
extern DECL_HIDDEN_DATA(PFNWSAGETLASTERROR)              g_pfnWSAGetLastError;
extern DECL_HIDDEN_DATA(PFNWSASETLASTERROR)              g_pfnWSASetLastError;
extern DECL_HIDDEN_DATA(PFNWSACREATEEVENT)               g_pfnWSACreateEvent;
extern DECL_HIDDEN_DATA(PFNWSACLOSEEVENT)                g_pfnWSACloseEvent;
extern DECL_HIDDEN_DATA(PFNWSASETEVENT)                  g_pfnWSASetEvent;
extern DECL_HIDDEN_DATA(PFNWSAEVENTSELECT)               g_pfnWSAEventSelect;
extern DECL_HIDDEN_DATA(PFNWSAENUMNETWORKEVENTS)         g_pfnWSAEnumNetworkEvents;
extern DECL_HIDDEN_DATA(PFNWSASOCKETW)                   g_pfnWSASocketW;
extern DECL_HIDDEN_DATA(PFNWSASEND)                      g_pfnWSASend;
extern DECL_HIDDEN_DATA(PFNWSAIOCTL)                     g_pfnWSAIoctl;
extern DECL_HIDDEN_DATA(PFNWINSOCKSOCKET)                g_pfnsocket;
extern DECL_HIDDEN_DATA(PFNWINSOCKCLOSESOCKET)           g_pfnclosesocket;
extern DECL_HIDDEN_DATA(PFNWINSOCKRECV)                  g_pfnrecv;
extern DECL_HIDDEN_DATA(PFNWINSOCKSEND)                  g_pfnsend;
extern DECL_HIDDEN_DATA(PFNWINSOCKRECVFROM)              g_pfnrecvfrom;
extern DECL_HIDDEN_DATA(PFNWINSOCKSENDTO)                g_pfnsendto;
extern DECL_HIDDEN_DATA(PFNWINSOCKBIND)                  g_pfnbind;
extern DECL_HIDDEN_DATA(PFNWINSOCKLISTEN)                g_pfnlisten;
extern DECL_HIDDEN_DATA(PFNWINSOCKACCEPT)                g_pfnaccept;
extern DECL_HIDDEN_DATA(PFNWINSOCKCONNECT)               g_pfnconnect;
extern DECL_HIDDEN_DATA(PFNWINSOCKSHUTDOWN)              g_pfnshutdown;
extern DECL_HIDDEN_DATA(PFNWINSOCKGETSOCKOPT)            g_pfngetsockopt;
extern DECL_HIDDEN_DATA(PFNWINSOCKSETSOCKOPT)            g_pfnsetsockopt;
extern DECL_HIDDEN_DATA(PFNWINSOCKIOCTLSOCKET)           g_pfnioctlsocket;
extern DECL_HIDDEN_DATA(PFNWINSOCKGETPEERNAME)           g_pfngetpeername;
extern DECL_HIDDEN_DATA(PFNWINSOCKGETSOCKNAME)           g_pfngetsockname;
extern DECL_HIDDEN_DATA(PFNWINSOCK__WSAFDISSET)          g_pfn__WSAFDIsSet;
extern DECL_HIDDEN_DATA(PFNWINSOCKSELECT)                g_pfnselect;
extern DECL_HIDDEN_DATA(PFNWINSOCKGETHOSTBYNAME)         g_pfngethostbyname;
#endif


#endif /* !IPRT_INCLUDED_SRC_r3_win_internal_r3_win_h */

