/* $Id: VBoxGINA.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGINA - Windows Logon DLL for VirtualBox.
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

#ifndef GA_INCLUDED_SRC_WINNT_VBoxGINA_VBoxGINA_h
#define GA_INCLUDED_SRC_WINNT_VBoxGINA_VBoxGINA_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** Handle to Winlogon service */
extern HANDLE hGinaWlx;
/** Winlog function dispatch table */
extern PWLX_DISPATCH_VERSION_1_1 pWlxFuncs;


/** @name GINA entry point calls
 * @{
 */
typedef BOOL (WINAPI *PGWLXNEGOTIATE)(DWORD, DWORD*);
typedef BOOL (WINAPI *PGWLXINITIALIZE)(LPWSTR, HANDLE, PVOID, PVOID, PVOID*);
typedef VOID (WINAPI *PGWLXDISPLAYSASNOTICE)(PVOID);
typedef int  (WINAPI *PGWLXLOGGEDOUTSAS)(PVOID, DWORD, PLUID, PSID, PDWORD,
                                        PHANDLE, PWLX_MPR_NOTIFY_INFO, PVOID*);
typedef BOOL (WINAPI *PGWLXACTIVATEUSERSHELL)(PVOID, PWSTR, PWSTR, PVOID);
typedef int  (WINAPI *PGWLXLOGGEDONSAS)(PVOID, DWORD, PVOID);
typedef VOID (WINAPI *PGWLXDISPLAYLOCKEDNOTICE)(PVOID);
typedef int  (WINAPI *PGWLXWKSTALOCKEDSAS)(PVOID, DWORD);
typedef BOOL (WINAPI *PGWLXISLOCKOK)(PVOID);
typedef BOOL (WINAPI *PGWLXISLOGOFFOK)(PVOID);
typedef VOID (WINAPI *PGWLXLOGOFF)(PVOID);
typedef VOID (WINAPI *PGWLXSHUTDOWN)(PVOID, DWORD);
/* 1.1 calls */
typedef BOOL (WINAPI *PGWLXSCREENSAVERNOTIFY)(PVOID, BOOL*);
typedef BOOL (WINAPI *PGWLXSTARTAPPLICATION)(PVOID, PWSTR, PVOID, PWSTR);
/* 1.3 calls */
typedef BOOL (WINAPI *PGWLXNETWORKPROVIDERLOAD)(PVOID, PWLX_MPR_NOTIFY_INFO);
typedef BOOL (WINAPI *PGWLXDISPLAYSTATUSMESSAGE)(PVOID, HDESK, DWORD, PWSTR, PWSTR);
typedef BOOL (WINAPI *PGWLXGETSTATUSMESSAGE)(PVOID, DWORD*, PWSTR, DWORD);
typedef BOOL (WINAPI *PGWLXREMOVESTATUSMESSAGE)(PVOID);
/* 1.4 calls */
typedef BOOL (WINAPI *PGWLXGETCONSOLESWITCHCREDENTIALS)(PVOID, PVOID);
typedef VOID (WINAPI *PGWLXRECONNECTNOTIFY)(PVOID);
typedef VOID (WINAPI *PGWLXDISCONNECTNOTIFY)(PVOID);
/** @}  */

#endif /* !GA_INCLUDED_SRC_WINNT_VBoxGINA_VBoxGINA_h */

