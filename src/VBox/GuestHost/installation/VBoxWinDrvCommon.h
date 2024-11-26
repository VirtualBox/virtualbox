/* $Id: VBoxWinDrvCommon.h 107166 2024-11-26 09:03:23Z andreas.loeffler@oracle.com $ */
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

#if defined(RT_ARCH_AMD64)
# define VBOXWINDRVINF_NATIVE_ARCH_STR     "AMD64"
#elif defined(RT_ARCH_X86)
# define VBOXWINDRVINF_NATIVE_ARCH_STR     "X86"
#elif defined(RT_ARCH_ARM64)
# define VBOXWINDRVINF_NATIVE_ARCH_STR     "ARM64"
#else
# error "Port me!"
#endif

#define VBOXWINDRVINF_DOT_NT_NATIVE_ARCH_STR ".NT" VBOXWINDRVINF_NATIVE_ARCH_STR

int VBoxWinDrvInfOpenEx(PCRTUTF16 pwszInfFile, PRTUTF16 pwszClassName, HINF *phInf);
int VBoxWinDrvInfOpen(PCRTUTF16 pwszInfFile, HINF *phInf);
int VBoxWinDrvInfOpenUtf8(const char *pszInfFile, HINF *phInf);
int VBoxWinDrvInfClose(HINF hInf);
int VBoxWinDrvInfQueryFirstModel(HINF hInf, PRTUTF16 *ppwszModel);
int VBoxWinDrvInfQueryFirstPnPId(HINF hInf, PRTUTF16 pwszModel, PRTUTF16 *ppwszPnPId);
int VBoxWinDrvInfQueryKeyValue(PINFCONTEXT pCtx, DWORD iValue, PRTUTF16 *ppwszValue, PDWORD pcwcValue);
int VBoxWinDrvInfQueryModelsSectionName(HINF hInf, PRTUTF16 *ppwszValue, PDWORD pcwcValue);

#endif /* !VBOX_INCLUDED_SRC_installation_VBoxWinDrvCommon_h */

