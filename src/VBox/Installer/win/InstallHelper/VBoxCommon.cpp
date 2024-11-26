/* $Id: VBoxCommon.cpp 107166 2024-11-26 09:03:23Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxCommon - Misc helper routines for install helper.
 *
 * This is used by internal/serial.cpp and VBoxInstallHelper.cpp.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/win/windows.h>
#include <msi.h>
#include <msiquery.h>

#include <iprt/string.h>
#include <iprt/utf16.h>

#include "VBoxCommon.h"


/**
 * Retrieves a MSI property (in UTF-16), extended version.
 *
 * @returns VBox status code.
 * @param   hMsi                MSI handle to use.
 * @param   pwszName            Name of property to retrieve.
 * @param   pwszVal             Where to store the allocated value on success.
 * @param   pcwVal              Input and output size (in WCHARs) of \a pwszVal.
 */
int VBoxMsiQueryPropEx(MSIHANDLE hMsi, const WCHAR *pwszName, WCHAR *pwszVal, DWORD *pcwVal)
{
    AssertPtrReturn(pwszName, VERR_INVALID_POINTER);
    AssertPtrReturn(pwszVal, VERR_INVALID_POINTER);
    AssertPtrReturn(pcwVal, VERR_INVALID_POINTER);
    AssertReturn(*pcwVal, VERR_INVALID_PARAMETER);

    int rc;

    RT_BZERO(pwszVal, *pcwVal * sizeof(WCHAR));
    UINT uRc = MsiGetPropertyW(hMsi, pwszName, pwszVal, pcwVal);
    if (uRc == ERROR_SUCCESS)
    {
        if (*pcwVal > 0)
        {
            rc = VINF_SUCCESS;
        }
        else /* Indicates value not found. */
            rc = VERR_NOT_FOUND;
    }
    else
        rc = RTErrConvertFromWin32(uRc);

    return rc;
}

#ifndef TESTCASE
/**
 * Retrieves a MSI property (in UTF-16).
 *
 * @returns VBox status code.
 * @param   hMsi                MSI handle to use.
 * @param   pwszName            Name of property to retrieve.
 * @param   pwszVal             Where to store the allocated value on success.
 * @param   cwVal               Input size (in WCHARs) of \a pwszVal.
 */
int VBoxMsiQueryProp(MSIHANDLE hMsi, const WCHAR *pwszName, WCHAR *pwszVal, DWORD cwVal)
{
    return VBoxMsiQueryPropEx(hMsi, pwszName, pwszVal, &cwVal);
}
#endif /* !TESTCASE */

/**
 * Retrieves a MSI property (in UTF-8).
 *
 * Convenience function for VBoxGetMsiProp().
 *
 * @returns VBox status code.
 * @param   hMsi                MSI handle to use.
 * @param   pszName             Name of property to retrieve.
 * @param   ppszValue           Where to store the allocated value on success.
 *                              Must be free'd using RTStrFree() by the caller.
 */
int VBoxMsiQueryPropUtf8(MSIHANDLE hMsi, const char *pszName, char **ppszValue)
{
   AssertPtrReturn(pszName, VERR_INVALID_POINTER);
   AssertPtrReturn(ppszValue, VERR_INVALID_POINTER);

    PRTUTF16 pwszName;
    int rc = RTStrToUtf16(pszName, &pwszName);
    if (RT_SUCCESS(rc))
    {
        WCHAR wszValue[1024]; /* 1024 should be enough for everybody (tm). */
        rc = VBoxMsiQueryProp(hMsi, pwszName, wszValue, RT_ELEMENTS(wszValue));
        if (RT_SUCCESS(rc))
            rc = RTUtf16ToUtf8(wszValue, ppszValue);

        RTUtf16Free(pwszName);
    }

    return rc;
}

#ifndef TESTCASE
int VBoxMsiQueryPropInt32(MSIHANDLE hMsi, const char *pszName, DWORD *pdwValue)
{
   AssertPtrReturn(pszName, VERR_INVALID_POINTER);
   AssertPtrReturn(pdwValue, VERR_INVALID_POINTER);

    PRTUTF16 pwszName;
    int rc = RTStrToUtf16(pszName, &pwszName);
    if (RT_SUCCESS(rc))
    {
        char *pszTemp;
        rc = VBoxMsiQueryPropUtf8(hMsi, pszName, &pszTemp);
        if (RT_SUCCESS(rc))
        {
            *pdwValue = RTStrToInt32(pszTemp);
            RTStrFree(pszTemp);
        }
    }

    return rc;
}

UINT VBoxMsiSetProp(MSIHANDLE hMsi, const WCHAR *pwszName, const WCHAR *pwszValue)
{
    return MsiSetPropertyW(hMsi, pwszName, pwszValue);
}
#endif

UINT VBoxMsiSetPropDWORD(MSIHANDLE hMsi, const WCHAR *pwszName, DWORD dwVal)
{
    wchar_t wszTemp[32];
    RTUtf16Printf(wszTemp, RT_ELEMENTS(wszTemp), "%u", dwVal);
    return VBoxMsiSetProp(hMsi, pwszName, wszTemp);
}

