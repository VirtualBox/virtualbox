/* $Id: VBoxGuestR3LibAdditions.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions, Additions Info.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>
#ifdef RT_OS_WINDOWS
# include <iprt/utf16.h>
#endif
#include <VBox/log.h>
#include <VBox/version.h>
#include "VBoxGuestR3LibInternal.h"



#ifdef RT_OS_WINDOWS

/**
 * Opens the "VirtualBox Guest Additions" registry key.
 *
 * @returns IPRT status code
 * @param   phKey       Receives key handle on success. The returned handle must
 *                      be closed by calling vbglR3WinCloseRegKey.
 */
static int vbglR3WinOpenAdditionRegisterKey(PHKEY phKey)
{
    /*
     * Current vendor first.  We keep the older ones just for the case that
     * the caller isn't actually installed yet (no real use case AFAIK).
     */
    static PCRTUTF16 s_apwszKeys[] =
    {
        L"SOFTWARE\\" RT_LSTR(VBOX_VENDOR_SHORT) L"\\VirtualBox Guest Additions",
#ifdef RT_ARCH_AMD64
        L"SOFTWARE\\Wow6432Node\\" RT_LSTR(VBOX_VENDOR_SHORT) L"\\VirtualBox Guest Additions",
#endif
        L"SOFTWARE\\Sun\\VirtualBox Guest Additions",
#ifdef RT_ARCH_AMD64
        L"SOFTWARE\\Wow6432Node\\Sun\\VirtualBox Guest Additions",
#endif
        L"SOFTWARE\\Sun\\xVM VirtualBox Guest Additions",
#ifdef RT_ARCH_AMD64
        L"SOFTWARE\\Wow6432Node\\Sun\\xVM VirtualBox Guest Additions",
#endif
    };
    int rc = VERR_NOT_FOUND;
    for (uint32_t i = 0; i < RT_ELEMENTS(s_apwszKeys); i++)
    {
        LSTATUS lrc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, s_apwszKeys[i], 0 /* ulOptions*/, KEY_READ, phKey);
        if (lrc == ERROR_SUCCESS)
            return VINF_SUCCESS;
        if (i == 0)
            rc = RTErrConvertFromWin32(lrc);
    }
    return rc;
}


/**
 * Closes the registry handle returned by vbglR3WinOpenAdditionRegisterKey().
 *
 * @returns @a rc or IPRT failure status.
 * @param   hKey        Handle to close.
 * @param   rc          The current IPRT status of the operation.  Error
 *                      condition takes precedence over errors from this call.
 */
static int vbglR3WinCloseRegKey(HKEY hKey, int rc)
{
    LSTATUS lrc = RegCloseKey(hKey);
    if (   lrc == ERROR_SUCCESS
        || RT_FAILURE(rc))
        return rc;
    return RTErrConvertFromWin32(lrc);
}


/**
 * Queries a string value from a specified registry key.
 *
 * @return  IPRT status code.
 * @param   hKey                Handle of registry key to use.
 * @param   pwszValueName       The name of the value to query.
 * @param   cbHint              Size hint.
 * @param   ppszValue           Where to return value string on success. Free
 *                              with RTStrFree.
 */
static int vbglR3QueryRegistryString(HKEY hKey, PCRTUTF16 pwszValueName, uint32_t cbHint, char **ppszValue)
{
    AssertPtr(pwszValueName);
    AssertPtrReturn(ppszValue, VERR_INVALID_POINTER);

    /*
     * First try.
     */
    int rc;
    DWORD dwType;
    DWORD cbTmp = cbHint;
    PRTUTF16 pwszTmp = (PRTUTF16)RTMemTmpAllocZ(cbTmp + sizeof(RTUTF16));
    if (pwszTmp)
    {
        LSTATUS lrc = RegQueryValueExW(hKey, pwszValueName, NULL, &dwType, (BYTE *)pwszTmp, &cbTmp);
        if (lrc == ERROR_MORE_DATA)
        {
            /*
             * Allocate larger buffer and try again.
             */
            RTMemTmpFree(pwszTmp);
            cbTmp += 16;
            pwszTmp = (PRTUTF16)RTMemTmpAllocZ(cbTmp + sizeof(RTUTF16));
            if (!pwszTmp)
            {
                *ppszValue = NULL;
                return VERR_NO_TMP_MEMORY;
            }
            lrc = RegQueryValueExW(hKey, pwszValueName, NULL, &dwType, (BYTE *)pwszTmp, &cbTmp);
        }
        if (lrc == ERROR_SUCCESS)
        {
            /*
             * Check the type and convert to UTF-8.
             */
            if (dwType == REG_SZ)
                rc = RTUtf16ToUtf8(pwszTmp, ppszValue);
            else
                rc = VERR_WRONG_TYPE;
        }
        else
            rc = RTErrConvertFromWin32(lrc);
        RTMemTmpFree(pwszTmp);
    }
    else
        rc = VERR_NO_TMP_MEMORY;
    if (RT_SUCCESS(rc))
        return rc;
    *ppszValue = NULL;
    return rc;
}

#endif /* RT_OS_WINDOWS */


/**
 * Fallback for VbglR3GetAdditionsVersion.
 *
 * @copydoc VbglR3GetAdditionsVersion
 */
