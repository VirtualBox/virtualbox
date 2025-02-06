/* $Id: VBoxWinDrvCommon.cpp 108245 2025-02-06 09:28:02Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxWinDrvCommon - Common Windows driver installation functions.
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
#include <iprt/ldr.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/once.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/utf16.h>

#include <VBox/err.h> /* For VERR_PLATFORM_ARCH_NOT_SUPPORTED.*/

#include "VBoxWinDrvCommon.h"


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
static int vboxWinDrvInfQueryContext(HINF hInf, LPCWSTR pwszSection, LPCWSTR pwszKey, PINFCONTEXT pCtx);


/**
 * Returns the type of an INF file.
 *
 * @returns Type of the INF file.
 * @param   hInf                INF handle to use.
 * @param   ppwszSection        Where to return main section of the driver.
 *                              Optional and can be NULL.
 */
VBOXWINDRVINFTYPE VBoxWinDrvInfGetTypeEx(HINF hInf, PRTUTF16 *ppwszSection)
{
    if (ppwszSection)
        *ppwszSection = NULL;

    /*
     * Regular driver?
     */

    /* Sorted by most likely-ness. */
    static PRTUTF16 s_apwsManufacturerSections[] =
    {
        /* Most likely (and doesn't have a decoration). */
        L"Manufacturer",
        L"Manufacturer" VBOXWINDRVINF_DOT_NT_NATIVE_ARCH_STR,
    };

    int rc;

    INFCONTEXT InfCtx;
    size_t     i;

    PCRTUTF16 pwszManufacturerSection = NULL;
    for (i = 0; i < RT_ELEMENTS(s_apwsManufacturerSections); i++)
    {
        rc = vboxWinDrvInfQueryContext(hInf, s_apwsManufacturerSections[i], NULL, &InfCtx);
        if (RT_SUCCESS(rc))
        {
            pwszManufacturerSection = s_apwsManufacturerSections[i];
            break;
        }
    }

    /*
     * Primitive driver?
     */

    /* Sorted by most likely-ness. */
    static PRTUTF16 s_apwszPrimitiveSections[] =
    {
        /* Most likely (and doesn't have a decoration). */
        L"DefaultInstall",
        L"DefaultInstall" VBOXWINDRVINF_DOT_NT_NATIVE_ARCH_STR,
        /** @todo Handle more specific decorations like "NTAMD64.6.3..10622". */
    };

    PCRTUTF16 pwszPrimitiveSection = NULL;
    for (i = 0; i < RT_ELEMENTS(s_apwszPrimitiveSections); i++)
    {
        rc = vboxWinDrvInfQueryContext(hInf, s_apwszPrimitiveSections[i], NULL, &InfCtx);
        if (RT_SUCCESS(rc))
        {
            pwszPrimitiveSection = s_apwszPrimitiveSections[i];
            break;
        }
    }

    /* If both sections are present, consider this INF file as being invalid.
     * Only one or the other has to be present. */
    if (   pwszManufacturerSection
        && pwszPrimitiveSection)
        return VBOXWINDRVINFTYPE_INVALID;
    if (pwszManufacturerSection)
    {
        if (ppwszSection)
            *ppwszSection = RTUtf16Dup(pwszManufacturerSection);
        return VBOXWINDRVINFTYPE_NORMAL;
    }
    if (pwszPrimitiveSection)
    {
        if (ppwszSection)
            *ppwszSection = RTUtf16Dup(pwszPrimitiveSection);
        return VBOXWINDRVINFTYPE_PRIMITIVE;
    }

    return VBOXWINDRVINFTYPE_INVALID;
}

/**
 * Returns the type of an INF file.
 *
 * @returns Type of the INF file.
 * @param   hInf                INF handle to use.
 */
VBOXWINDRVINFTYPE VBoxWinDrvInfGetType(HINF hInf)
{
    return VBoxWinDrvInfGetTypeEx(hInf, NULL);
}

/**
 * Queries an INF context from an INF handle.
 *
 * @returns VBox status code.
 * @param   hInf                INF handle to use.
 * @param   pwszSection         Section name to query context for.
 * @param   pwszKey             Key to query context for.
 * @param   pCtx                Where to return the INF context on success.
 */
