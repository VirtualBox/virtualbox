/* $Id: tstDeviceCfg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstDevice - Configuration loader.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEFAULT /** @todo */
#include <VBox/types.h>
#include <iprt/errcore.h>
#include <iprt/json.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/string.h>

#include "tstDeviceCfg.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/**
 * Wrapper around RTErrInfoSetV / RTMsgErrorV.
 *
 * @returns @a rc
 * @param   pErrInfo            Extended error info.
 * @param   rc                  The return code.
 * @param   pszFormat           The message format.
 * @param   ...                 The message format arguments.
 */
static int tstDevCfgErrorRc(PRTERRINFO pErrInfo, int rc, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    if (pErrInfo)
        RTErrInfoSetV(pErrInfo, rc, pszFormat, va);
    else
        RTMsgErrorV(pszFormat, va);
    va_end(va);
    return rc;
}


/**
 * Destroys the given configuration item array freeing all allocated resources.
 *
 * @param   paCfg               The configuration item array to destroy.
 * @param   cCfgItems           Number of items in the array.
 */
static void tstDevCfgItemsDestroy(PTSTDEVCFGITEM paCfg, uint32_t cCfgItems)
{
    RT_NOREF(paCfg, cCfgItems);
}


/**
 * Loads the given string from the config, creating a duplicate.
 *
 * @returns VBox status code.
 * @param   hJsonTop            The JSON top value handle containing the value to load.
 * @param   pszValName          The value name.
 * @param   ppszValCopy         Where to store the pointer to the value on success, must be freed with RTStrFree().
 * @param   fMissingOk          Flag whether it is considered success if the value does not exist.
 * @param   pErrInfo            Pointer to the error info to fill on error.
 */
static int tstDevCfgLoadString(RTJSONVAL hJsonTop, const char *pszValName, char **ppszValCopy, bool fMissingOk, PRTERRINFO pErrInfo)
{
    RTJSONVAL hJsonVal;
    int rc = RTJsonValueQueryByName(hJsonTop, pszValName, &hJsonVal);
    if (RT_SUCCESS(rc))
    {
        const char *pszVal = RTJsonValueGetString(hJsonVal);
        if (RT_LIKELY(pszVal))
        {
            *ppszValCopy = RTStrDup(pszVal);
            if (RT_UNLIKELY(!*ppszValCopy))
                rc = tstDevCfgErrorRc(pErrInfo, VERR_NO_STR_MEMORY, "tstDevCfg/JSON: Out of memory allocating memory for value of \"%s\" ", pszValName);
        }
        else
            rc = tstDevCfgErrorRc(pErrInfo, VERR_JSON_VALUE_INVALID_TYPE, "tstDevCfg/JSON: \"%s\" is not a string", pszValName);

        RTJsonValueRelease(hJsonVal);
    }
    else if (   rc == VERR_NOT_FOUND
             && fMissingOk)
    {
        *ppszValCopy = NULL;
        rc = VINF_SUCCESS;
    }
    else
        rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: Failed to query \"%s\"", pszValName);

    return rc;
}


/**
 * Loads a bool value using the given value name from the config.
 *
 * @returns VBox status code.
 * @param   hJsonTop            The JSON top value handle containing the value to load.
 * @param   pszValName          The value name.
 * @param   pf                  Where to store the value on success.
 * @param   pErrInfo            Pointer to the error info to fill on error.
 */
static int tstDevCfgLoadBool(RTJSONVAL hJsonTop, const char *pszValName, bool *pf, PRTERRINFO pErrInfo)
{
    int rc = RTJsonValueQueryBooleanByName(hJsonTop, pszValName, pf);
    if (RT_FAILURE(rc))
        rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: Failed to query boolean value of \"%s\"", pszValName);

    return rc;
}


/**
 * Determines the config item type from the given.value.
 *
 * @returns VBox status code.
 * @param   hJsonTop            The JSON top value handle containing the value to load.
 * @param   pszValName          The value name.
 * @param   penmCfgItemType     Where to store the determined config item type on success.
 * @param   pErrInfo            Pointer to the error info to fill on error.
 */
