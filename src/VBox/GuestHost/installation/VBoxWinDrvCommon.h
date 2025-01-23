/* $Id: VBoxWinDrvCommon.h 108020 2025-01-23 08:29:22Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxWinDrvCommon - Common Windows driver functions.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_installation_VBoxWinDrvCommon_h
#define VBOX_INCLUDED_SRC_installation_VBoxWinDrvCommon_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/win/windows.h>
#include <iprt/win/setupapi.h>

#include <iprt/utf16.h>

#include <VBox/GuestHost/VBoxWinDrvDefs.h>

/**
 * Enumeration specifying the INF (driver) type.
 */
typedef enum VBOXWINDRVINFTYPE
{
    /** Invalid type. */
    VBOXWINDRVINFTYPE_INVALID = 0,
    /** Primitive driver.
     *  This uses a "DefaultInstall" (plus optionally "DefaultUninstall") sections
     *  and does not have a PnP ID. */
    VBOXWINDRVINFTYPE_PRIMITIVE,
    /** Normal driver.
     *  Uses a "Manufacturer" section and can have a PnP ID. */
    VBOXWINDRVINFTYPE_NORMAL
} VBOXWINDRVINFTYPE;

int VBoxWinDrvInfOpenEx(PCRTUTF16 pwszInfFile, PRTUTF16 pwszClassName, HINF *phInf);
int VBoxWinDrvInfOpen(PCRTUTF16 pwszInfFile, HINF *phInf);
int VBoxWinDrvInfOpenUtf8(const char *pszInfFile, HINF *phInf);
int VBoxWinDrvInfClose(HINF hInf);
VBOXWINDRVINFTYPE VBoxWinDrvInfGetTypeEx(HINF hInf, PRTUTF16 *ppwszSection);
VBOXWINDRVINFTYPE VBoxWinDrvInfGetType(HINF hInf);
int VBoxWinDrvInfQueryFirstModel(HINF hInf, PCRTUTF16 pwszSection, PRTUTF16 *ppwszModel);
int VBoxWinDrvInfQueryFirstPnPId(HINF hInf, PRTUTF16 pwszModel, PRTUTF16 *ppwszPnPId);
int VBoxWinDrvInfQueryKeyValue(PINFCONTEXT pCtx, DWORD iValue, PRTUTF16 *ppwszValue, PDWORD pcwcValue);
int VBoxWinDrvInfQueryModelEx(HINF hInf, PCRTUTF16 pwszSection, unsigned uIndex, PRTUTF16 *ppwszValue, PDWORD pcwcValue);
int VBoxWinDrvInfQueryModel(HINF hInf, PCRTUTF16 pwszSection, unsigned uIndex, PRTUTF16 *ppwszValue, PDWORD pcwcValue);
int VBoxWinDrvInfQueryInstallSectionEx(HINF hInf, PCRTUTF16 pwszModel, PRTUTF16 *ppwszValue, PDWORD pcwcValue);
int VBoxWinDrvInfQueryInstallSection(HINF hInf, PCRTUTF16 pwszModel, PRTUTF16 *ppwszValue);
int VBoxWinDrvInfQuerySectionVerEx(HINF hInf, UINT uIndex, PVBOXWINDRVINFSECVERSION pVer);
int VBoxWinDrvInfQuerySectionVer(HINF hInf, PVBOXWINDRVINFSECVERSION pVer);

const char *VBoxWinDrvSetupApiErrToStr(const DWORD dwErr);
const char *VBoxWinDrvWinErrToStr(const DWORD dwErr);
int VBoxWinDrvInstErrorFromWin32(unsigned uNativeCode);

#endif /* !VBOX_INCLUDED_SRC_installation_VBoxWinDrvCommon_h */

