/* $Id: VBoxWinDrvCommon.cpp 107166 2024-11-26 09:03:23Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxWinDrvCommon - Common Windows driver functions.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/win/windows.h>
#include <iprt/win/setupapi.h>
#include <newdev.h> /* For INSTALLFLAG_XXX. */
#include <cfgmgr32.h> /* For MAX_DEVICE_ID_LEN. */

#include <iprt/assert.h>
#include <iprt/dir.h>
#include <iprt/errcore.h>
#include <iprt/ldr.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/once.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/utf16.h>

#ifdef DEBUG
# include <iprt/stream.h>
#endif

#include "VBoxWinDrvCommon.h"


/*********************************************************************************************************************************
*   Defines                                                                                                                      *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Implementation                                                                                                               *
*********************************************************************************************************************************/


/**
 * Queries an INF context from an INF handle.
 *
 * @returns VBox status code.
 * @param   hInf                INF handle to use.
 * @param   pwszSection         Section name to query context for.
 * @param   pwszKey             Key to query context for.
 * @param   pCtx                Where to return the INF context on success.
 */
int VBoxWinDrvInfQueryContext(HINF hInf, LPCWSTR pwszSection, LPCWSTR pwszKey, PINFCONTEXT pCtx)
{
    if (!SetupFindFirstLineW(hInf, pwszSection, pwszKey, pCtx))
        return VERR_NOT_FOUND;

    return VINF_SUCCESS;
}

/**
 * Queries a value from an INF context.
 *
 * @returns VBox status code.
 * @param   pCtx                INF context to use.
 * @param   iValue              Index to query.
 * @param   ppwszValue          Where to return the value on success.
 * @param   pcwcValue           Where to return the number of characters for \a ppwszValue. Optional an can be NULL.
 */
int VBoxWinDrvInfQueryKeyValue(PINFCONTEXT pCtx, DWORD iValue, PRTUTF16 *ppwszValue, PDWORD pcwcValue)
{
    *ppwszValue = NULL;
    if (pcwcValue)
        *pcwcValue = 0;

    DWORD cwcValue;
    if (!SetupGetStringFieldW(pCtx, iValue, NULL, 0, &cwcValue))
    {
        DWORD const dwErr = GetLastError();
        if (dwErr != ERROR_INSUFFICIENT_BUFFER)
            return RTErrConvertFromWin32(dwErr);
    }

    LPWSTR pwszValue = (LPWSTR)RTMemAlloc(cwcValue * sizeof(pwszValue[0]));
    AssertPtrReturn(pwszValue, VERR_NO_MEMORY);

    if (!SetupGetStringFieldW(pCtx, iValue, pwszValue, cwcValue, &cwcValue))
    {
        RTMemFree(pwszValue);
        return RTErrConvertFromWin32(GetLastError());
    }

    *ppwszValue = pwszValue;
    if (pcwcValue)
        *pcwcValue = cwcValue;

    return VINF_SUCCESS;
}

/**
 * Queries for the model name in an INF file.
 *
 * @returns VBox status code.
 * @param   hInf                INF handle to use.
 * @param   ppwszValue          Where to return the model name on success.
 * @param   pcwcValue           Where to return the number of characters for \a ppwszValue. Optional an can be NULL.
 */