static int tstDevCfgLoadCfgItemType(RTJSONVAL hJsonTop, const char *pszValName, PTSTDEVCFGITEMTYPE penmCfgItemType, PRTERRINFO pErrInfo)
{
    RTJSONVAL hJsonVal;
    int rc = RTJsonValueQueryByName(hJsonTop, pszValName, &hJsonVal);
    if (RT_SUCCESS(rc))
    {
        const char *pszVal = RTJsonValueGetString(hJsonVal);
        if (!RTStrCmp(pszVal, "Integer"))
            *penmCfgItemType = TSTDEVCFGITEMTYPE_INTEGER;
        else if (!RTStrCmp(pszVal, "String"))
            *penmCfgItemType = TSTDEVCFGITEMTYPE_STRING;
        else
            rc = tstDevCfgErrorRc(pErrInfo, VERR_JSON_VALUE_INVALID_TYPE, "tstDevCfg/JSON: \"%s\" is not a valid config item type", pszVal);

        RTJsonValueRelease(hJsonVal);
    }
    else
        rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: Failed to query \"%s\"", pszValName);

    return rc;
}


/**
 * Loads the config item value from the given config based on the earlier determined type.
 *
 * @returns VBox status code.
 * @param   hJsonTop            The JSON top value handle containing the value to load.
 * @param   pszValName          The value name.
 * @param   pCfg                Where to store the retrieved config value.
 * @param   enmCfgItemType      The earlier determined config item type.
 * @param   pErrInfo            Pointer to the error info to fill on error.
 */
static int tstDevCfgLoadCfgItemValue(RTJSONVAL hJsonTop, const char *pszValName, PTSTDEVCFGITEM pCfg, TSTDEVCFGITEMTYPE enmCfgItemType, PRTERRINFO pErrInfo)
{
    RTJSONVAL hJsonVal;

    int rc = RTJsonValueQueryByName(hJsonTop, pszValName, &hJsonVal);
    if (RT_SUCCESS(rc))
    {
        RTJSONVALTYPE enmJsonType = RTJsonValueGetType(hJsonVal);

        if (   (    enmJsonType == RTJSONVALTYPE_INTEGER
                && enmCfgItemType == TSTDEVCFGITEMTYPE_INTEGER)
            || (    enmJsonType == RTJSONVALTYPE_STRING
                && enmCfgItemType == TSTDEVCFGITEMTYPE_STRING))
        {
            switch (enmCfgItemType)
            {
                case TSTDEVCFGITEMTYPE_INTEGER:
                {
                    rc = RTJsonValueQueryInteger(hJsonVal, &pCfg->u.i64);
                    break;
                }
                case TSTDEVCFGITEMTYPE_STRING:
                {
                    const char *psz = RTJsonValueGetString(hJsonVal);
                    AssertPtr(psz);

                    pCfg->u.psz = RTStrDup(psz);
                    if (RT_UNLIKELY(!pCfg->u.psz))
                        rc = VERR_NO_STR_MEMORY;
                    break;
                }
                default:
                    AssertFailed(); /* Should never ever get here. */
                    rc = tstDevCfgErrorRc(pErrInfo, VERR_INTERNAL_ERROR, "tstDevCfg/JSON: Invalid config item type %u", enmCfgItemType);
            }

            if (RT_SUCCESS(rc))
                pCfg->enmType = enmCfgItemType;
            else
                rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: Failed to query config item value");
        }
        else
            rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: JSON value type doesn't match config item type (got %u, expected %u)", enmJsonType, enmCfgItemType);

        RTJsonValueRelease(hJsonVal);
    }
    else
        rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: Failed to query \"%s\"", pszValName);

    return rc;
}


/**
 * Loads the test configuration from the given JSON value.
 *
 * @returns VBox status code.
 * @param   paCfg               The configuration array to fill.
 * @param   cCfgItems           Number of configuration items.
 * @param   hJsonValCfg         The JSON value to gather the config items from.
 * @param   pErrInfo            Pointer to error info.
 */
