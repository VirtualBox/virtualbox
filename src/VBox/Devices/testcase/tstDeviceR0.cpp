/* $Id: tstDeviceR0.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstDevice - Test framework for PDM devices/drivers
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEFAULT /** @todo */
#undef IN_RING3
#undef IN_SUP_R3
//#undef CTX_SUFF
//#undef R0PTRTYPE
//#undef R3PTRTYPE
#define IN_RING0
#define IN_SUP_R0
#define LINUX_VERSION_CODE 0
#define KERNEL_VERSION(a,b,c) 1
//#define CTX_SUFF(a_Name) a_Name##R0
//#define R3PTRTYPE(a_R3Type)     RTHCUINTPTR
//#define R0PTRTYPE(a_R0Type)     a_R0Type
#include <VBox/types.h>

#include <VBox/sup.h>
#include <VBox/version.h>
#include <iprt/assert.h>
#include <iprt/getopt.h>
#include <iprt/log.h>
#include <iprt/list.h>
#include <iprt/mem.h>

#include "tstDeviceInternal.h"
#include "tstDeviceCfg.h"
#include "tstDeviceBuiltin.h"



/**
 * Create a new PDM device with default config.
 *
 * @returns VBox status code.
 * @param   pszName                 Name of the device to create.
 * @param   fRCEnabled              Flag whether RC support should be enabled for this device.
 * @param   pDut                    The device under test structure the created PDM device instance is exercised under.
 */
