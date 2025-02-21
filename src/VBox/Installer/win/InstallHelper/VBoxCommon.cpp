/* $Id: VBoxCommon.cpp 108493 2025-02-21 08:15:34Z andreas.loeffler@oracle.com $ */
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

#include <iprt/mem.h>
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
 * Destroys a custom action data entry.
 *
 * @param   pEntry              Custom action data entry to destroy.
 */
static void vboxMsiCustomActionDataEntryDestroy(PVBOXMSICUSTOMACTIONDATAENTRY pEntry)
{
    if (!pEntry)
        return;

    RTStrFree(pEntry->pszKey);
    pEntry->pszKey = NULL;
    RTStrFree(pEntry->pszVal);
    pEntry->pszVal = NULL;
}

/**
 * Queries custom action data entries, extended version.
 *
 * @returns VBox status code.
 * @param   hMsi                MSI handle to use.
 * @param   pszSep              Separator to use for parsing the key=value pairs.
 * @param   ppaEntries          Where to return the allocated custom action data entries.
                                Might be NULL if \a pcEntries returns 0.
 *                              Must be destroyed using vboxMsiCustomActionDataEntryDestroy().
 * @param   pcEntries           Where to return the number of allocated custom action data entries of \a ppaEntries.
 *
 * @note    The "CustomActionData" property used is fixed by the MSI engine and must not be changed.
 */
static int vboxMsiCustomActionDataQueryEx(MSIHANDLE hMsi, const char *pszSep, PVBOXMSICUSTOMACTIONDATAENTRY *ppaEntries, size_t *pcEntries)
{
    char *pszData = NULL;
    int rc = VBoxMsiQueryPropUtf8(hMsi, "CustomActionData", &pszData);
    if (RT_FAILURE(rc))
        return rc;

    *ppaEntries = NULL;

    char **ppapszPairs; /* key=value pairs. */
    size_t cPairs;
    rc = RTStrSplit(pszData, strlen(pszData) + 1 /* Must include terminator */, pszSep, &ppapszPairs, &cPairs);
    if (   RT_SUCCESS(rc)
        && cPairs)
    {
        PVBOXMSICUSTOMACTIONDATAENTRY paEntries =
            (PVBOXMSICUSTOMACTIONDATAENTRY)RTMemAllocZ(cPairs * sizeof(VBOXMSICUSTOMACTIONDATAENTRY));
        if (paEntries)
        {
            size_t i = 0;
            for (; i < cPairs; i++)
            {
                const char *pszPair = ppapszPairs[i];

                char **ppapszKeyVal;
                size_t cKeyVal;
                rc = RTStrSplit(pszPair, strlen(pszPair) + 1 /* Must include terminator */, "=", &ppapszKeyVal, &cKeyVal);
                if (RT_SUCCESS(rc))
                {
                    if (cKeyVal == 2) /* Exactly one key=val pair. */
                    {
                        /* paEntries[i] will take ownership of ppapszKeyVal. */
                        paEntries[i].pszKey = ppapszKeyVal[0];
                        ppapszKeyVal[0] = NULL;
                        paEntries[i].pszVal = ppapszKeyVal[1];
                        ppapszKeyVal[1] = NULL;
                    }
                    else
                        rc = VERR_INVALID_PARAMETER;

                    for (size_t a = 0; a < cKeyVal; a++)
                        RTStrFree(ppapszKeyVal[a]);
                    RTMemFree(ppapszKeyVal);
                }

                if (RT_FAILURE(rc))
                    break;
            }

            if (RT_FAILURE(rc))
            {
                /* Rollback on failure. */
                while (i)
                    vboxMsiCustomActionDataEntryDestroy(&paEntries[i--]);
                RTMemFree(paEntries);
            }
            else
            {
                *ppaEntries = paEntries;
                *pcEntries  = cPairs;
            }
        }
        else
            rc = VERR_NO_MEMORY;

        for (size_t i = 0; i < cPairs; i++)
            RTStrFree(ppapszPairs[i]);
        RTMemFree(ppapszPairs);
    }

    return rc;
}