int VBoxWinDrvInfQueryModelsSectionName(HINF hInf, PRTUTF16 *ppwszValue, PDWORD pcwcValue)
{
    *ppwszValue = NULL;
    if (pcwcValue)
        *pcwcValue = 0;

    /* Sorted by most likely-ness. */
    static PRTUTF16 s_apwszSections[] =
    {
        /* The more specific (using decorations), the better. Try these first. */
        L"Manufacturer"     VBOXWINDRVINF_DOT_NT_NATIVE_ARCH_STR,
        L"DefaultInstall"   VBOXWINDRVINF_DOT_NT_NATIVE_ARCH_STR,
        L"DefaultUninstall" VBOXWINDRVINF_DOT_NT_NATIVE_ARCH_STR,
        /* Try undecorated sections last. */
        L"Manufacturer",
        L"DefaultInstall",
        L"DefaultUninstall"
    };

    int rc = VINF_SUCCESS;

    INFCONTEXT InfCtx;
    for (int i = 0; i < RT_ELEMENTS(s_apwszSections); i++)
    {
        PRTUTF16 const pwszSection = s_apwszSections[i];
        rc = VBoxWinDrvInfQueryContext(hInf, pwszSection, NULL, &InfCtx);
        if (RT_SUCCESS(rc))
            break;
    }

    if (RT_FAILURE(rc))
        return rc;

    LPWSTR pwszModels;
    DWORD  cwcModels;
    rc = VBoxWinDrvInfQueryKeyValue(&InfCtx, 1, &pwszModels, &cwcModels);
    if (RT_FAILURE(rc))
        return rc;

    LPWSTR pwszPlatform = NULL;
    DWORD  cwcPlatform  = 0;
    bool   fArch        = false;
    bool   fNt          = false;

    LPWSTR pwszPlatformCur;
    DWORD  cwcPlatformCur;
    for (DWORD i = 2; (rc = VBoxWinDrvInfQueryKeyValue(&InfCtx, i, &pwszPlatformCur, &cwcPlatformCur)) == VINF_SUCCESS; ++i)
    {
        if (RTUtf16ICmpAscii(pwszPlatformCur, "NT" VBOXWINDRVINF_NATIVE_ARCH_STR) == 0)
            fArch = true;
        else
        {
            if (   fNt
                || RTUtf16ICmpAscii(pwszPlatformCur, "NT") != 0)
            {
                RTMemFree(pwszPlatformCur);
                pwszPlatformCur = NULL;
                continue;
            }
            fNt = true;
        }

        cwcPlatform = cwcPlatformCur;
        if (pwszPlatform)
            RTMemFree(pwszPlatform);
        pwszPlatform = pwszPlatformCur;
        pwszPlatformCur = NULL;
    }

    rc = VINF_SUCCESS;

    LPWSTR pwszResult = NULL;
    DWORD  cwcResult = 0;
    if (pwszPlatform)
    {
        pwszResult = (LPWSTR)RTMemAlloc((cwcModels + cwcPlatform) * sizeof(pwszResult[0]));
        if (pwszResult)
        {
            memcpy(pwszResult, pwszModels, (cwcModels - 1) * sizeof(pwszResult[0]));
            pwszResult[cwcModels - 1] = L'.';
            memcpy(&pwszResult[cwcModels], pwszPlatform, cwcPlatform * sizeof(pwszResult[0]));
            cwcResult = cwcModels + cwcPlatform;
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
    {
        pwszResult = pwszModels;
        cwcResult  = cwcModels;
        pwszModels = NULL;
    }

    if (pwszModels)
        RTMemFree(pwszModels);
    if (pwszPlatform)
        RTMemFree(pwszPlatform);

    if (RT_SUCCESS(rc))
    {
        *ppwszValue = pwszResult;
        if (pcwcValue)
            *pcwcValue = cwcResult;
    }

    return rc;
}

/**
 * Opens an INF file, extended version.
 *
 * @returns VBox status code.
 * @param   pwszInfFile         Path to INF file to open.
 * @param   pwszClassName       Class name to use.
 * @param   phInf               Where to return the INF handle on success.
 */
int VBoxWinDrvInfOpenEx(PCRTUTF16 pwszInfFile, PRTUTF16 pwszClassName, HINF *phInf)
{
    HINF hInf = SetupOpenInfFileW(pwszInfFile, pwszClassName, INF_STYLE_WIN4, NULL /*__in PUINT ErrorLine */);
    if (hInf == INVALID_HANDLE_VALUE)
        return RTErrConvertFromWin32(GetLastError());

    *phInf = hInf;

    return VINF_SUCCESS;
}

/**
 * Opens an INF file, wide char version.
 *
 * @returns VBox status code.
 * @param   pwszInfFile         Path to INF file to open.
 * @param   phInf               Where to return the INF handle on success.
 *
 * @note    Queryies the class name automatically from the given INF file.
 */
int VBoxWinDrvInfOpen(PCRTUTF16 pwszInfFile, HINF *phInf)
{
    int rc;

    GUID    guid = {};
    RTUTF16 pwszClassName[MAX_CLASS_NAME_LEN] = { };
    if (SetupDiGetINFClassW(pwszInfFile, &guid, &(pwszClassName[0]), sizeof(pwszClassName), NULL))
    {
        rc = VBoxWinDrvInfOpenEx(pwszInfFile, pwszClassName, phInf);
    }
    else
        rc = RTErrConvertFromWin32(GetLastError());

    return rc;
}

/**
 * Opens an INF file.
 *
 * @returns VBox status code.
 * @param   pszInfFile          Path to INF file to open.
 * @param   phInf               Where to return the INF handle on success.
 *
 * @note    Queryies the class name automatically from the given INF file.
 */
int VBoxWinDrvInfOpenUtf8(const char *pszInfFile, HINF *phInf)
{
    PRTUTF16 pwszInfFile;
    int rc = RTStrToUtf16(pszInfFile, &pwszInfFile);
    AssertRCReturn(rc, rc);

    rc = VBoxWinDrvInfOpen(pwszInfFile, phInf);

    RTUtf16Free(pwszInfFile);
    return rc;
}

/**
 * Closes an INF file.
 *
 * @returns VBox status code.
 * @param   hInf                INF handle to use.
 */
int VBoxWinDrvInfClose(HINF hInf)
{
    SetupCloseInfFile(hInf);

    return VINF_SUCCESS;
}

/**
 * Queries the first (device) model from an INF file.
 *
 * @returns VBox status code.
 * @param   hInf                INF handle to use.
 * @param   ppwszModel          Where to return the model on success.
 *                              Needs to be free'd by RTUtf16Free().
 */
int VBoxWinDrvInfQueryFirstModel(HINF hInf, PRTUTF16 *ppwszModel)
{
    *ppwszModel = NULL;

    return VBoxWinDrvInfQueryModelsSectionName(hInf, ppwszModel, NULL);
}

/**
 * Queries the first PnP ID from an INF file.
 *
 * @returns VBox status code.
 * @param   hInf                INF handle to use.
 * @param   pwszModel           Model to query PnP ID for.
 * @param   ppwszPnPId          Where to return the PnP ID on success.
 *                              Needs to be free'd by RTUtf16Free().
 */
int VBoxWinDrvInfQueryFirstPnPId(HINF hInf, PRTUTF16 pwszModel, PRTUTF16 *ppwszPnPId)
{
    *ppwszPnPId = NULL;

    PRTUTF16   pwszPnPId = NULL;
    INFCONTEXT InfCtx;
    int rc = VBoxWinDrvInfQueryContext(hInf, pwszModel, NULL, &InfCtx);
    if (RT_SUCCESS(rc))
    {
        rc = VBoxWinDrvInfQueryKeyValue(&InfCtx, 2, &pwszPnPId, NULL);
        if (RT_SUCCESS(rc))
            *ppwszPnPId = pwszPnPId;
    }

    return rc;
}

