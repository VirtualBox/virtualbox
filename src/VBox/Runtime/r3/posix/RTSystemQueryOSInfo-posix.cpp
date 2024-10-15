/* $Id: RTSystemQueryOSInfo-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTSystemQueryOSInfo, POSIX implementation.
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
#include <iprt/system.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/errcore.h>

#include <errno.h>
#include <sys/utsname.h>


RTDECL(int) RTSystemQueryOSInfo(RTSYSOSINFO enmInfo, char *pszInfo, size_t cchInfo)
{
    /*
     * Quick validation.
     */
    AssertReturn(enmInfo > RTSYSOSINFO_INVALID && enmInfo < RTSYSOSINFO_END, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszInfo, VERR_INVALID_POINTER);
    if (!cchInfo)
        return VERR_BUFFER_OVERFLOW;

    /*
     * Handle the request.
     */
    switch (enmInfo)
    {
        case RTSYSOSINFO_PRODUCT:
        case RTSYSOSINFO_RELEASE:
        case RTSYSOSINFO_VERSION:
        {
            struct utsname UtsInfo;
            if (uname(&UtsInfo) < 0)
                return RTErrConvertFromErrno(errno);
            const char *pszSrc;
            switch (enmInfo)
            {
                case RTSYSOSINFO_PRODUCT:   pszSrc = UtsInfo.sysname; break;
                case RTSYSOSINFO_RELEASE:   pszSrc = UtsInfo.release; break;
                case RTSYSOSINFO_VERSION:   pszSrc = UtsInfo.version; break;
                default: AssertFatalFailed(); /* screw gcc */
            }
            size_t cch = strlen(pszSrc);
            if (cch < cchInfo)
            {
                memcpy(pszInfo, pszSrc, cch + 1);
                return VINF_SUCCESS;
            }
            memcpy(pszInfo, pszSrc, cchInfo - 1);
            pszInfo[cchInfo - 1] = '\0';
            return VERR_BUFFER_OVERFLOW;
        }


        case RTSYSOSINFO_SERVICE_PACK:
        default:
            *pszInfo = '\0';
            return VERR_NOT_SUPPORTED;
    }

    return VINF_SUCCESS;
}

