/* $Id: RTSystemRegistry-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Windows registry access functions.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/system.h>

#include <iprt/nt/nt-and-windows.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/utf16.h>

#include "internal-r3-win.h"


/**
 * Queries a DWORD value from a Windows registry key, Unicode (wide char) version.
 *
 * @returns IPRT status code.
 * @retval  VERR_FILE_NOT_FOUND if the value has not been found.
 * @retval  VERR_WRONG_TYPE if the type (DWORD) of the value does not match.
 * @retval  VERR_MISMATCH if the type sizes do not match.
 * @param   hKey                    Registry handle to use.
 * @param   pwszKey                 Registry key to query \a pwszName in.
 * @param   pwszName                Name of the value to query.
 * @param   pdwValue                Where to return the actual value on success.
 */
static int rtSystemWinRegistryQueryDWORDW(HKEY hKey, LPCWSTR pwszKey, LPCWSTR pwszName, DWORD *pdwValue)
{
    LONG lErr = RegOpenKeyExW(hKey, pwszKey, 0, KEY_QUERY_VALUE, &hKey);
    if (lErr != ERROR_SUCCESS)
        return RTErrConvertFromWin32(lErr);

    int rc = VINF_SUCCESS;

    DWORD cbType = sizeof(DWORD);
    DWORD dwType = 0;
    DWORD dwValue;
    lErr = RegQueryValueExW(hKey, pwszName, NULL, &dwType, (BYTE *)&dwValue, &cbType);
    if (lErr == ERROR_SUCCESS)
    {
        if (cbType == sizeof(DWORD))
        {
            if (dwType == REG_DWORD)
            {
                *pdwValue = dwValue;
            }
            else
                rc = VERR_WRONG_TYPE;
        }
        else
            rc = VERR_MISMATCH;
    }
    else
        rc = RTErrConvertFromWin32(lErr);

    RegCloseKey(hKey);

    return rc;
}


RTDECL(int) RTSystemWinRegistryQueryDWORD(HKEY hKey, const char *pszKey, const char *pszName, DWORD *pdwValue)
{
    PRTUTF16 pwszKey;
    int rc = RTStrToUtf16Ex(pszKey, RTSTR_MAX, &pwszKey, 0, NULL);
    if (RT_SUCCESS(rc))
    {
        PRTUTF16 pwszName;
        rc = RTStrToUtf16Ex(pszName, RTSTR_MAX, &pwszName, 0, NULL);
        if (RT_SUCCESS(rc))
        {
            rc = rtSystemWinRegistryQueryDWORDW(hKey, pwszKey, pwszName, pdwValue);
            RTUtf16Free(pwszName);
        }
        RTUtf16Free(pwszKey);
    }

    return rc;
}

