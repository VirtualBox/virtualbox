/* $Id: VBoxUsbTool.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Windows USB R0 Tooling.
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

#ifndef VBOX_INCLUDED_SRC_VBoxUSB_win_cmn_VBoxUsbTool_h
#define VBOX_INCLUDED_SRC_VBoxUSB_win_cmn_VBoxUsbTool_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxDrvTool.h"

RT_C_DECLS_BEGIN
#pragma warning( disable : 4200 )
#include <usbdi.h>
#pragma warning( default : 4200 )
#include <usbdlib.h>

RT_C_DECLS_END

#if 0
/* enable this in case we include this in a dll*/
# ifdef IN_VBOXUSBTOOL
#  define VBOXUSBTOOL_DECL(a_Type) DECLEXPORT(a_Type)
# else
#  define VBOXUSBTOOL_DECL(a_Type) DECLIMPORT(a_Type)
# endif
#else
/*enable this in case we include this in a static lib*/
# define VBOXUSBTOOL_DECL(a_Type) a_Type VBOXCALL
#endif


RT_C_DECLS_BEGIN

VBOXUSBTOOL_DECL(PURB) VBoxUsbToolUrbAlloc(USHORT u16Function, USHORT cbSize);
VBOXUSBTOOL_DECL(PURB) VBoxUsbToolUrbAllocZ(USHORT u16Function, USHORT cbSize);
VBOXUSBTOOL_DECL(PURB) VBoxUsbToolUrbReinit(PURB pUrb, USHORT cbSize, USHORT u16Function);
VBOXUSBTOOL_DECL(VOID) VBoxUsbToolUrbFree(PURB pUrb);
VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolUrbPost(PDEVICE_OBJECT pDevObj, PURB pUrb, ULONG dwTimeoutMs);
VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolGetDescriptor(PDEVICE_OBJECT pDevObj, void *pvBuffer, int cbBuffer, int Type, int iIndex, int LangId, ULONG dwTimeoutMs);
VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolGetStringDescriptor(PDEVICE_OBJECT pDevObj, char *pszResult, ULONG cbResult, int iIndex, int LangId, ULONG dwTimeoutMs);
VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolGetLangID(PDEVICE_OBJECT pDevObj, int *pLangId, ULONG dwTimeoutMs);
VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolGetDeviceSpeed(PDEVICE_OBJECT pDevObj, BOOLEAN *pbIsHigh);
VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolPipeClear(PDEVICE_OBJECT pDevObj, HANDLE hPipe, bool fReset);
VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolCurrentFrame(PDEVICE_OBJECT pDevObj, PIRP pIrp, PULONG piFrame);
VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolDevUnconfigure(PDEVICE_OBJECT pDevObj);
VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolIoInternalCtlSendAsync(PDEVICE_OBJECT pDevObj, ULONG uCtl, void *pvArg1, void *pvArg2, PKEVENT pEvent, PIO_STATUS_BLOCK pIoStatus);
VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolIoInternalCtlSendSync(PDEVICE_OBJECT pDevObj, ULONG uCtl, void *pvArg1, void *pvArg2);
VBOXUSBTOOL_DECL(PIRP) VBoxUsbToolIoBuildAsyncInternalCtl(PDEVICE_OBJECT pDevObj, ULONG uCtl, void *pvArg1, void *pvArg2);
VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolIoInternalCtlSendSyncWithTimeout(PDEVICE_OBJECT pDevObj, ULONG uCtl, void *pvArg1, void *pvArg2, ULONG dwTimeoutMs);
VBOXUSBTOOL_DECL(VOID) VBoxUsbToolStringDescriptorToUnicodeString(PUSB_STRING_DESCRIPTOR pDr, PUNICODE_STRING pUnicode);


RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_VBoxUSB_win_cmn_VBoxUsbTool_h */