static int vbglR3GetAdditionsCompileTimeVersion(char **ppszVer, char **ppszVerExt, char **ppszRev)
{
    int rc = VINF_SUCCESS;
    if (ppszVer)
        rc = RTStrDupEx(ppszVer, VBOX_VERSION_STRING_RAW);
    if (RT_SUCCESS(rc))
    {
        if (ppszVerExt)
            rc = RTStrDupEx(ppszVerExt, VBOX_VERSION_STRING);
        if (RT_SUCCESS(rc))
        {
            if (ppszRev)
                rc = RTStrDupEx(ppszRev, RT_XSTR(VBOX_SVN_REV));
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;

            /* bail out: */
        }
        if (ppszVerExt)
        {
            RTStrFree(*ppszVerExt);
            *ppszVerExt = NULL;
        }
    }
    if (ppszVer)
    {
        RTStrFree(*ppszVer);
        *ppszVer = NULL;
    }
    return rc;
}


/**
 * Retrieves the installed Guest Additions version and/or revision.
 *
 * @returns IPRT status code
 * @param   ppszVer     Receives pointer of allocated raw version string
 *                      (major.minor.build). NULL is accepted. The returned
 *                      pointer must be freed using RTStrFree().
 * @param   ppszVerExt  Receives pointer of allocated full version string
 *                      (raw version + vendor suffix(es)). NULL is
 *                      accepted. The returned pointer must be freed using
 *                      RTStrFree().
 * @param   ppszRev     Receives pointer of allocated revision string. NULL is
 *                      accepted. The returned pointer must be freed using
 *                      RTStrFree().
 */
VBGLR3DECL(int) VbglR3GetAdditionsVersion(char **ppszVer, char **ppszVerExt, char **ppszRev)
{
    /*
     * Zap the return value up front.
     */
    if (ppszVer)
        *ppszVer    = NULL;
    if (ppszVerExt)
        *ppszVerExt = NULL;
    if (ppszRev)
        *ppszRev    = NULL;

#ifdef RT_OS_WINDOWS
    HKEY hKey;
    int rc = vbglR3WinOpenAdditionRegisterKey(&hKey);
    if (RT_SUCCESS(rc))
    {
        /*
         * Version.
         */
        if (ppszVer)
            rc = vbglR3QueryRegistryString(hKey, L"Version", 64, ppszVer);

        if (   RT_SUCCESS(rc)
            && ppszVerExt)
            rc = vbglR3QueryRegistryString(hKey, L"VersionExt", 128, ppszVerExt);

        /*
         * Revision.
         */
        if (   RT_SUCCESS(rc)
            && ppszRev)
            rc = vbglR3QueryRegistryString(hKey, L"Revision", 64, ppszRev);

        rc = vbglR3WinCloseRegKey(hKey, rc);

        /* Clean up allocated strings on error. */
        if (RT_FAILURE(rc))
        {
            if (ppszVer)
            {
                RTStrFree(*ppszVer);
                *ppszVer = NULL;
            }
            if (ppszVerExt)
            {
                RTStrFree(*ppszVerExt);
                *ppszVerExt = NULL;
            }
            if (ppszRev)
            {
                RTStrFree(*ppszRev);
                *ppszRev = NULL;
            }
        }
    }
    /*
     * No registry entries found, return the version string compiled into this binary.
     */
    else
        rc = vbglR3GetAdditionsCompileTimeVersion(ppszVer, ppszVerExt, ppszRev);
    return rc;

#else  /* !RT_OS_WINDOWS */
    /*
     * On non-Windows platforms just return the compile-time version string.
     */
    return vbglR3GetAdditionsCompileTimeVersion(ppszVer, ppszVerExt, ppszRev);
#endif /* !RT_OS_WINDOWS */
}


/**
 * Retrieves the installation path of Guest Additions.
 *
 * @returns IPRT status code
 * @param   ppszPath    Receives pointer of allocated installation path string.
 *                      The returned pointer must be freed using
 *                      RTStrFree().
 */
VBGLR3DECL(int) VbglR3GetAdditionsInstallationPath(char **ppszPath)
{
    int rc;

#ifdef RT_OS_WINDOWS
    /*
     * Get it from the registry.
     */
    HKEY hKey;
    rc = vbglR3WinOpenAdditionRegisterKey(&hKey);
    if (RT_SUCCESS(rc))
    {
        rc = vbglR3QueryRegistryString(hKey, L"InstallDir", MAX_PATH * sizeof(RTUTF16), ppszPath);
        if (RT_SUCCESS(rc))
            RTPathChangeToUnixSlashes(*ppszPath, true /*fForce*/);
        rc = vbglR3WinCloseRegKey(hKey, rc);
    }
#else
    /** @todo implement me */
    rc = VERR_NOT_IMPLEMENTED;
    RT_NOREF1(ppszPath);
#endif
    return rc;
}


/**
 * Reports the Guest Additions status of a certain facility to the host.
 *
 * @returns IPRT status code
 * @param   enmFacility     The facility to report the status on.
 * @param   enmStatus       The new status of the facility.
 * @param   fReserved       Flags reserved for future hacks.
 */
VBGLR3DECL(int) VbglR3ReportAdditionsStatus(VBoxGuestFacilityType enmFacility, VBoxGuestFacilityStatus enmStatus,
                                            uint32_t fReserved)
{
    VMMDevReportGuestStatus Report;
    RT_ZERO(Report);
    int rc = vmmdevInitRequest(&Report.header, VMMDevReq_ReportGuestStatus);
    if (RT_SUCCESS(rc))
    {
        Report.guestStatus.facility = enmFacility;
        Report.guestStatus.status   = enmStatus;
        Report.guestStatus.flags    = fReserved;

        rc = vbglR3GRPerform(&Report.header);
    }
    return rc;
}