/**
 * Queries custom action data entries.
 *
 * @returns VBox status code.
 * @param   hMsi                MSI handle to use.
 * @param   ppaEntries          Where to return the allocated custom action data entries.
 *                              Must be destroyed using vboxMsiCustomActionDataEntryDestroy().
 * @param   pcEntries           Where to return the number of allocated custom action data entries of \a ppaEntries.
 */
static int vboxMsiCustomActionDataQuery(MSIHANDLE hMsi, PVBOXMSICUSTOMACTIONDATAENTRY *ppaEntries, size_t *pcEntries)
{
    return vboxMsiCustomActionDataQueryEx(hMsi, VBOX_MSI_CUSTOMACTIONDATA_SEP_STR, ppaEntries, pcEntries);
}

/**
 * Frees custom action data.
 *
 * @returns VBox status code.
 * @param   pData               Custom action data to free.
 *                              The pointer will be invalid on return.
 */
void VBoxMsiCustomActionDataFree(PVBOXMSICUSTOMACTIONDATA pData)
{
    if (!pData)
        return;

    for (size_t i = 0; i < pData->cEntries; i++)
        vboxMsiCustomActionDataEntryDestroy(&pData->paEntries[i]);

    RTMemFree(pData);
    pData = NULL;
}

/**
 * Queries custom action data, extended version.
 *
 * @returns VBox status code.
 * @param   hMsi                MSI handle to use.
 * @param   pszSep              Separator to use for parsing the key=value pairs.
 * @param   ppData              Where to return the allocated custom action data.
 *                              Needs to be free'd using VBoxMsiCustomActionDataFree().
 */
int VBoxMsiCustomActionDataQueryEx(MSIHANDLE hMsi, const char *pszSep, PVBOXMSICUSTOMACTIONDATA *ppData)
{
    AssertPtrReturn(pszSep, VERR_INVALID_POINTER);
    AssertPtrReturn(ppData, VERR_INVALID_POINTER);

    PVBOXMSICUSTOMACTIONDATA pData = (PVBOXMSICUSTOMACTIONDATA)RTMemAllocZ(sizeof(VBOXMSICUSTOMACTIONDATA));
    AssertPtrReturn(pData, VERR_NO_MEMORY);

    int rc = vboxMsiCustomActionDataQueryEx(hMsi, pszSep, &pData->paEntries, &pData->cEntries);
    if (RT_SUCCESS(rc))
    {
        *ppData = pData;
    }
    else
        VBoxMsiCustomActionDataFree(pData);

    return rc;
}

/**
 * Queries custom action data.
 *
 * @returns VBox status code.
 * @param   hMsi                MSI handle to use.
 * @param   ppData              Where to return the allocated custom action data.
 *                              Needs to be free'd using VBoxMsiCustomActionDataFree().
 */
int VBoxMsiCustomActionDataQuery(MSIHANDLE hMsi, PVBOXMSICUSTOMACTIONDATA *ppData)
{
    return VBoxMsiCustomActionDataQueryEx(hMsi, VBOX_MSI_CUSTOMACTIONDATA_SEP_STR, ppData);
}

/**
 * Finds a key in custom action data and returns its value.
 *
 * @returns Value if found, or NULL if not found.
 * @param   pHaystack           Custom action data to search in.
 * @param   pszNeedle           Key to search for. Case-sensitive.
 */
const char *VBoxMsiCustomActionDataFind(PVBOXMSICUSTOMACTIONDATA pHaystack, const char *pszNeedle)
{
    AssertPtrReturn(pHaystack, NULL);
    AssertPtrReturn(pszNeedle, NULL);

    for (size_t i = 0; i < pHaystack->cEntries; i++)
    {
        if (!RTStrICmp(pHaystack->paEntries[i].pszKey, pszNeedle))
            return pHaystack->paEntries[i].pszVal;
    }

    return NULL;
}

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

