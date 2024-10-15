/* $Id: VBoxCommon.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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


#ifndef TESTCASE
/**
 * Retrieves a MSI property (in UTF-16).
 *
 * Convenience function for VBoxGetMsiProp().
 *
 * @returns VBox status code.
 * @param   hMsi                MSI handle to use.
 * @param   pwszName            Name of property to retrieve.
 * @param   pwszValueBuf        Where to store the allocated value on success.
 * @param   cwcValueBuf         Size (in WCHARs) of \a pwszValueBuf.
 */
UINT VBoxGetMsiProp(MSIHANDLE hMsi, const WCHAR *pwszName, WCHAR *pwszValueBuf, DWORD cwcValueBuf)
{
    RT_BZERO(pwszValueBuf, cwcValueBuf * sizeof(WCHAR));
    return MsiGetPropertyW(hMsi, pwszName, pwszValueBuf, &cwcValueBuf);
}
#endif

/**
 * Retrieves a MSI property (in UTF-8).
 *
 * Convenience function for VBoxGetMsiProp().
 *
 * @returns VBox status code.
 * @param   hMsi                MSI handle to use.
 * @param   pcszName            Name of property to retrieve.
 * @param   ppszValue           Where to store the allocated value on success.
 *                              Must be free'd using RTStrFree() by the caller.
 */
int VBoxGetMsiPropUtf8(MSIHANDLE hMsi, const char *pcszName, char **ppszValue)
{
    PRTUTF16 pwszName;
    int rc = RTStrToUtf16(pcszName, &pwszName);
    if (RT_SUCCESS(rc))
    {
        WCHAR wszValue[1024]; /* 1024 should be enough for everybody (tm). */
        if (VBoxGetMsiProp(hMsi, pwszName, wszValue, RT_ELEMENTS(wszValue)) == ERROR_SUCCESS)
            rc = RTUtf16ToUtf8(wszValue, ppszValue);
        else
            rc = VERR_NOT_FOUND;

        RTUtf16Free(pwszName);
    }

    return rc;
}

#ifndef TESTCASE
UINT VBoxSetMsiProp(MSIHANDLE hMsi, const WCHAR *pwszName, const WCHAR *pwszValue)
{
    return MsiSetPropertyW(hMsi, pwszName, pwszValue);
}
#endif

UINT VBoxSetMsiPropDWORD(MSIHANDLE hMsi, const WCHAR *pwszName, DWORD dwVal)
{
    wchar_t wszTemp[32];
    RTUtf16Printf(wszTemp, RT_ELEMENTS(wszTemp), "%u", dwVal);
    return VBoxSetMsiProp(hMsi, pwszName, wszTemp);
}

