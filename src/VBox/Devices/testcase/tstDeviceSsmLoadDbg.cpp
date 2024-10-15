/* $Id: tstDeviceSsmLoadDbg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstDeviceSsmFuzz - SSM fuzzing testcase.
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
#include <iprt/mem.h>
#include <iprt/file.h>
#include <iprt/fuzz.h>
#include <iprt/time.h>
#include <iprt/string.h>

#include "tstDeviceBuiltin.h"
#include "tstDeviceCfg.h"
#include "tstDeviceInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


static PCTSTDEVCFGITEM tstDevSsmFuzzGetCfgItem(PCTSTDEVCFGITEM paCfg, uint32_t cCfgItems, const char *pszName)
{
    for (uint32_t i = 0; i < cCfgItems; i++)
    {
        if (!RTStrCmp(paCfg[i].pszKey, pszName))
            return &paCfg[i];
    }

    return NULL;
}


static const char *tstDevSsmFuzzGetCfgString(PCTSTDEVCFGITEM paCfg, uint32_t cCfgItems, const char *pszName)
{
    PCTSTDEVCFGITEM pCfgItem = tstDevSsmFuzzGetCfgItem(paCfg, cCfgItems, pszName);
    if (   pCfgItem
        && pCfgItem->enmType == TSTDEVCFGITEMTYPE_STRING)
        return pCfgItem->u.psz;

    return NULL;
}


static uint32_t tstDevSsmFuzzGetCfgU32(PCTSTDEVCFGITEM paCfg, uint32_t cCfgItems, const char *pszName)
{
    PCTSTDEVCFGITEM pCfgItem = tstDevSsmFuzzGetCfgItem(paCfg, cCfgItems, pszName);
    if (   pCfgItem
        && pCfgItem->enmType == TSTDEVCFGITEMTYPE_INTEGER)
        return (uint32_t)pCfgItem->u.i64;

    return 0;
}


/**
 * Entry point for the SSM load debug.
 *
 * @returns VBox status code.
 * @param   hDut                The device under test.
 * @param   paCfg               The testcase config.
 * @param   cCfgItems           Number of config items.
 */
static DECLCALLBACK(int) tstDevSsmLoadDbgEntry(TSTDEVDUT hDut, PCTSTDEVCFGITEM paCfg, uint32_t cCfgItems)
{
    int rc = VINF_SUCCESS;
    const char *pszSsmUnit = tstDevSsmFuzzGetCfgString(paCfg, cCfgItems, "SsmUnit");
    if (pszSsmUnit)
    {
        void *pvData = NULL;
        size_t cbData = 0;

        rc = RTFileReadAll(pszSsmUnit, &pvData, &cbData);
        if (RT_SUCCESS(rc))
        {
            /* Create a new SSM handle to use. */
            PSSMHANDLE pSsm = (PSSMHANDLE)RTMemAllocZ(sizeof(*pSsm));
            if (RT_LIKELY(pSsm))
            {
                pSsm->pDut          = hDut;
                pSsm->pbSavedState  = (uint8_t *)pvData;
                pSsm->cbSavedState  = cbData;
                pSsm->offDataBuffer = 0;
                pSsm->uCurUnitVer   = tstDevSsmFuzzGetCfgU32(paCfg, cCfgItems, "UnitVersion");
                pSsm->rc            = VINF_SUCCESS;


                /* Get the SSM handler from the device. */
                int rcDut = VINF_SUCCESS;
                PTSTDEVDUTSSM pSsmClbks = RTListGetFirst(&hDut->LstSsmHandlers, TSTDEVDUTSSM, NdSsm);
                if (pSsmClbks)
                {
                    /* Load preparations. */
                    if (pSsmClbks->pfnLoadPrep)
                        rcDut = pSsmClbks->pfnLoadPrep(hDut->pDevIns, pSsm);
                    if (RT_SUCCESS(rcDut))
                        rcDut = pSsmClbks->pfnLoadExec(hDut->pDevIns, pSsm, pSsm->uCurUnitVer, SSM_PASS_FINAL);
                }
                RTMemFree(pSsm);
            }
            else
                rc = VERR_NO_MEMORY;

            RTFileReadAllFree(pvData, cbData);
        }
    }
    else
        rc = VERR_NOT_FOUND;

    return rc;
}


const TSTDEVTESTCASEREG g_TestcaseSsmLoadDbg =
{
    /** szName */
    "SsmLoadDbg",
    /** pszDesc */
    "Load SSM states which fail to load in VBox for investigation",
    /** fFlags */
    0,
    /** pfnTestEntry */
    tstDevSsmLoadDbgEntry
};