DECLHIDDEN(int) tstDevPdmDevR0R3Create(const char *pszName, bool fRCEnabled, PTSTDEVDUTINT pDut)
{
    int rc = VINF_SUCCESS;
    PCPDMDEVREGR0 pPdmDevR0 = NULL;
    PCTSTDEVPDMDEV pPdmDev = tstDevPdmDeviceFind(pszName, &pPdmDevR0);
    if (RT_LIKELY(pPdmDev))
    {
        uint32_t const cbRing0     = RT_ALIGN_32(RT_UOFFSETOF(PDMDEVINSR0, achInstanceData) + pPdmDevR0->cbInstanceCC,
                                                 HOST_PAGE_SIZE);
        uint32_t const cbRing3     = RT_ALIGN_32(RT_UOFFSETOF(PDMDEVINSR3, achInstanceData) + pPdmDev->pReg->cbInstanceCC,
                                                 fRCEnabled ? HOST_PAGE_SIZE : 64);
        uint32_t const cbRC        = fRCEnabled ? 0
                                   : RT_ALIGN_32(RT_UOFFSETOF(PDMDEVINSRC, achInstanceData) + pPdmDevR0->cbInstanceRC, 64);
        uint32_t const cbShared    = RT_ALIGN_32(pPdmDev->pReg->cbInstanceShared, 64);
        uint32_t const cbCritSect  = RT_ALIGN_32(sizeof(PDMCRITSECT), 64);
        uint32_t const cbMsixState = RT_ALIGN_32(pPdmDev->pReg->cMaxMsixVectors * 16 + (pPdmDev->pReg->cMaxMsixVectors + 7) / 8, _4K);
        uint32_t const cbPciDev    = RT_ALIGN_32(RT_UOFFSETOF_DYN(PDMPCIDEV, abMsixState[cbMsixState]), 64);
        uint32_t const cPciDevs    = RT_MIN(pPdmDev->pReg->cMaxPciDevices, 8);
        uint32_t const cbPciDevs   = cbPciDev * cPciDevs;
        uint32_t const cbTotal     = RT_ALIGN_32(cbRing0 + cbRing3 + cbRC + cbShared + cbCritSect + cbPciDevs, HOST_PAGE_SIZE);
        AssertLogRelMsgReturn(cbTotal <= PDM_MAX_DEVICE_INSTANCE_SIZE,
                              ("Instance of '%s' is too big: cbTotal=%u, max %u\n",
                               pPdmDev->pReg->szName, cbTotal, PDM_MAX_DEVICE_INSTANCE_SIZE),
                              VERR_OUT_OF_RANGE);

        PPDMDEVINSR0 pDevInsR0 = (PPDMDEVINSR0)RTMemAllocZ(cbTotal);
        PDMDEVINSR3 *pDevInsR3   = (PDMDEVINSR3 *)(((uint8_t *)pDevInsR0 + cbRing0));

        pDevInsR0->u32Version             = PDM_DEVINSR0_VERSION;
        pDevInsR0->iInstance              = 0;
        pDevInsR0->pHlpR0                 = &g_tstDevPdmDevHlpR0;
        pDevInsR0->Internal.s.pDut        = pDut;
        pDevInsR0->pvInstanceDataR0       = (uint8_t *)pDevInsR0 + cbRing0 + cbRing3 + cbRC;
        pDevInsR0->pvInstanceDataForR0    = &pDevInsR0->achInstanceData[0];
        pDevInsR0->pCritSectRoR0          = (PPDMCRITSECT)((uint8_t *)pDevInsR0->pvInstanceDataR0 + cbShared);
        pDevInsR0->pReg                   = pPdmDevR0;
        pDevInsR0->pDevInsForR3           = (RTHCUINTPTR)pDevInsR3;
        pDevInsR0->pDevInsForR3R0         = pDevInsR3;
        pDevInsR0->pvInstanceDataForR3R0  = &pDevInsR3->achInstanceData[0];
        pDevInsR0->cbPciDev               = cbPciDev;
        pDevInsR0->cPciDevs               = cPciDevs;
        for (uint32_t iPciDev = 0; iPciDev < cPciDevs; iPciDev++)
        {
            /* Note! PDMDevice.cpp has a copy of this code.  Keep in sync. */
            PPDMPCIDEV pPciDev = (PPDMPCIDEV)((uint8_t *)pDevInsR0->pCritSectRoR0 + cbCritSect + cbPciDev * iPciDev);
            if (iPciDev < RT_ELEMENTS(pDevInsR0->apPciDevs))
                pDevInsR0->apPciDevs[iPciDev] = pPciDev;
            pPciDev->cbConfig           = _4K;
            pPciDev->cbMsixState        = cbMsixState;
            pPciDev->idxSubDev          = (uint16_t)iPciDev;
            //pPciDev->Int.s.idxSubDev    = (uint16_t)iPciDev;
            pPciDev->u32Magic           = PDMPCIDEV_MAGIC;
        }

        pDevInsR3->u32Version               = PDM_DEVINSR3_VERSION;
        pDevInsR3->iInstance                = 0;
        pDevInsR3->cbRing3                  = cbRing3;
        pDevInsR3->fR0Enabled               = true;
        pDevInsR3->fRCEnabled               = fRCEnabled;
        pDevInsR3->pvInstanceDataR3     = pDevInsR0->pDevInsForR3 + cbRing3 + cbRC;
        pDevInsR3->pvInstanceDataForR3  = pDevInsR0->pDevInsForR3 + RT_UOFFSETOF(PDMDEVINSR3, achInstanceData);
        pDevInsR3->pCritSectRoR3        = pDevInsR0->pDevInsForR3 + cbRing3 + cbRC + cbShared;
        pDevInsR3->pDevInsR0RemoveMe    = pDevInsR0;
        pDevInsR3->pvInstanceDataR0     = pDevInsR0->pvInstanceDataR0;
        pDevInsR3->pvInstanceDataRC     = fRCEnabled ? NIL_RTRGPTR : pDevInsR0->pDevInsForRC + RT_UOFFSETOF(PDMDEVINSRC, achInstanceData);
        pDevInsR3->pDevInsForRC         = pDevInsR0->pDevInsForRC;
        pDevInsR3->pDevInsForRCR3       = pDevInsR0->pDevInsForR3 + cbRing3;
        pDevInsR3->pDevInsForRCR3       = pDevInsR3->pDevInsForRCR3 + RT_UOFFSETOF(PDMDEVINSRC, achInstanceData);
        pDevInsR3->cbPciDev             = cbPciDev;
        pDevInsR3->cPciDevs             = cPciDevs;
        for (uint32_t i = 0; i < RT_MIN(cPciDevs, RT_ELEMENTS(pDevInsR3->apPciDevs)); i++)
            pDevInsR3->apPciDevs[i] = pDevInsR3->pCritSectRoR3 + cbCritSect + cbPciDev * i;
        RTCritSectInit(&pDevInsR0->pCritSectRoR0->s.CritSect);
        pDut->pDevIns   = pDevInsR3;
        pDut->pDevInsR0 = pDevInsR0;

        rc = tstDevPdmDeviceR3Construct(pDut);
        if (RT_SUCCESS(rc))
        {
            rc = pPdmDevR0->pfnConstruct(pDevInsR0);
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;
        }

        if (RT_FAILURE(rc))
        {
            //rc = pPdmDev->pReg->pfnDestruct(pDevIns);
            RTMemFree(pDevInsR0);
        }
    }
    else
        rc = VERR_NOT_FOUND;

    return rc;
}
