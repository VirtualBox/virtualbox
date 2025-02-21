/* $Id: VBoxCommon.h 108493 2025-02-21 08:15:34Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxCommon - Misc helper routines for install helper.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_InstallHelper_VBoxCommon_h
#define VBOX_INCLUDED_SRC_InstallHelper_VBoxCommon_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#if (_MSC_VER < 1400) /* Provide _stprintf_s to VC < 8.0. */
int swprintf_s(WCHAR *buffer, size_t cbBuffer, const WCHAR *format, ...);
#endif

/** Struct for keeping a single "CustomActionData" key=value item. */
typedef struct VBOXMSICUSTOMACTIONDATAENTRY
{
    /** Key (name) of the item. */
    char *pszKey;
    /** Value of the item. Always a string but can represent other stuff. Use with care. */
    char *pszVal;
} VBOXMSICUSTOMACTIONDATAENTRY;
/** Pointer to a struct for keeping a single "CustomActionData" key=value item. */
typedef VBOXMSICUSTOMACTIONDATAENTRY *PVBOXMSICUSTOMACTIONDATAENTRY;

/** Struct for keeping "CustomActionData" entries. */
typedef struct VBOXMSICUSTOMACTIONDATA
{
    /** Array of CustomActionData entries. */
    PVBOXMSICUSTOMACTIONDATAENTRY paEntries;
    /** Number of entries in \a paEntries. */
    size_t                        cEntries;
} VBOXMSICUSTOMACTIONDATA;
/** Pointer to a struct for keeping "CustomActionData" entries. */
typedef VBOXMSICUSTOMACTIONDATA *PVBOXMSICUSTOMACTIONDATA;

/** Default separator for custom action data key=value pairs. */
#define VBOX_MSI_CUSTOMACTIONDATA_SEP_STR "##"

void VBoxMsiCustomActionDataFree(PVBOXMSICUSTOMACTIONDATA pData);
int  VBoxMsiCustomActionDataQueryEx(MSIHANDLE hMsi, const char *pszSep, PVBOXMSICUSTOMACTIONDATA *ppData);
int  VBoxMsiCustomActionDataQuery(MSIHANDLE hMsi, PVBOXMSICUSTOMACTIONDATA *ppData);
const char *VBoxMsiCustomActionDataFind(PVBOXMSICUSTOMACTIONDATA pHaystack, const char *pszNeedle);

int  VBoxMsiQueryProp(MSIHANDLE hMsi, const WCHAR *pwszName, WCHAR *pwszVal, DWORD cwVal);
int  VBoxMsiQueryPropEx(MSIHANDLE hMsi, const WCHAR *pwszName, WCHAR *pwszVal, DWORD *pcwVal);
int  VBoxMsiQueryPropUtf8(MSIHANDLE hMsi, const char *pszName, char **ppszValue);
int  VBoxMsiQueryPropInt32(MSIHANDLE hMsi, const char *pszName, DWORD *pdwValue);
UINT VBoxMsiSetProp(MSIHANDLE hMsi, const WCHAR *pwszName, const WCHAR *pwszValue);
int  VBoxMsiSetPropUtf8(MSIHANDLE hMsi, const char *pszName, const char *pszValue);
UINT VBoxMsiSetPropDWORD(MSIHANDLE hMsi, const WCHAR *pwszName, DWORD dwVal);
int  VBoxMsiRegQueryDWORDW(MSIHANDLE hMsi, HKEY hKey, LPCWSTR pwszName, DWORD *pdwValue);
int  VBoxMsiRegQueryDWORD(MSIHANDLE hMsi, HKEY hKey, const char *pszName, DWORD *pdwValue);

#endif /* !VBOX_INCLUDED_SRC_InstallHelper_VBoxCommon_h */