static int tstDevCfgLoadTestCfgWorker(PTSTDEVCFGITEM paCfg, uint32_t cCfgItems, RTJSONVAL hJsonValCfg, PRTERRINFO pErrInfo)
{
    int rc = VINF_SUCCESS;

    for (uint32_t i = 0; i < cCfgItems && RT_SUCCESS(rc); i++)
    {
        PTSTDEVCFGITEM pCfg = &paCfg[i];
        RTJSONVAL hJsonCfg;

        rc = RTJsonValueQueryByIndex(hJsonValCfg, i, &hJsonCfg);
        if (RT_SUCCESS(rc))
        {
            TSTDEVCFGITEMTYPE enmCfgItemType;

            rc = tstDevCfgLoadString(hJsonCfg, "Key", (char **)&pCfg->pszKey, false /*fMissingOk*/, pErrInfo);
            if (RT_SUCCESS(rc))
                rc = tstDevCfgLoadCfgItemType(hJsonCfg, "Type", &enmCfgItemType, pErrInfo);
            if (RT_SUCCESS(rc))
                rc = tstDevCfgLoadCfgItemValue(hJsonCfg, "Value", pCfg, enmCfgItemType, pErrInfo);
        }
        else
            rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: Failed to query config item %u", i);
    }

    return rc;
}


/**
 * Loads a single testcase from the given JSON config value.
 *
 * @returns VBox status code.
 * @param   ppszTestcaseId      Where to store the testcase ID on success.
 * @param   pcTestcaseCfgItems  Where to store the number of testcase config items on success.
 * @param   ppTestcaseCfg       Where to store the testcase config on success.
 * @param   pErrInfo            Pointer to error info.
 */
static int tstDevCfgLoadTestcase(RTJSONVAL hJsonTestcase, const char **ppszTestcaseId, uint32_t *pcTestcaseCfgItems, PCTSTDEVCFGITEM *ppTestcaseCfg, PRTERRINFO pErrInfo)
{
    char *pszTestcaseId = NULL;
    int rc = tstDevCfgLoadString(hJsonTestcase, "Testcase", &pszTestcaseId, false /*fMissingOk*/, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        RTJSONVAL hJsonValCfg;
        rc = RTJsonValueQueryByName(hJsonTestcase, "Config", &hJsonValCfg);
        if (RT_SUCCESS(rc))
        {
            unsigned cCfgItems = 0;
            rc = RTJsonValueQueryArraySize(hJsonValCfg, &cCfgItems);
            if (RT_SUCCESS(rc))
            {
                if (cCfgItems > 0)
                {
                    size_t cbCfg = sizeof(TSTDEVCFGITEM) * cCfgItems;
                    PTSTDEVCFGITEM paCfg = (PTSTDEVCFGITEM)RTMemAllocZ(cbCfg);
                    if (paCfg)
                    {
                        rc = tstDevCfgLoadTestCfgWorker(paCfg, cCfgItems, hJsonValCfg, pErrInfo);
                        if (RT_SUCCESS(rc))
                        {
                            *ppszTestcaseId     = pszTestcaseId;
                            *pcTestcaseCfgItems = cCfgItems;
                            *ppTestcaseCfg      = paCfg;
                        }
                        else /* Error already set, free test config structure. */
                            tstDevCfgItemsDestroy(paCfg, cCfgItems);
                    }
                    else
                        rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: Failed to allocate %zu bytes for the test config structure", cbCfg);
                }
                else
                {
                    *ppszTestcaseId     = pszTestcaseId;
                    *pcTestcaseCfgItems = 0;
                    *ppTestcaseCfg      = NULL;
                }
            }
            else
                rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: \"Config\" is not an array");

            RTJsonValueRelease(hJsonValCfg);
        }
        else
            tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: Failed to query \"Config\" value");

        if (RT_FAILURE(rc))
            RTStrFree(pszTestcaseId);
    }

    return rc;
}


/**
 * Loads the testcase descriptions from the config.
 *
 * @returns VBox status code.
 * @param   pDevTest            Where to store the testcases config on success.
 * @param   hJsonValTest        Where to load the testcases config from.
 * @param   pErrInfo            Pointer to error info.
 */
