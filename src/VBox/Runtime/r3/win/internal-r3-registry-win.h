/* $Id: internal-r3-registry-win.h 105788 2024-08-21 17:36:21Z andreas.loeffler@oracle.com $ */
/** @file
 * IPRT - Registy functions for Windows.
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

#ifndef IPRT_INCLUDED_SRC_r3_win_internal_r3_registry_win_h
#define IPRT_INCLUDED_SRC_r3_win_internal_r3_registry_win_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "internal/iprt.h"
#include <iprt/types.h>


/**
 * Queries a DWORD value from a Windows registry key.
 *
 * @returns IPRT status code.
 * @retval  VERR_FILE_NOT_FOUND if the value has not been found.
 * @param   hKey                    Registry handle to use.
 * @param   pszKey                  Registry key to query \a pszName in.
 * @param   pszName                 Name of the value to query.
 * @param   pdwValue                Where to return the actual value on success.
 */
RTDECL(int) RTSystemWinRegistryQueryDWORD(HKEY hKey, const char *pszKey, const char *pszName, DWORD *pdwValue);


#endif /* !IPRT_INCLUDED_SRC_r3_win_internal_r3_registry_win_h */
