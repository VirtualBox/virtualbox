/* $Id: VBoxDrvCfg-win.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Windows Driver Manipulation API.
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

#ifndef VBOX_INCLUDED_VBoxDrvCfg_win_h
#define VBOX_INCLUDED_VBoxDrvCfg_win_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/win/windows.h>
#include <VBox/cdefs.h>

RT_C_DECLS_BEGIN

#if 0
/* enable this in case we include this in a dll*/
# ifdef IN_VBOXDRVCFG
#  define VBOXDRVCFG_DECL(a_Type) DECLEXPORT(a_Type)
# else
#  define VBOXDRVCFG_DECL(a_Type) DECLIMPORT(a_Type)
# endif
#else
/*enable this in case we include this in a static lib*/
# define VBOXDRVCFG_DECL(a_Type) a_Type VBOXCALL
#endif

typedef enum
{
    VBOXDRVCFG_LOG_SEVERITY_FLOW = 1,
    VBOXDRVCFG_LOG_SEVERITY_REGULAR,
    VBOXDRVCFG_LOG_SEVERITY_REL
} VBOXDRVCFG_LOG_SEVERITY_T;

typedef DECLCALLBACKTYPE(void, FNVBOXDRVCFGLOG,(VBOXDRVCFG_LOG_SEVERITY_T enmSeverity, char *pszMsg, void *pvContext));
typedef FNVBOXDRVCFGLOG *PFNVBOXDRVCFGLOG;

VBOXDRVCFG_DECL(void) VBoxDrvCfgLoggerSet(PFNVBOXDRVCFGLOG pfnLog, void *pvLog);

typedef DECLCALLBACKTYPE(void, FNVBOXDRVCFGPANIC,(void *pvPanic));
typedef FNVBOXDRVCFGPANIC *PFNVBOXDRVCFGPANIC;
VBOXDRVCFG_DECL(void) VBoxDrvCfgPanicSet(PFNVBOXDRVCFGPANIC pfnPanic, void *pvPanic);

/* Driver package API*/
VBOXDRVCFG_DECL(HRESULT) VBoxDrvCfgInfInstall(IN LPCWSTR pwszInfPath);
VBOXDRVCFG_DECL(HRESULT) VBoxDrvCfgInfUninstall(IN LPCWSTR pwszInfPath, IN DWORD fFlags);
VBOXDRVCFG_DECL(HRESULT) VBoxDrvCfgInfUninstallAllSetupDi(IN const GUID * pGuidClass, IN LPCWSTR pwszClassName,
                                                          IN LPCWSTR pwszPnPId, IN DWORD fFlags);
VBOXDRVCFG_DECL(HRESULT) VBoxDrvCfgInfUninstallAllF(IN LPCWSTR pwszClassName, IN LPCWSTR pwszPnPId, IN DWORD fFlags);

/* Service API */
VBOXDRVCFG_DECL(HRESULT) VBoxDrvCfgSvcStart(LPCWSTR pwszSvcName);

HRESULT VBoxDrvCfgDrvUpdate(LPCWSTR pszwHwId, LPCWSTR psxwInf, BOOL *pfRebootRequired);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_VBoxDrvCfg_win_h */