static int vboxWinDrvInfQueryContext(HINF hInf, LPCWSTR pwszSection, LPCWSTR pwszKey, PINFCONTEXT pCtx)
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
 * @param   pcwcValue           Where to return the number of characters in the
 *                              string returned via \a ppwszValue, including the
 *                              zero terminator. Optional and can be NULL.
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
            return VBoxWinDrvInstErrorFromWin32(dwErr);
    }

    LPWSTR pwszValue = (LPWSTR)RTMemAlloc(cwcValue * sizeof(pwszValue[0]));
    AssertPtrReturn(pwszValue, VERR_NO_MEMORY);

    if (!SetupGetStringFieldW(pCtx, iValue, pwszValue, cwcValue, &cwcValue))
    {
        RTMemFree(pwszValue);
        return VBoxWinDrvInstErrorFromWin32(GetLastError());
    }

    *ppwszValue = pwszValue;
    if (pcwcValue)
        *pcwcValue = cwcValue;

    return VINF_SUCCESS;
}

/**
 * Queries a model name from an INF section.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if no model has been found.
 * @param   hInf                INF handle to use.
 * @param   pwszSection         Section to query model for.
 * @param   uIndex              Index of model to query.
 *                              Currently only the first model (index 0) is supported.
 * @param   ppwszValue          Where to return the model name on success.
 * @param   pcwcValue           Where to return the number of characters in the
 *                              string returned via \a ppwszValue, including the
 *                              zero terminator. Optional and can be NULL.
 */
int VBoxWinDrvInfQueryModelEx(HINF hInf, PCRTUTF16 pwszSection, unsigned uIndex, PRTUTF16 *ppwszValue, PDWORD pcwcValue)
{
    *ppwszValue = NULL;
    if (pcwcValue)
        *pcwcValue = 0;
    AssertPtrReturn(pwszSection, VERR_INVALID_POINTER);
    AssertReturn(uIndex == 0, VERR_INVALID_PARAMETER);

    INFCONTEXT InfCtx;
    int rc = vboxWinDrvInfQueryContext(hInf, pwszSection, NULL, &InfCtx);
    if (RT_FAILURE(rc))
        return rc;

    PRTUTF16 pwszModel;
    DWORD    cwcModels;
    rc = VBoxWinDrvInfQueryKeyValue(&InfCtx, 1, &pwszModel, &cwcModels);
    if (RT_FAILURE(rc))
        return rc;

    PRTUTF16 pwszPlatform = NULL;
    DWORD    cwcPlatform;
    rc = VBoxWinDrvInfQueryKeyValue(&InfCtx, 2, &pwszPlatform, &cwcPlatform);
    if (RT_SUCCESS(rc)) /* Platform is optional. */
    {
        /* Convert to uppercase first so that RTUtf16FindAscii() below works. */
        RTUtf16ToUpper(pwszPlatform);

        /* Note! The platform can be more specific, e.g. "NTAMD64.6.0". */
        if (RTUtf16FindAscii(pwszPlatform, VBOXWINDRVINF_NT_NATIVE_ARCH_STR) == 0)
        {
            RTUTF16 wszSection[VBOXWINDRVINF_MAX_SECTION_NAME_LEN];
            rc = RTUtf16Copy(wszSection, RT_ELEMENTS(wszSection), pwszModel);
            if (RT_SUCCESS(rc))
            {
                rc = RTUtf16Cat(wszSection, RT_ELEMENTS(wszSection), VBOXWINDRVINF_DECORATION_SEP_UTF16_STR);
                if (RT_SUCCESS(rc))
                {
                    rc = RTUtf16Cat(wszSection, RT_ELEMENTS(wszSection), pwszPlatform);
                    if (RT_SUCCESS(rc))
                    {
                        /** @todo r=bird: Mixing RTMemAlloc and RTUtf16Dup/RTUtf16Alloc is not
                         *        allowed because of RTMEM_WRAP_TO_EF_APIS and other reasons!  */
                        PRTUTF16 pwszResult = RTUtf16Dup(wszSection);
                        if (pwszResult)
                        {
                            *ppwszValue = pwszResult;
                            if (pcwcValue)
                                *pcwcValue = (DWORD)(RTUtf16Len(wszSection) + 1);
                        }
                        else
                            rc = VERR_NO_MEMORY;
                    }
                }
            }
        }
        else
            rc = VERR_PLATFORM_ARCH_NOT_SUPPORTED;
        RTMemFree(pwszPlatform);
        RTMemFree(pwszModel);
    }
    else
    {
        /* Model without platform - just return the pwszModel string. */
        *ppwszValue = pwszModel;
        if (pcwcValue)
            *pcwcValue = cwcModels;
        rc = VINF_SUCCESS;
    }

    return rc;
}

