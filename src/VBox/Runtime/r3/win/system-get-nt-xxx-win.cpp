/* $Id: system-get-nt-xxx-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTSystemQueryOSInfo, generic stub.
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
#include "internal/iprt.h"
#include <iprt/win/windows.h>

#include "internal-r3-win.h"
#include "internal-r3-registry-win.h"
#include <iprt/system.h>
#include <iprt/assert.h>
#include <iprt/err.h>


RTDECL(uint32_t) RTSystemGetNtBuildNo(void)
{
    Assert(g_WinOsInfoEx.dwOSVersionInfoSize > 0);
    return g_WinOsInfoEx.dwBuildNumber;
}


RTDECL(uint64_t) RTSystemGetNtVersion(void)
{
    Assert(g_WinOsInfoEx.dwOSVersionInfoSize > 0);
    return RTSYSTEM_MAKE_NT_VERSION(g_WinOsInfoEx.dwMajorVersion, g_WinOsInfoEx.dwMinorVersion, g_WinOsInfoEx.dwBuildNumber);
}


RTDECL(uint8_t) RTSystemGetNtProductType(void)
{
    Assert(g_WinOsInfoEx.dwOSVersionInfoSize > 0);
    return g_WinOsInfoEx.wProductType; /* It's a byte, not a word as 'w' normally indicates. (Baka Maikurosofuto!) */
}


RTDECL(int) RTSystemQueryNtFeatureEnabled(RTSYSNTFEATURE enmFeature, bool *pfEnabled)
{
    AssertPtrReturn(pfEnabled, VERR_INVALID_POINTER);

    int rc;

    switch (enmFeature)
    {
        case RTSYSNTFEATURE_CORE_ISOLATION_MEMORY_INTEGRITY: /* aka Code Integrity */
        {
            DWORD dwEnabled;
            rc = RTSystemWinRegistryQueryDWORD(HKEY_LOCAL_MACHINE,
                                    "SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity",
                                    "Enabled", &dwEnabled);
            if (RT_SUCCESS(rc))
                *pfEnabled = RT_BOOL(dwEnabled);
            else if (rc == VERR_FILE_NOT_FOUND)
                rc = VERR_NOT_SUPPORTED;
            break;
        }

        default:
            rc = VERR_NOT_IMPLEMENTED;
            break;
    }

    return rc;
}

