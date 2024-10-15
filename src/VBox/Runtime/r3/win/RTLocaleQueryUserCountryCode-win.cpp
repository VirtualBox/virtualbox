/* $Id: RTLocaleQueryUserCountryCode-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTLocaleQueryUserCountryCode, ring-3, Windows.
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
#include <iprt/win/windows.h>

#include <iprt/locale.h>
#include "internal/iprt.h"

#include <iprt/ctype.h>
#include <iprt/errcore.h>
#include <iprt/string.h>

#include "internal-r3-win.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef GEOID (WINAPI *PFNGETUSERGEOID)(GEOCLASS);
typedef INT   (WINAPI *PFNGETGEOINFOW)(GEOID,GEOTYPE,LPWSTR,INT,LANGID);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Pointer to GetUserGeoID. */
static PFNGETUSERGEOID  g_pfnGetUserGeoID = NULL;
/** Pointer to GetGeoInfoW. */
static PFNGETGEOINFOW   g_pfnGetGeoInfoW = NULL;
/** Set if we've tried to resolve the APIs. */
static bool volatile    g_fResolvedApis = false;


RTDECL(int) RTLocaleQueryUserCountryCode(char pszCountryCode[3])
{
    /*
     * Get API pointers.
     */
    PFNGETUSERGEOID  pfnGetUserGeoID;
    PFNGETGEOINFOW   pfnGetGeoInfoW;
    if (g_fResolvedApis)
    {
        pfnGetUserGeoID = g_pfnGetUserGeoID;
        pfnGetGeoInfoW  = g_pfnGetGeoInfoW;
    }
    else
    {
        pfnGetUserGeoID = (PFNGETUSERGEOID)GetProcAddress(g_hModKernel32, "GetUserGeoID");
        pfnGetGeoInfoW  = (PFNGETGEOINFOW)GetProcAddress(g_hModKernel32, "GetGeoInfoW");
        g_pfnGetUserGeoID = pfnGetUserGeoID;
        g_pfnGetGeoInfoW  = pfnGetGeoInfoW;
        g_fResolvedApis   = true;
    }

    int rc;
    if (   pfnGetGeoInfoW
        && pfnGetUserGeoID)
    {
        /*
         * Call the API and retrieve the two letter ISO country code.
         */
        GEOID idGeo = pfnGetUserGeoID(GEOCLASS_NATION);
        if (idGeo != GEOID_NOT_AVAILABLE)
        {
            RTUTF16 wszName[16];
            RT_ZERO(wszName);
            DWORD cwcReturned = pfnGetGeoInfoW(idGeo, GEO_ISO2, wszName, RT_ELEMENTS(wszName), LOCALE_NEUTRAL);
            if (   cwcReturned >= 2
                && cwcReturned <= 3
                && wszName[2] == '\0'
                && wszName[1] != '\0'
                && RT_C_IS_ALPHA(wszName[1])
                && wszName[0] != '\0'
                && RT_C_IS_ALPHA(wszName[0]) )
            {
                pszCountryCode[0] = RT_C_TO_UPPER(wszName[0]);
                pszCountryCode[1] = RT_C_TO_UPPER(wszName[1]);
                pszCountryCode[2] = '\0';
                return VINF_SUCCESS;
            }
            AssertMsgFailed(("cwcReturned=%d err=%u wszName='%.16ls'\n", cwcReturned, GetLastError(), wszName));
        }
        rc = VERR_NOT_AVAILABLE;
    }
    else
        rc = VERR_NOT_SUPPORTED;
    pszCountryCode[0] = 'Z';
    pszCountryCode[1] = 'Z';
    pszCountryCode[2] = '\0';
    return rc;
}

