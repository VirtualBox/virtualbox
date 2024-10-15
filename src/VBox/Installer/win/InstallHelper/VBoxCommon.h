/* $Id: VBoxCommon.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

UINT VBoxGetMsiProp(MSIHANDLE hMsi, const WCHAR *pwszName, WCHAR *pwszValueBuf, DWORD cwcValueBuf);
int  VBoxGetMsiPropUtf8(MSIHANDLE hMsi, const char *pcszName, char **ppszValue);
UINT VBoxSetMsiProp(MSIHANDLE hMsi, const WCHAR *pwszName, const WCHAR *pwszValue);
UINT VBoxSetMsiPropDWORD(MSIHANDLE hMsi, const WCHAR *pwszName, DWORD dwVal);

#endif /* !VBOX_INCLUDED_SRC_InstallHelper_VBoxCommon_h */