static int tstDevCfgLoadTestcases(PTSTDEVTEST pDevTest, RTJSONVAL hJsonValTest, PRTERRINFO pErrInfo)
{
    RTJSONVAL hJsonValTestcases;
    int rc = RTJsonValueQueryByName(hJsonValTest, "Testcases", &hJsonValTestcases);
    if (RT_SUCCESS(rc))
    {
        unsigned cTestcases = 0;
        rc = RTJsonValueQueryArraySize(hJsonValTestcases, &cTestcases);
        if (RT_SUCCESS(rc))
        {
            pDevTest->cTestcases = cTestcases;
            if (cTestcases > 0)
            {
                size_t cbArray = sizeof(void *) * 2 * cTestcases + cTestcases * sizeof(uint32_t); /* One for the testcase ID and one for the associated configuration. */
                uint8_t *pbTmp = (uint8_t *)RTMemAllocZ(cbArray);
                if (pbTmp)
                {
                    pDevTest->papszTestcaseIds    = (const char **)pbTmp;
                    pDevTest->pacTestcaseCfgItems = (uint32_t *)&pDevTest->papszTestcaseIds[cTestcases];
                    pDevTest->papTestcaseCfg      = (PCTSTDEVCFGITEM *)&pDevTest->pacTestcaseCfgItems[cTestcases];

                    for (uint32_t i = 0; i < cTestcases; i++)
                    {
                        RTJSONVAL hJsonTestcase;

                        rc = RTJsonValueQueryByIndex(hJsonValTestcases, i, &hJsonTestcase);
                        if (RT_SUCCESS(rc))
                        {
                            rc = tstDevCfgLoadTestcase(hJsonTestcase, &pDevTest->papszTestcaseIds[i],
                                                       &pDevTest->pacTestcaseCfgItems[i], &pDevTest->papTestcaseCfg[i], pErrInfo);
                            RTJsonValueRelease(hJsonTestcase);
                        }
                        else
                            rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: Failed to query testcase item %u", i);
                    }
                }
                else
                    rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: Failed to allocate %zu bytes for the testcases", cbArray);
            }
            else
                rc = tstDevCfgErrorRc(pErrInfo, VERR_INVALID_PARAMETER, "tstDevCfg/JSON: \"Testcases\" doesn't contain anything");
        }
        else
            rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: \"Testcases\" is not an array");

        RTJsonValueRelease(hJsonValTestcases);
    }
    else
        tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: Failed to query \"Testcases\" value");

    return rc;
}


/**
 * Loads a test config from the given JSON object.
 *
 * @returns VBox status code.
 * @param   pDevTest            Where to store the test config on success.
 * @param   hJsonValTest        Where to load the test config from.
 * @param   pErrInfo            Pointer to error info.
 */
static int tstDevCfgLoadTest(PTSTDEVTEST pDevTest, RTJSONVAL hJsonValTest, PRTERRINFO pErrInfo)
{
    int rc = tstDevCfgLoadBool(hJsonValTest, "R0Enabled", &pDevTest->fR0Enabled, pErrInfo);
    if (RT_SUCCESS(rc))
        rc = tstDevCfgLoadBool(hJsonValTest, "RCEnabled", &pDevTest->fRCEnabled, pErrInfo);

    if (RT_SUCCESS(rc))
    {
        RTJSONVAL hJsonValCfg;
        rc = RTJsonValueQueryByName(hJsonValTest, "Config", &hJsonValCfg);
        if (RT_SUCCESS(rc))
        {
            unsigned cCfgItems = 0;
            rc = RTJsonValueQueryArraySize(hJsonValCfg, &cCfgItems);
            if (RT_SUCCESS(rc))
            {
                pDevTest->cCfgItems = cCfgItems;
                if (cCfgItems > 0)
                {
                    size_t cbCfg = sizeof(TSTDEVCFGITEM) * cCfgItems;
                    PTSTDEVCFGITEM paCfg = (PTSTDEVCFGITEM)RTMemAllocZ(cbCfg);
                    if (paCfg)
                    {
                        rc = tstDevCfgLoadTestCfgWorker(paCfg, cCfgItems, hJsonValCfg, pErrInfo);
                        if (RT_SUCCESS(rc))
                            pDevTest->paCfgItems = paCfg;
                        else /* Error already set, free test config structure. */
                            tstDevCfgItemsDestroy(paCfg, cCfgItems);
                    }
                    else
                        rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: Failed to allocate %zu bytes for the test config structure", cbCfg);
                }
            }
            else
                rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: \"Config\" is not an array");

            RTJsonValueRelease(hJsonValCfg);
        }
        else
            tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: Failed to query \"Config\" value");
    }

    /* Load the test configs. */
    if (RT_SUCCESS(rc))
        rc = tstDevCfgLoadTestcases(pDevTest, hJsonValTest, pErrInfo);

    return rc;
}


/**
 * Configuration loader worker.
 *
 * @returns VBox status code.
 * @param   pDevTstCfg          The test config structure to fill.
 * @param   hJsonRoot           Handle of the root JSON value.
 * @param   hJsonValDeviceTests Handle to the test JSON array.
 * @param   pErrInfo            Pointer to the error info.
 */