int VBoxWinDrvInfQueryInstallSectionEx(HINF hInf, PCRTUTF16 pwszModel, PRTUTF16 *ppwszValue, PDWORD pcwcValue)
{
    INFCONTEXT InfCtx;
    int rc = vboxWinDrvInfQueryContext(hInf, pwszModel, NULL, &InfCtx);
    if (RT_FAILURE(rc))
        return rc;

    return VBoxWinDrvInfQueryKeyValue(&InfCtx, 1, ppwszValue, pcwcValue);
}

int VBoxWinDrvInfQueryInstallSection(HINF hInf, PCRTUTF16 pwszModel, PRTUTF16 *ppwszValue)
{
    return VBoxWinDrvInfQueryInstallSectionEx(hInf, pwszModel, ppwszValue, NULL);
}

/**
 * Queries the "Version" section of an INF file, extended version.
 *
 * @returns VBox status code.
 * @param   hInf                INF handle to use.
 * @param   uIndex              Index of version information to query. Usually 0.
 * @param   pVer                Where to return the Version section information on success.
 */
int VBoxWinDrvInfQuerySectionVerEx(HINF hInf, UINT uIndex, PVBOXWINDRVINFSECVERSION pVer)
{
    DWORD dwSize = 0;
    bool fRc = SetupGetInfInformationW(hInf, INFINFO_INF_SPEC_IS_HINF, NULL, 0, &dwSize);
    if (!fRc || !dwSize)
        return VERR_NOT_FOUND;

    int rc;

    PSP_INF_INFORMATION pInfo = (PSP_INF_INFORMATION)RTMemAllocZ(dwSize);
    AssertPtrReturn(pInfo, VERR_NO_MEMORY);
    fRc = SetupGetInfInformationW(hInf, INFINFO_INF_SPEC_IS_HINF, pInfo, dwSize, NULL);
    if (fRc)
    {
        if (pInfo->InfStyle == INF_STYLE_WIN4)
        {
            /*
             * We query all the keys and values for the given index.
             */
            dwSize = 0;
            fRc = SetupQueryInfVersionInformationW(pInfo, uIndex, NULL /* Key, NULL means all */,
                                                   NULL, 0, &dwSize);
            if (fRc)
            {
                PRTUTF16 pwszzInfo = (PRTUTF16)RTMemAllocZ(dwSize * sizeof(RTUTF16));
                if (pwszzInfo)
                {
                    fRc = SetupQueryInfVersionInformationW(pInfo, uIndex, NULL /* Key, NULL means all */,
                                                           pwszzInfo, dwSize, NULL);
                    if (fRc)
                    {
                        /*
                         * Parse the block of "key1\0value1\0\key2\0\value2\0....\0\0" strings,
                         * snapping up stuff we know.
                         */
                        rc = VINF_SUCCESS;
                        size_t off  = 0;
                        while (off < dwSize)
                        {
                            PRTUTF16 const pwszKey = &pwszzInfo[off];
                            size_t const   cwcKey  = RTUtf16NLen(pwszKey, dwSize - off);
                            off += cwcKey + 1;
                            if (off >= dwSize)
                                break;
                            PRTUTF16 const pwszValue = &pwszzInfo[off];
                            size_t const   cwcValue  = RTUtf16NLen(pwszValue, dwSize - off);
                            off += cwcValue + 1;

/** Macro to find a specific key and assign its value to the given string. */
#define GET_VALUE(a_wszKey, a_wszDst) \
    if (!RTUtf16ICmp(pwszKey, a_wszKey)) \
    { \
        rc = RTUtf16CopyEx(a_wszDst, RT_ELEMENTS(a_wszDst), pwszValue, cwcValue); \
        AssertRCBreak(rc); \
    }
                            GET_VALUE(L"DriverVer", pVer->wszDriverVer);
                            GET_VALUE(L"Provider", pVer->wszProvider);
                            GET_VALUE(L"CatalogFile", pVer->wszCatalogFile);
#undef GET_VALUE
                        }
                        Assert(off == dwSize);
                    }
                    else
                        rc = VBoxWinDrvInstErrorFromWin32(GetLastError());
                    RTMemFree(pwszzInfo);
                }
                else
                    rc = VERR_NO_MEMORY;
            }
            else
                rc = VBoxWinDrvInstErrorFromWin32(GetLastError());
        }
        else /* Legacy INF files are not supported. */
            rc = VERR_NOT_SUPPORTED;
    }
    else
        rc = VBoxWinDrvInstErrorFromWin32(GetLastError());

    RTMemFree(pInfo);
    return rc;
}

