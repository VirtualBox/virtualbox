/* $Id: RTTimeZoneGetCurrent-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTTimeZoneGetCurrent, generic.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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
#include <iprt/time.h>
#include "internal/iprt.h"

#include <iprt/errcore.h>
#include <iprt/env.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include <iprt/win/windows.h>
#include "internal-r3-win.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef DWORD (WINAPI *PFNGETDYNAMICTIMEZONEINFORMATION)(PDYNAMIC_TIME_ZONE_INFORMATION);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Pointer to the GetDynamicTimeZoneInformation API if present. */
static PFNGETDYNAMICTIMEZONEINFORMATION g_pfnGetDynamicTimeZoneInformation = NULL;
/** Flipped after we've tried to resolve g_pfnGetDynamicTimeZoneInformation.  */
static bool volatile                    g_fResolvedApi = false;


RTDECL(int) RTTimeZoneGetCurrent(char *pszName, size_t cbName)
{
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertReturn(cbName > 0, VERR_BUFFER_OVERFLOW);

    /*
     * Resolve API.
     */
    PFNGETDYNAMICTIMEZONEINFORMATION pfnApi;
    if (g_fResolvedApi)
        pfnApi = g_pfnGetDynamicTimeZoneInformation;
    else
    {
        pfnApi = (PFNGETDYNAMICTIMEZONEINFORMATION)GetProcAddress(g_hModKernel32, "GetDynamicTimeZoneInformation");
        g_pfnGetDynamicTimeZoneInformation = pfnApi;
        g_fResolvedApi = true;
    }

    /*
     * Call the API and convert the name we get.
     */
    union
    {
        TIME_ZONE_INFORMATION           Tzi;
        DYNAMIC_TIME_ZONE_INFORMATION   DynTzi;
    } uBuf;
    RT_ZERO(uBuf);
    DWORD     dwRc;
    PCRTUTF16 pwszSrcName;
    size_t    cwcSrcName;
    if (pfnApi)
    {
        dwRc = pfnApi(&uBuf.DynTzi);
        pwszSrcName = uBuf.DynTzi.TimeZoneKeyName;
        cwcSrcName  = RT_ELEMENTS(uBuf.DynTzi.TimeZoneKeyName);
    }
    else
    {
        /* Not sure how helpful this fallback really is... */
        dwRc = GetTimeZoneInformation(&uBuf.Tzi);
        pwszSrcName = uBuf.Tzi.StandardName;
        cwcSrcName  = RT_ELEMENTS(uBuf.Tzi.StandardName);
    }
    if (dwRc != TIME_ZONE_ID_INVALID)
    {
        Assert(*pwszSrcName != '\0');
        return RTUtf16ToUtf8Ex(pwszSrcName, cwcSrcName, &pszName, cbName, &cbName);
    }
    return RTErrConvertFromWin32(GetLastError());
}