static int tstDevCfgLoadWorker(PTSTDEVCFG pDevTstCfg, RTJSONVAL hJsonRoot, RTJSONVAL hJsonValDeviceTests, PRTERRINFO pErrInfo)
{
    int rc = tstDevCfgLoadString(hJsonRoot, "PdmR3Module", (char **)&pDevTstCfg->pszPdmR3Mod, false /*fMissingOk*/, pErrInfo);
    if (RT_SUCCESS(rc))
        rc = tstDevCfgLoadString(hJsonRoot, "PdmR0Module", (char **)&pDevTstCfg->pszPdmR0Mod, true /*fMissingOk*/, pErrInfo);
    if (RT_SUCCESS(rc))
        rc = tstDevCfgLoadString(hJsonRoot, "PdmRCModule", (char **)&pDevTstCfg->pszPdmRCMod, true /*fMissingOk*/, pErrInfo);
    if (RT_SUCCESS(rc))
        rc = tstDevCfgLoadString(hJsonRoot, "TestcaseModule", (char **)&pDevTstCfg->pszTstDevMod, true /*fMissingOk*/, pErrInfo);
    if (RT_SUCCESS(rc))
        rc = tstDevCfgLoadString(hJsonRoot, "Device", (char **)&pDevTstCfg->pszDevName, false /*fMissingOk*/, pErrInfo);

    if (RT_SUCCESS(rc))
    {
        /* Load the individual test configs. */
        for (uint32_t idx = 0; idx < pDevTstCfg->cTests && RT_SUCCESS(rc); idx++)
        {
            RTJSONVAL hJsonValTest;

            rc = RTJsonValueQueryByIndex(hJsonValDeviceTests, idx, &hJsonValTest);
            if (RT_SUCCESS(rc))
            {
                rc = tstDevCfgLoadTest(&pDevTstCfg->aTests[idx], hJsonValTest, pErrInfo);
                RTJsonValueRelease(hJsonValTest);
            }
            else
                rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: Failed to query test %u from \"DeviceTests\"", idx);
        }
    }

    return rc;
}


DECLHIDDEN(int) tstDevCfgLoad(const char *pszCfgFilename, PRTERRINFO pErrInfo, PCTSTDEVCFG *ppDevTstCfg)
{
    RTJSONVAL hJsonRoot;
    int rc = RTJsonParseFromFile(&hJsonRoot, pszCfgFilename, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        RTJSONVAL hJsonValDeviceTests;

        rc = RTJsonValueQueryByName(hJsonRoot, "DeviceTests", &hJsonValDeviceTests);
        if (RT_SUCCESS(rc))
        {
            unsigned cTests = 0;
            rc = RTJsonValueQueryArraySize(hJsonValDeviceTests, &cTests);
            if (RT_SUCCESS(rc))
            {
                if (cTests > 0)
                {
                    size_t cbTestCfg = RT_UOFFSETOF_DYN(TSTDEVCFG, aTests[cTests]);
                    PTSTDEVCFG pDevTstCfg = (PTSTDEVCFG)RTMemAllocZ(cbTestCfg);
                    if (pDevTstCfg)
                    {
                        pDevTstCfg->cTests = cTests;
                        rc = tstDevCfgLoadWorker(pDevTstCfg, hJsonRoot, hJsonValDeviceTests, pErrInfo);
                        if (RT_SUCCESS(rc))
                            *ppDevTstCfg = pDevTstCfg;
                        else /* Error already set, free test config structure. */
                            tstDevCfgDestroy(pDevTstCfg);
                    }
                    else
                        rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: Failed to allocate %zu bytes for the test config structure", cbTestCfg);
                }
                else
                    rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: \"DeviceTests\" is empty");
            }
            else
                rc = tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: \"DeviceTests\" is not an array");

            RTJsonValueRelease(hJsonValDeviceTests);
        }
        else
            tstDevCfgErrorRc(pErrInfo, rc, "tstDevCfg/JSON: Failed to query \"DeviceTests\" value");

        RTJsonValueRelease(hJsonRoot);
    }

    return rc;
}


DECLHIDDEN(void) tstDevCfgDestroy(PCTSTDEVCFG pDevTstCfg)
{
    RT_NOREF(pDevTstCfg);
}