/**
 * Queries the "Version" section of an INF file.
 *
 * @returns VBox status code.
 * @param   hInf                INF handle to use.
 * @param   pVer                Where to return the Version section information on success.
 */
int VBoxWinDrvInfQuerySectionVer(HINF hInf, PVBOXWINDRVINFSECVERSION pVer)
{
    return VBoxWinDrvInfQuerySectionVerEx(hInf, 0 /* uIndex */, pVer);
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
    *phInf = hInf;
    if (hInf != INVALID_HANDLE_VALUE)
        return VBoxWinDrvInstErrorFromWin32(GetLastError());
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
    int     rc;
    GUID    guid = {};
    RTUTF16 wszClassName[MAX_CLASS_NAME_LEN] = {};
    if (SetupDiGetINFClassW(pwszInfFile, &guid, wszClassName, RT_ELEMENTS(wszClassName), NULL))
        rc = VBoxWinDrvInfOpenEx(pwszInfFile, wszClassName, phInf);
    else
        rc = VBoxWinDrvInstErrorFromWin32(GetLastError());
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
 * @retval  VERR_NOT_FOUND if no model has been found.
 * @param   hInf                INF handle to use.
 * @param   pwszSection         Section to query model for.
 * @param   ppwszModel          Where to return the model on success.
 *                              Needs to be free'd by RTUtf16Free().
 */
int VBoxWinDrvInfQueryFirstModel(HINF hInf, PCRTUTF16 pwszSection, PRTUTF16 *ppwszModel)
{
    *ppwszModel = NULL;

    return VBoxWinDrvInfQueryModelEx(hInf, pwszSection, 0 /* Index */, ppwszModel, NULL);
}

/**
 * Queries the first PnP ID from an INF file.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if no PnP ID has been found.
 * @param   hInf                INF handle to use.
 * @param   pwszModel           Model to query PnP ID for.
 * @param   ppwszPnPId          Where to return the PnP ID on success.
 *                              Needs to be free'd by RTUtf16Free().
 */
int VBoxWinDrvInfQueryFirstPnPId(HINF hInf, PRTUTF16 pwszModel, PRTUTF16 *ppwszPnPId)
{
    if (!pwszModel) /* No model given? Bail out early. */
        return VERR_NOT_FOUND;

    *ppwszPnPId = NULL;

    PRTUTF16   pwszPnPId = NULL;
    INFCONTEXT InfCtx;
    int rc = vboxWinDrvInfQueryContext(hInf, pwszModel, NULL, &InfCtx);
    if (RT_SUCCESS(rc))
    {
        rc = VBoxWinDrvInfQueryKeyValue(&InfCtx, 2, &pwszPnPId, NULL);
        if (RT_SUCCESS(rc))
            *ppwszPnPId = pwszPnPId;
    }

    return rc;
}


/**
 * Returns a Setup API error as a string.
 *
 * Needded to get at least a minimally meaningful error string back from Setup API.
 *
 * @returns Setup API error as a string, or NULL if not found.
 * @param   dwErr               Error code to return as a string.
 *
 * @note    Keep this sorted for easier lookup.
 */
const char *VBoxWinDrvSetupApiErrToStr(const DWORD dwErr)
{
    switch (dwErr)
    {
        RT_CASE_RET_STR(ERROR_AUTHENTICODE_DISALLOWED           );
        RT_CASE_RET_STR(ERROR_AUTHENTICODE_PUBLISHER_NOT_TRUSTED);
        RT_CASE_RET_STR(ERROR_AUTHENTICODE_TRUST_NOT_ESTABLISHED);
        RT_CASE_RET_STR(ERROR_AUTHENTICODE_TRUSTED_PUBLISHER    );
        RT_CASE_RET_STR(ERROR_BAD_INTERFACE_INSTALLSECT         );
        RT_CASE_RET_STR(ERROR_BAD_SECTION_NAME_LINE             );
        RT_CASE_RET_STR(ERROR_BAD_SERVICE_INSTALLSECT           );
        RT_CASE_RET_STR(ERROR_CANT_LOAD_CLASS_ICON              );
        RT_CASE_RET_STR(ERROR_CANT_REMOVE_DEVINST               );
        RT_CASE_RET_STR(ERROR_CLASS_MISMATCH                    );
        RT_CASE_RET_STR(ERROR_DEVICE_INSTALL_BLOCKED            );
        RT_CASE_RET_STR(ERROR_DEVICE_INSTALLER_NOT_READY        );
        RT_CASE_RET_STR(ERROR_DEVICE_INTERFACE_ACTIVE           );
        RT_CASE_RET_STR(ERROR_DEVICE_INTERFACE_REMOVED          );
        RT_CASE_RET_STR(ERROR_DEVINFO_DATA_LOCKED               );
        RT_CASE_RET_STR(ERROR_DEVINFO_LIST_LOCKED               );
        RT_CASE_RET_STR(ERROR_DEVINFO_NOT_REGISTERED            );
        RT_CASE_RET_STR(ERROR_DEVINST_ALREADY_EXISTS            );
        RT_CASE_RET_STR(ERROR_DEVINSTALL_QUEUE_NONNATIVE        );
        RT_CASE_RET_STR(ERROR_DI_BAD_PATH                       );
        RT_CASE_RET_STR(ERROR_DI_DO_DEFAULT                     );
        RT_CASE_RET_STR(ERROR_DI_DONT_INSTALL                   );
        RT_CASE_RET_STR(ERROR_DI_FUNCTION_OBSOLETE              );
        RT_CASE_RET_STR(ERROR_DI_NOFILECOPY                     );
        RT_CASE_RET_STR(ERROR_DI_POSTPROCESSING_REQUIRED        );
        RT_CASE_RET_STR(ERROR_DRIVER_INSTALL_BLOCKED            );
        RT_CASE_RET_STR(ERROR_DRIVER_NONNATIVE                  );
        RT_CASE_RET_STR(ERROR_DRIVER_STORE_ADD_FAILED           );
        RT_CASE_RET_STR(ERROR_DRIVER_STORE_DELETE_FAILED        );
        RT_CASE_RET_STR(ERROR_DUPLICATE_FOUND                   );
        RT_CASE_RET_STR(ERROR_EXPECTED_SECTION_NAME             );
        RT_CASE_RET_STR(ERROR_FILE_HASH_NOT_IN_CATALOG          );
        RT_CASE_RET_STR(ERROR_FILEQUEUE_LOCKED                  );
        RT_CASE_RET_STR(ERROR_GENERAL_SYNTAX                    ); /* Also when INF file is empty. */
        RT_CASE_RET_STR(ERROR_IN_WOW64                          );
        RT_CASE_RET_STR(ERROR_INF_IN_USE_BY_DEVICES             );
        RT_CASE_RET_STR(ERROR_INVALID_CLASS                     );
        RT_CASE_RET_STR(ERROR_INVALID_CLASS_INSTALLER           );
        RT_CASE_RET_STR(ERROR_INVALID_COINSTALLER               );
        RT_CASE_RET_STR(ERROR_INVALID_DEVINST_NAME              );
        RT_CASE_RET_STR(ERROR_INVALID_FILTER_DRIVER             );
        RT_CASE_RET_STR(ERROR_INVALID_HWPROFILE                 );
        RT_CASE_RET_STR(ERROR_INVALID_INF_LOGCONFIG             );
        RT_CASE_RET_STR(ERROR_INVALID_MACHINENAME               );
        RT_CASE_RET_STR(ERROR_INVALID_PROPPAGE_PROVIDER         );
        RT_CASE_RET_STR(ERROR_INVALID_REFERENCE_STRING          );
        RT_CASE_RET_STR(ERROR_INVALID_REG_PROPERTY              );
        RT_CASE_RET_STR(ERROR_INVALID_TARGET                    );
        RT_CASE_RET_STR(ERROR_KEY_DOES_NOT_EXIST                );
        RT_CASE_RET_STR(ERROR_LINE_NOT_FOUND                    );
        RT_CASE_RET_STR(ERROR_MACHINE_UNAVAILABLE               );
        RT_CASE_RET_STR(ERROR_NO_ASSOCIATED_CLASS               );
        RT_CASE_RET_STR(ERROR_NO_ASSOCIATED_SERVICE             );
        RT_CASE_RET_STR(ERROR_NO_AUTHENTICODE_CATALOG           );
        RT_CASE_RET_STR(ERROR_NO_BACKUP                         );
        RT_CASE_RET_STR(ERROR_NO_CATALOG_FOR_OEM_INF            );
        RT_CASE_RET_STR(ERROR_NO_CLASS_DRIVER_LIST              );
        RT_CASE_RET_STR(ERROR_NO_CLASSINSTALL_PARAMS            );
        RT_CASE_RET_STR(ERROR_NO_COMPAT_DRIVERS                 );
        RT_CASE_RET_STR(ERROR_NO_CONFIGMGR_SERVICES             );
        RT_CASE_RET_STR(ERROR_NO_DEFAULT_DEVICE_INTERFACE       );
        RT_CASE_RET_STR(ERROR_NO_DEVICE_ICON                    );
        RT_CASE_RET_STR(ERROR_NO_DEVICE_SELECTED                );
        RT_CASE_RET_STR(ERROR_NO_DRIVER_SELECTED                );
        RT_CASE_RET_STR(ERROR_NO_INF                            );
        RT_CASE_RET_STR(ERROR_NO_SUCH_DEVICE_INTERFACE          );
        RT_CASE_RET_STR(ERROR_NO_SUCH_DEVINST                   );
        RT_CASE_RET_STR(ERROR_NO_SUCH_INTERFACE_CLASS           );
        RT_CASE_RET_STR(ERROR_NON_WINDOWS_DRIVER                );
        RT_CASE_RET_STR(ERROR_NON_WINDOWS_NT_DRIVER             );
        RT_CASE_RET_STR(ERROR_NOT_AN_INSTALLED_OEM_INF          );
        RT_CASE_RET_STR(ERROR_NOT_DISABLEABLE                   );
        RT_CASE_RET_STR(ERROR_NOT_INSTALLED                     );
        RT_CASE_RET_STR(ERROR_ONLY_VALIDATE_VIA_AUTHENTICODE    );
        RT_CASE_RET_STR(ERROR_PNP_REGISTRY_ERROR                );
        RT_CASE_RET_STR(ERROR_REMOTE_COMM_FAILURE               );
        RT_CASE_RET_STR(ERROR_REMOTE_REQUEST_UNSUPPORTED        );
        RT_CASE_RET_STR(ERROR_SCE_DISABLED                      );
        RT_CASE_RET_STR(ERROR_SECTION_NAME_TOO_LONG             );
        RT_CASE_RET_STR(ERROR_SECTION_NOT_FOUND                 );
        RT_CASE_RET_STR(ERROR_SET_SYSTEM_RESTORE_POINT          );
        RT_CASE_RET_STR(ERROR_SIGNATURE_OSATTRIBUTE_MISMATCH    );
        RT_CASE_RET_STR(ERROR_UNKNOWN_EXCEPTION                 );
        RT_CASE_RET_STR(ERROR_WRONG_INF_STYLE                   );
        RT_CASE_RET_STR(ERROR_WRONG_INF_TYPE                    );
        default:
            break;
    }

    return NULL;
}

/**
 * Returns a winerr.h error as a string.
 *
 * Needded to get at least a minimally meaningful error string back.
 *
 * @returns Error as a string, or NULL if not found.
 * @param   dwErr               Error code to return as a string.
 */
const char *VBoxWinDrvWinErrToStr(const DWORD dwErr)
{
    switch (dwErr)
    {
        RT_CASE_RET_STR(ERROR_BADKEY                            );
        RT_CASE_RET_STR(ERROR_SERVICE_MARKED_FOR_DELETE         );
        RT_CASE_RET_STR(CERT_E_EXPIRED                          );
        RT_CASE_RET_STR(CERT_E_UNTRUSTEDTESTROOT                );
        RT_CASE_RET_STR(CERT_E_CHAINING                         );
        RT_CASE_RET_STR(CERT_E_REVOKED                          );
        RT_CASE_RET_STR(CERT_E_WRONG_USAGE                      );
        default:
            break;
    }

    return NULL;
}

/**
 * Translates a native Windows error code to a VBox one.
 *
 * @returns VBox status code.
 * @retval  VERR_UNRESOLVED_ERROR if no translation was possible.
 * @retval  VERR_INSTALLATION_FAILED if a Setup API or a specific Windows error code occurred not handled
 *          within IPRT's error resolving function.
 * @param   uNativeCode         Native Windows error code to translate.
 */
int VBoxWinDrvInstErrorFromWin32(unsigned uNativeCode)
{
    const char *pszErr = VBoxWinDrvSetupApiErrToStr(uNativeCode);
    if (!pszErr)
        pszErr = VBoxWinDrvWinErrToStr(uNativeCode);

    /* All we can do here is to return VERR_INSTALLATION_FAILED if the above calls returned something,
     * as we don't have IPRT equivalents for all those (Windows- / SetupAPI-)specific error codes.
     *
     * For anything else not (yet) handled we want to get a debug assertion, however. */
    int rc = VERR_INSTALLATION_FAILED;
    if (!pszErr)
    {
        rc = RTErrConvertFromWin32(uNativeCode);
        if (rc == VERR_UNRESOLVED_ERROR)
            AssertMsgFailed(("Unhandled error %u (%#x): %s\n", uNativeCode, uNativeCode, pszErr ? pszErr : "<Unknown>"));
    }

    return rc;
}

/**
 * Queries a DWORD value from a Windows registry key, Unicode (wide char) version.
 *
 * @returns IPRT status code.
 * @retval  VERR_FILE_NOT_FOUND if the value has not been found.
 * @retval  VERR_WRONG_TYPE if the type (DWORD) of the value does not match.
 * @retval  VERR_MISMATCH if the type sizes do not match.
 * @param   hKey                    Registry handle of key to query.
 * @param   pwszName                Name of the value to query.
 * @param   pdwValue                Where to return the actual value on success.
 *
 * @note    Taken from IPRT's rtSystemWinRegistryQueryDWORDW.
 */
int VBoxWinDrvRegQueryDWORDW(HKEY hKey, LPCWSTR pwszName, DWORD *pdwValue)
{
    int rc = VINF_SUCCESS;

    DWORD cbType = sizeof(DWORD);
    DWORD dwType = 0;
    DWORD dwValue;
    LONG lErr = RegQueryValueExW(hKey, pwszName, NULL, &dwType, (BYTE *)&dwValue, &cbType);
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

    return rc;
}

/**
 * Queries a DWORD value from a Windows registry key.
 *
 * @returns IPRT status code.
 * @retval  VERR_FILE_NOT_FOUND if the value has not been found.
 * @retval  VERR_WRONG_TYPE if the type (DWORD) of the value does not match.
 * @retval  VERR_MISMATCH if the type sizes do not match.
 * @param   hKey                    Registry handle of key to query.
 * @param   pszName                 Name of the value to query.
 * @param   pdwValue                Where to return the actual value on success.
 */
int VBoxWinDrvRegQueryDWORD(HKEY hKey, const char *pszName, DWORD *pdwValue)
{
    PRTUTF16 pwszName;
    int rc = RTStrToUtf16Ex(pszName, RTSTR_MAX, &pwszName, 0, NULL);
    if (RT_SUCCESS(rc))
    {
        rc = VBoxWinDrvRegQueryDWORDW(hKey, pwszName, pdwValue);
        RTUtf16Free(pwszName);
    }

    return rc;
}

