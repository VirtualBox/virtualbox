/* $Id: tstDevicePdmDevHlp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstDevice - Test framework for PDM devices/drivers, PDM helper implementation.
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
#include <VBox/types.h>
#include <VBox/version.h>
#include <VBox/vmm/pdmpci.h>

#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/rand.h>

#include "tstDeviceInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/* Temporarily until the stubs got implemented. */
#define VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS 1

/** @def PDMDEV_ASSERT_DEVINS
 * Asserts the validity of the device instance.
 */
#ifdef VBOX_STRICT
# define PDMDEV_ASSERT_DEVINS(pDevIns)   \
    do { \
        AssertPtr(pDevIns); \
        Assert(pDevIns->u32Version == PDM_DEVINS_VERSION); \
        Assert(pDevIns->CTX_SUFF(pvInstanceDataFor) == (void *)&pDevIns->achInstanceData[0]); \
    } while (0)
#else
# define PDMDEV_ASSERT_DEVINS(pDevIns)   do { } while (0)
#endif


/** Frequency of the real clock. */
#define TMCLOCK_FREQ_REAL       UINT32_C(1000)
/** Frequency of the virtual clock. */
#define TMCLOCK_FREQ_VIRTUAL    UINT32_C(1000000000)


/** Start structure magic. (Isaac Asimov) */
#define SSMR3STRUCT_BEGIN                       UINT32_C(0x19200102)
/** End structure magic. (Isaac Asimov) */
#define SSMR3STRUCT_END                         UINT32_C(0x19920406)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Resolves a path reference to a configuration item.
 *
 * @returns VBox status code.
 * @param   paDevCfg        The array of config items.
 * @param   cCfgItems       Number of config items in the array.
 * @param   pszName         Name of a byte string value.
 * @param   ppItem          Where to store the pointer to the item.
 */
static int tstDev_CfgmR3ResolveItem(PCTSTDEVCFGITEM paDevCfg, uint32_t cCfgItems, const char *pszName, PCTSTDEVCFGITEM *ppItem)
{
    *ppItem = NULL;
    if (!paDevCfg)
        return VERR_CFGM_VALUE_NOT_FOUND;

    size_t          cchName = strlen(pszName);
    PCTSTDEVCFGITEM pDevCfgItem = paDevCfg;

    for (uint32_t i = 0; i < cCfgItems; i++)
    {
        size_t cchKey = strlen(pDevCfgItem->pszKey);
        if (cchName == cchKey)
        {
            int iDiff = memcmp(pszName, pDevCfgItem->pszKey, cchName);
            if (iDiff <= 0)
            {
                if (iDiff != 0)
                    break;
                *ppItem = pDevCfgItem;
                return VINF_SUCCESS;
            }
        }

        /* next */
        pDevCfgItem++;
    }
    return VERR_CFGM_VALUE_NOT_FOUND;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnIoPortCreateEx} */
static DECLCALLBACK(int) pdmR3DevHlp_IoPortCreateEx(PPDMDEVINS pDevIns, RTIOPORT cPorts, uint32_t fFlags, PPDMPCIDEV pPciDev,
                                                    uint32_t iPciRegion, PFNIOMIOPORTNEWOUT pfnOut, PFNIOMIOPORTNEWIN pfnIn,
                                                    PFNIOMIOPORTNEWOUTSTRING pfnOutStr, PFNIOMIOPORTNEWINSTRING pfnInStr, RTR3PTR pvUser,
                                                    const char *pszDesc, PCIOMIOPORTDESC paExtDescs, PIOMIOPORTHANDLE phIoPorts)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_IoPortCreateEx: caller='%s'/%d: cPorts=%#x fFlags=%#x pPciDev=%p iPciRegion=%#x pfnOut=%p pfnIn=%p pfnOutStr=%p pfnInStr=%p pvUser=%p pszDesc=%p:{%s} paExtDescs=%p phIoPorts=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cPorts, fFlags, pPciDev, iPciRegion, pfnOut, pfnIn, pfnOutStr, pfnInStr,
             pvUser, pszDesc, pszDesc, paExtDescs, phIoPorts));

    /** @todo Verify there is no overlapping. */

    RT_NOREF(pszDesc);
    int rc = VINF_SUCCESS;
    PRTDEVDUTIOPORT pIoPort = (PRTDEVDUTIOPORT)RTMemAllocZ(sizeof(RTDEVDUTIOPORT));
    if (RT_LIKELY(pIoPort))
    {
        pIoPort->cPorts      = cPorts;
        pIoPort->pvUserR3    = pvUser;
        pIoPort->pfnOutR3    = pfnOut;
        pIoPort->pfnInR3     = pfnIn;
        pIoPort->pfnOutStrR3 = pfnOutStr;
        pIoPort->pfnInStrR3  = pfnInStr;
        RTListAppend(&pDevIns->Internal.s.pDut->LstIoPorts, &pIoPort->NdIoPorts);
        *phIoPorts = (IOMIOPORTHANDLE)pIoPort;
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlow(("pdmR3DevHlp_IoPortCreateEx: caller='%s'/%d: returns %Rrc (*phIoPorts=%#x)\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc, *phIoPorts));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnIoPortMap} */
static DECLCALLBACK(int) pdmR3DevHlp_IoPortMap(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts, RTIOPORT Port)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_IoPortMap: caller='%s'/%d: hIoPorts=%#x Port=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, hIoPorts, Port));

    PRTDEVDUTIOPORT pIoPort = (PRTDEVDUTIOPORT)hIoPorts;
    pIoPort->PortStart = Port;

    LogFlow(("pdmR3DevHlp_IoPortMap: caller='%s'/%d: returns VINF_SUCCESS\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnIoPortUnmap} */
static DECLCALLBACK(int) pdmR3DevHlp_IoPortUnmap(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_IoPortMap: caller='%s'/%d: hIoPorts=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, hIoPorts));

    PRTDEVDUTIOPORT pIoPort = (PRTDEVDUTIOPORT)hIoPorts;
    pIoPort->PortStart = 0;
    int rc = VINF_SUCCESS;

    LogFlow(("pdmR3DevHlp_IoPortMap: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnIoPortGetMappingAddress} */
static DECLCALLBACK(uint32_t) pdmR3DevHlp_IoPortGetMappingAddress(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_IoPortGetMappingAddress: caller='%s'/%d: hIoPorts=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, hIoPorts));

    PRTDEVDUTIOPORT pIoPort = (PRTDEVDUTIOPORT)hIoPorts;
    uint32_t uAddress = pIoPort->PortStart;

    LogFlow(("pdmR3DevHlp_IoPortGetMappingAddress: caller='%s'/%d: returns %#RX32\n", pDevIns->pReg->szName, pDevIns->iInstance, uAddress));
    return uAddress;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnIoPortWrite} */
static DECLCALLBACK(VBOXSTRICTRC) pdmR3DevHlp_IoPortWrite(PPDMDEVINS pDevIns, RTIOPORT Port, uint32_t u32Value, size_t cbValue)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_IoPortWrite: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    RT_NOREF(Port, u32Value, cbValue);
    VBOXSTRICTRC rcStrict = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_IoPortWrite: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmioCreateEx} */
static DECLCALLBACK(int) pdmR3DevHlp_MmioCreateEx(PPDMDEVINS pDevIns, RTGCPHYS cbRegion,
                                                  uint32_t fFlags, PPDMPCIDEV pPciDev, uint32_t iPciRegion,
                                                  PFNIOMMMIONEWWRITE pfnWrite, PFNIOMMMIONEWREAD pfnRead, PFNIOMMMIONEWFILL pfnFill,
                                                  void *pvUser, const char *pszDesc, PIOMMMIOHANDLE phRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MmioCreateEx: caller='%s'/%d: cbRegion=%#RGp fFlags=%#x pPciDev=%p iPciRegion=%#x pfnWrite=%p pfnRead=%p pfnFill=%p pvUser=%p pszDesc=%p:{%s} phRegion=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cbRegion, fFlags, pPciDev, iPciRegion, pfnWrite, pfnRead, pfnFill, pvUser, pszDesc, pszDesc, phRegion));

    /** @todo Verify there is no overlapping. */

    RT_NOREF(pszDesc);
    int rc = VINF_SUCCESS;
    PRTDEVDUTMMIO pMmio = (PRTDEVDUTMMIO)RTMemAllocZ(sizeof(*pMmio));
    if (RT_LIKELY(pMmio))
    {
        pMmio->cbRegion    = cbRegion;
        pMmio->pvUserR3    = pvUser;
        pMmio->pfnWriteR3  = pfnWrite;
        pMmio->pfnReadR3   = pfnRead;
        pMmio->pfnFillR3   = pfnFill;
        RTListAppend(&pDevIns->Internal.s.pDut->LstMmio, &pMmio->NdMmio);
        *phRegion = (IOMMMIOHANDLE)pMmio;
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlow(("pdmR3DevHlp_MmioCreateEx: caller='%s'/%d: returns %Rrc (*phRegion=%#x)\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc, *phRegion));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmioMap} */
static DECLCALLBACK(int) pdmR3DevHlp_MmioMap(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, RTGCPHYS GCPhys)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MmioMap: caller='%s'/%d: hRegion=%#x GCPhys=%#RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion, GCPhys));

    int rc = VINF_SUCCESS;
    PRTDEVDUTMMIO pMmio = (PRTDEVDUTMMIO)hRegion;
    pMmio->GCPhysStart = GCPhys;

    LogFlow(("pdmR3DevHlp_MmioMap: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmioUnmap} */
static DECLCALLBACK(int) pdmR3DevHlp_MmioUnmap(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MmioUnmap: caller='%s'/%d: hRegion=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion));

    int rc = VINF_SUCCESS;
    PRTDEVDUTMMIO pMmio = (PRTDEVDUTMMIO)hRegion;
    pMmio->GCPhysStart = NIL_RTGCPHYS;

    LogFlow(("pdmR3DevHlp_MmioUnmap: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmioReduce} */
static DECLCALLBACK(int) pdmR3DevHlp_MmioReduce(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, RTGCPHYS cbRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MmioReduce: caller='%s'/%d: hRegion=%#x cbRegion=%#RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion, cbRegion));

    int rc = VINF_SUCCESS;
    PRTDEVDUTMMIO pMmio = (PRTDEVDUTMMIO)hRegion;
    pMmio->cbRegion = cbRegion;

    LogFlow(("pdmR3DevHlp_MmioReduce: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmioGetMappingAddress} */
static DECLCALLBACK(RTGCPHYS) pdmR3DevHlp_MmioGetMappingAddress(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MmioGetMappingAddress: caller='%s'/%d: hRegion=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion));

    PRTDEVDUTMMIO pMmio = (PRTDEVDUTMMIO)hRegion;
    RTGCPHYS GCPhys = pMmio->GCPhysStart;

    LogFlow(("pdmR3DevHlp_MmioGetMappingAddress: caller='%s'/%d: returns %RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, GCPhys));
    return GCPhys;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmio2Create} */
static DECLCALLBACK(int) pdmR3DevHlp_Mmio2Create(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iPciRegion, RTGCPHYS cbRegion,
                                                 uint32_t fFlags, const char *pszDesc, void **ppvMapping, PPGMMMIO2HANDLE phRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_Mmio2Create: caller='%s'/%d: pPciDev=%p (%#x) iPciRegion=%#x cbRegion=%#RGp fFlags=%RX32 pszDesc=%p:{%s} ppvMapping=%p phRegion=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPciDev, pPciDev ? pPciDev->uDevFn : UINT32_MAX, iPciRegion, cbRegion,
             fFlags, pszDesc, pszDesc, ppvMapping, phRegion));

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
    *phRegion   = 0;
    *ppvMapping = RTMemAllocZ(cbRegion);
    if (!*ppvMapping)
        rc = VERR_NO_MEMORY;
#endif

    LogFlow(("pdmR3DevHlp_Mmio2Create: caller='%s'/%d: returns %Rrc *ppvMapping=%p phRegion=%#RX64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc, *ppvMapping, *phRegion));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmio2Destroy} */
static DECLCALLBACK(int) pdmR3DevHlp_Mmio2Destroy(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_Mmio2Destroy: caller='%s'/%d: hRegion=%#RX64\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_Mmio2Destroy: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmio2Map} */
static DECLCALLBACK(int) pdmR3DevHlp_Mmio2Map(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion, RTGCPHYS GCPhys)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_Mmio2Map: caller='%s'/%d: hRegion=%#RX64 GCPhys=%RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion, GCPhys));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_Mmio2Map: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmio2Unmap} */
static DECLCALLBACK(int) pdmR3DevHlp_Mmio2Unmap(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_Mmio2Unmap: caller='%s'/%d: hRegion=%#RX64\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_Mmio2Unmap: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmio2Reduce} */
static DECLCALLBACK(int) pdmR3DevHlp_Mmio2Reduce(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion, RTGCPHYS cbRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_Mmio2Reduce: caller='%s'/%d: hRegion=%#RX64 cbRegion=%RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion, cbRegion));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_Mmio2Reduce: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmio2GetMappingAddress} */
static DECLCALLBACK(RTGCPHYS) pdmR3DevHlp_Mmio2GetMappingAddress(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_Mmio2GetMappingAddress: caller='%s'/%d: hRegion=%#RX6r\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion));

    RTGCPHYS GCPhys = NIL_RTGCPHYS;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_Mmio2GetMappingAddress: caller='%s'/%d: returns %RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, GCPhys));
    return GCPhys;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmio2QueryAndResetDirtyBitmap} */
static DECLCALLBACK(int) pdmR3DevHlp_Mmio2QueryAndResetDirtyBitmap(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion,
                                                                   void *pvBitmap, size_t cbBitmap)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    //PVM pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_Mmio2QueryAndResetDirtyBitmap: caller='%s'/%d: hRegion=%#RX64 pvBitmap=%p cbBitmap=%#zx\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hRegion, pvBitmap, cbBitmap));

    int rc = VERR_NOT_IMPLEMENTED;

    LogFlow(("pdmR3DevHlp_Mmio2QueryAndResetDirtyBitmap: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmio2ControlDirtyPageTracking} */
static DECLCALLBACK(int) pdmR3DevHlp_Mmio2ControlDirtyPageTracking(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion, bool fEnabled)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    //PVM pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_Mmio2ControlDirtyPageTracking: caller='%s'/%d: hRegion=%#RX64 fEnabled=%RTbool\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hRegion, fEnabled));

    int rc = VERR_NOT_IMPLEMENTED;

    LogFlow(("pdmR3DevHlp_Mmio2ControlDirtyPageTracking: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/**
 * @copydoc PDMDEVHLPR3::pfnMmio2ChangeRegionNo
 */
static DECLCALLBACK(int) pdmR3DevHlp_Mmio2ChangeRegionNo(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion, uint32_t iNewRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_Mmio2ChangeRegionNo: caller='%s'/%d: hRegion=%#RX6r iNewRegion=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion, iNewRegion));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_Mmio2ChangeRegionNo: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmioMapMmio2Page} */
static DECLCALLBACK(int) pdmR3DevHlp_MmioMapMmio2Page(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, RTGCPHYS offRegion,
                                                      uint64_t hMmio2, RTGCPHYS offMmio2, uint64_t fPageFlags)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MmioMapMmio2Page: caller='%s'/%d: hRegion=%RX64 offRegion=%RGp hMmio2=%RX64 offMmio2=%RGp fPageFlags=%RX64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hRegion, offRegion, hMmio2, offMmio2, fPageFlags));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    Log(("pdmR3DevHlp_MmioMapMmio2Page: caller='%s'/%d: returns %Rrc\n",
         pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmioResetRegion} */
static DECLCALLBACK(int) pdmR3DevHlp_MmioResetRegion(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MmioResetRegion: caller='%s'/%d: hRegion=%RX64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hRegion));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    Log(("pdmR3DevHlp_MmioResetRegion: caller='%s'/%d: returns %Rrc\n",
         pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnROMRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_ROMRegister(PPDMDEVINS pDevIns, RTGCPHYS GCPhysStart, uint32_t cbRange,
                                                 const void *pvBinary, uint32_t cbBinary, uint32_t fFlags, const char *pszDesc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_ROMRegister: caller='%s'/%d: GCPhysStart=%RGp cbRange=%#x pvBinary=%p cbBinary=%#x fFlags=%#RX32 pszDesc=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPhysStart, cbRange, pvBinary, cbBinary, fFlags, pszDesc, pszDesc));

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    LogFlow(("pdmR3DevHlp_ROMRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnROMProtectShadow} */
static DECLCALLBACK(int) pdmR3DevHlp_ROMProtectShadow(PPDMDEVINS pDevIns, RTGCPHYS GCPhysStart, uint32_t cbRange, PGMROMPROT enmProt)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_ROMProtectShadow: caller='%s'/%d: GCPhysStart=%RGp cbRange=%#x enmProt=%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPhysStart, cbRange, enmProt));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_ROMProtectShadow: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMRegister(PPDMDEVINS pDevIns, uint32_t uVersion, size_t cbGuess, const char *pszBefore,
                                                 PFNSSMDEVLIVEPREP pfnLivePrep, PFNSSMDEVLIVEEXEC pfnLiveExec, PFNSSMDEVLIVEVOTE pfnLiveVote,
                                                 PFNSSMDEVSAVEPREP pfnSavePrep, PFNSSMDEVSAVEEXEC pfnSaveExec, PFNSSMDEVSAVEDONE pfnSaveDone,
                                                 PFNSSMDEVLOADPREP pfnLoadPrep, PFNSSMDEVLOADEXEC pfnLoadExec, PFNSSMDEVLOADDONE pfnLoadDone)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SSMRegister: caller='%s'/%d: uVersion=%#x cbGuess=%#x pszBefore=%p:{%s}\n"
             "    pfnLivePrep=%p pfnLiveExec=%p pfnLiveVote=%p pfnSavePrep=%p pfnSaveExec=%p pfnSaveDone=%p pszLoadPrep=%p pfnLoadExec=%p pfnLoadDone=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, uVersion, cbGuess, pszBefore, pszBefore,
             pfnLivePrep, pfnLiveExec, pfnLiveVote,
             pfnSavePrep, pfnSaveExec, pfnSaveDone,
             pfnLoadPrep, pfnLoadExec, pfnLoadDone));

    RT_NOREF(cbGuess, pszBefore);
    int rc = VINF_SUCCESS;
    PTSTDEVDUTSSM pSsm = (PTSTDEVDUTSSM)RTMemAllocZ(sizeof(*pSsm));
    if (RT_LIKELY(pSsm))
    {
        pSsm->uVersion      = uVersion;
        pSsm->pfnLivePrep   = pfnLivePrep;
        pSsm->pfnLiveExec   = pfnLiveExec;
        pSsm->pfnLiveVote   = pfnLiveVote;
        pSsm->pfnSavePrep   = pfnSavePrep;
        pSsm->pfnSaveExec   = pfnSaveExec;
        pSsm->pfnSaveDone   = pfnSaveDone;
        pSsm->pfnLoadPrep   = pfnLoadPrep;
        pSsm->pfnLoadExec   = pfnLoadExec;
        pSsm->pfnLoadDone   = pfnLoadDone;
        RTListAppend(&pDevIns->Internal.s.pDut->LstSsmHandlers, &pSsm->NdSsm);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlow(("pdmR3DevHlp_SSMRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSSMRegisterLegacy} */
static DECLCALLBACK(int) pdmR3DevHlp_SSMRegisterLegacy(PPDMDEVINS pDevIns, const char *pszOldName, PFNSSMDEVLOADPREP pfnLoadPrep,
                                                       PFNSSMDEVLOADEXEC pfnLoadExec, PFNSSMDEVLOADDONE pfnLoadDone)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SSMRegisterLegacy: caller='%s'/%d: pszOldName=%p:{%s} pfnLoadPrep=%p pfnLoadExec=%p pfnLoadDone=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pszOldName, pszOldName, pfnLoadPrep, pfnLoadExec, pfnLoadDone));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_SSMRegisterLegacy: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutStruct(PSSMHANDLE pSSM, const void *pvStruct, PCSSMFIELD paFields)
{
    RT_NOREF(pSSM, pvStruct, paFields);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutStructEx(PSSMHANDLE pSSM, const void *pvStruct, size_t cbStruct, uint32_t fFlags, PCSSMFIELD paFields, void *pvUser)
{
    RT_NOREF(pSSM, pvStruct, cbStruct, fFlags, paFields, pvUser);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutBool(PSSMHANDLE pSSM, bool fBool)
{
    RT_NOREF(pSSM, fBool);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutU8(PSSMHANDLE pSSM, uint8_t u8)
{
    RT_NOREF(pSSM, u8);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutS8(PSSMHANDLE pSSM, int8_t i8)
{
    RT_NOREF(pSSM, i8);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutU16(PSSMHANDLE pSSM, uint16_t u16)
{
    RT_NOREF(pSSM, u16);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutS16(PSSMHANDLE pSSM, int16_t i16)
{
    RT_NOREF(pSSM, i16);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutU32(PSSMHANDLE pSSM, uint32_t u32)
{
    RT_NOREF(pSSM, u32);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutS32(PSSMHANDLE pSSM, int32_t i32)
{
    RT_NOREF(pSSM, i32);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutU64(PSSMHANDLE pSSM, uint64_t u64)
{
    RT_NOREF(pSSM, u64);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutS64(PSSMHANDLE pSSM, int64_t i64)
{
    RT_NOREF(pSSM, i64);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutU128(PSSMHANDLE pSSM, uint128_t u128)
{
    RT_NOREF(pSSM, u128);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutS128(PSSMHANDLE pSSM, int128_t i128)
{
    RT_NOREF(pSSM, i128);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutUInt(PSSMHANDLE pSSM, RTUINT u)
{
    RT_NOREF(pSSM, u);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutSInt(PSSMHANDLE pSSM, RTINT i)
{
    RT_NOREF(pSSM, i);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutGCUInt(PSSMHANDLE pSSM, RTGCUINT u)
{
    RT_NOREF(pSSM, u);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutGCUIntReg(PSSMHANDLE pSSM, RTGCUINTREG u)
{
    RT_NOREF(pSSM, u);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutGCPhys32(PSSMHANDLE pSSM, RTGCPHYS32 GCPhys)
{
    RT_NOREF(pSSM, GCPhys);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutGCPhys64(PSSMHANDLE pSSM, RTGCPHYS64 GCPhys)
{
    RT_NOREF(pSSM, GCPhys);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutGCPhys(PSSMHANDLE pSSM, RTGCPHYS GCPhys)
{
    RT_NOREF(pSSM, GCPhys);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutGCPtr(PSSMHANDLE pSSM, RTGCPTR GCPtr)
{
    RT_NOREF(pSSM, GCPtr);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutGCUIntPtr(PSSMHANDLE pSSM, RTGCUINTPTR GCPtr)
{
    RT_NOREF(pSSM, GCPtr);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutRCPtr(PSSMHANDLE pSSM, RTRCPTR RCPtr)
{
    RT_NOREF(pSSM, RCPtr);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutIOPort(PSSMHANDLE pSSM, RTIOPORT IOPort)
{
    RT_NOREF(pSSM, IOPort);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutSel(PSSMHANDLE pSSM, RTSEL Sel)
{
    RT_NOREF(pSSM, Sel);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutMem(PSSMHANDLE pSSM, const void *pv, size_t cb)
{
    RT_NOREF(pSSM, pv, cb);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMPutStrZ(PSSMHANDLE pSSM, const char *psz)
{
    RT_NOREF(pSSM, psz);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Gets the host bit count of the saved state.
 *
 * Works for on both save and load handles.
 *
 * @returns 32 or 64.
 * @param   pSSM                The saved state handle.
 */
DECLINLINE(uint32_t) ssmR3GetHostBits(PSSMHANDLE pSSM)
{
    /** @todo Don't care about 32bit saved states for now (VBox is 64bit only as of 6.0). */
    RT_NOREF(pSSM);
    return HC_ARCH_BITS;
}


/**
 * Saved state origins on a host using 32-bit MSC?
 *
 * Works for on both save and load handles.
 *
 * @returns true/false.
 * @param   pSSM                The saved state handle.
 */
DECLINLINE(bool) ssmR3IsHostMsc32(PSSMHANDLE pSSM)
{
    /** @todo Don't care about 32bit saved states for now (VBox is 64bit only as of 6.0). */
    RT_NOREF(pSSM);
    return false;
}


/**
 * Inlined worker that handles format checks and buffered reads.
 *
 * @param   pSSM            The saved state handle.
 * @param   pvBuf           Where to store the read data.
 * @param   cbBuf           Number of bytes to read.
 */
DECLINLINE(int) tstDevSsmR3DataRead(PSSMHANDLE pSSM, void *pvBuf, size_t cbBuf)
{
    /*
     * Fend off previous errors and V1 data units.
     */
    if (RT_SUCCESS(pSSM->rc))
    {
        /** @todo Don't care about version 1 saved states (long obsolete). */
        uint32_t off = pSSM->offDataBuffer;
        if (   cbBuf <= pSSM->cbSavedState
            && pSSM->cbSavedState - cbBuf >= off)
        {
            memcpy(pvBuf, &pSSM->pbSavedState[off], cbBuf);
            pSSM->offDataBuffer = off + (uint32_t)cbBuf;
            return VINF_SUCCESS;
        }
        else
            pSSM->rc = VERR_BUFFER_OVERFLOW;
    }
    return pSSM->rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetBool(PSSMHANDLE pSSM, bool *pfBool)
{
    uint8_t u8; /* see SSMR3PutBool */
    int rc = tstDevSsmR3DataRead(pSSM, &u8, sizeof(u8));
    if (RT_SUCCESS(rc))
    {
        Assert(u8 <= 1);
        *pfBool = RT_BOOL(u8);
    }
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetBoolV(PSSMHANDLE pSSM, bool volatile *pfBool)
{
    uint8_t u8; /* see SSMR3PutBool */
    int rc = tstDevSsmR3DataRead(pSSM, &u8, sizeof(u8));
    if (RT_SUCCESS(rc))
    {
        Assert(u8 <= 1);
        *pfBool = RT_BOOL(u8);
    }
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetU8(PSSMHANDLE pSSM, uint8_t *pu8)
{
    return tstDevSsmR3DataRead(pSSM, pu8, sizeof(*pu8));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetU8V(PSSMHANDLE pSSM, uint8_t volatile *pu8)
{
    return tstDevSsmR3DataRead(pSSM, (void *)pu8, sizeof(*pu8));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetS8(PSSMHANDLE pSSM, int8_t *pi8)
{
    return tstDevSsmR3DataRead(pSSM, pi8, sizeof(*pi8));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetS8V(PSSMHANDLE pSSM, int8_t volatile *pi8)
{
    return tstDevSsmR3DataRead(pSSM, (void *)pi8, sizeof(*pi8));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetU16(PSSMHANDLE pSSM, uint16_t *pu16)
{
    return tstDevSsmR3DataRead(pSSM, pu16, sizeof(*pu16));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetU16V(PSSMHANDLE pSSM, uint16_t volatile *pu16)
{
    return tstDevSsmR3DataRead(pSSM, (void *)pu16, sizeof(*pu16));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetS16(PSSMHANDLE pSSM, int16_t *pi16)
{
    return tstDevSsmR3DataRead(pSSM, pi16, sizeof(*pi16));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetS16V(PSSMHANDLE pSSM, int16_t volatile *pi16)
{
    return tstDevSsmR3DataRead(pSSM, (void *)pi16, sizeof(*pi16));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetU32(PSSMHANDLE pSSM, uint32_t *pu32)
{
    return tstDevSsmR3DataRead(pSSM, pu32, sizeof(*pu32));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetU32V(PSSMHANDLE pSSM, uint32_t volatile *pu32)
{
    return tstDevSsmR3DataRead(pSSM, (void *)pu32, sizeof(*pu32));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetS32(PSSMHANDLE pSSM, int32_t *pi32)
{
    return tstDevSsmR3DataRead(pSSM, pi32, sizeof(*pi32));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetS32V(PSSMHANDLE pSSM, int32_t volatile *pi32)
{
    return tstDevSsmR3DataRead(pSSM, (void *)pi32, sizeof(*pi32));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetU64(PSSMHANDLE pSSM, uint64_t *pu64)
{
    return tstDevSsmR3DataRead(pSSM, pu64, sizeof(*pu64));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetU64V(PSSMHANDLE pSSM, uint64_t volatile *pu64)
{
    return tstDevSsmR3DataRead(pSSM, (void *)pu64, sizeof(*pu64));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetS64(PSSMHANDLE pSSM, int64_t *pi64)
{
    return tstDevSsmR3DataRead(pSSM, pi64, sizeof(*pi64));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetS64V(PSSMHANDLE pSSM, int64_t volatile *pi64)
{
    return tstDevSsmR3DataRead(pSSM, (void *)pi64, sizeof(*pi64));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetU128(PSSMHANDLE pSSM, uint128_t *pu128)
{
    return tstDevSsmR3DataRead(pSSM, pu128, sizeof(*pu128));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetU128V(PSSMHANDLE pSSM, uint128_t volatile *pu128)
{
    return tstDevSsmR3DataRead(pSSM, (void *)pu128, sizeof(*pu128));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetS128(PSSMHANDLE pSSM, int128_t *pi128)
{
    return tstDevSsmR3DataRead(pSSM, pi128, sizeof(*pi128));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetS128V(PSSMHANDLE pSSM, int128_t  volatile *pi128)
{
    return tstDevSsmR3DataRead(pSSM, (void *)pi128, sizeof(*pi128));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetGCPhys32(PSSMHANDLE pSSM, PRTGCPHYS32 pGCPhys)
{
    return tstDevSsmR3DataRead(pSSM, pGCPhys, sizeof(*pGCPhys));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetGCPhys32V(PSSMHANDLE pSSM, RTGCPHYS32 volatile *pGCPhys)
{
    return tstDevSsmR3DataRead(pSSM, (void *)pGCPhys, sizeof(*pGCPhys));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetGCPhys64(PSSMHANDLE pSSM, PRTGCPHYS64 pGCPhys)
{
    return tstDevSsmR3DataRead(pSSM, pGCPhys, sizeof(*pGCPhys));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetGCPhys64V(PSSMHANDLE pSSM, RTGCPHYS64 volatile *pGCPhys)
{
    return tstDevSsmR3DataRead(pSSM, (void *)pGCPhys, sizeof(*pGCPhys));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetGCPhys(PSSMHANDLE pSSM, PRTGCPHYS pGCPhys)
{
    /*
     * Default size?
     */
    if (RT_LIKELY(/*sizeof(*pGCPhys) == pSSM->u.Read.cbGCPhys*/true))
        return tstDevSsmR3DataRead(pSSM, pGCPhys, sizeof(*pGCPhys));

#if 0 /** @todo Later if necessary (only very old saved states). */
    /*
     * Fiddly.
     */
    Assert(sizeof(*pGCPhys)      == sizeof(uint64_t) || sizeof(*pGCPhys)      == sizeof(uint32_t));
    Assert(pSSM->u.Read.cbGCPhys == sizeof(uint64_t) || pSSM->u.Read.cbGCPhys == sizeof(uint32_t));
    if (pSSM->u.Read.cbGCPhys == sizeof(uint64_t))
    {
        /* 64-bit saved, 32-bit load: try truncate it. */
        uint64_t u64;
        int rc = tstDevSsmR3DataRead(pSSM, &u64, sizeof(uint64_t));
        if (RT_FAILURE(rc))
            return rc;
        if (u64 >= _4G)
            return VERR_SSM_GCPHYS_OVERFLOW;
        *pGCPhys = (RTGCPHYS)u64;
        return rc;
    }

    /* 32-bit saved, 64-bit load: clear the high part. */
    *pGCPhys = 0;
    return tstDevSsmR3DataRead(pSSM, pGCPhys, sizeof(uint32_t));
#endif
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetGCPhysV(PSSMHANDLE pSSM, RTGCPHYS volatile *pGCPhys)
{
    return pdmR3DevHlp_SSMGetGCPhys(pSSM, (PRTGCPHYS)pGCPhys);
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetUInt(PSSMHANDLE pSSM, PRTUINT pu)
{
    return tstDevSsmR3DataRead(pSSM, pu, sizeof(*pu));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetSInt(PSSMHANDLE pSSM, PRTINT pi)
{
    return tstDevSsmR3DataRead(pSSM, pi, sizeof(*pi));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetGCPtr(PSSMHANDLE pSSM, PRTGCPTR pGCPtr)
{
    return tstDevSsmR3DataRead(pSSM, pGCPtr, sizeof(*pGCPtr));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetGCUInt(PSSMHANDLE pSSM, PRTGCUINT pu)
{
    return pdmR3DevHlp_SSMGetGCPtr(pSSM, (PRTGCPTR)pu);
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetGCUIntReg(PSSMHANDLE pSSM, PRTGCUINTREG pu)
{
    AssertCompile(sizeof(RTGCPTR) == sizeof(*pu));
    return pdmR3DevHlp_SSMGetGCPtr(pSSM, (PRTGCPTR)pu);
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetGCUIntPtr(PSSMHANDLE pSSM, PRTGCUINTPTR pGCPtr)
{
    return pdmR3DevHlp_SSMGetGCPtr(pSSM, (PRTGCPTR)pGCPtr);
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetRCPtr(PSSMHANDLE pSSM, PRTRCPTR pRCPtr)
{
    return tstDevSsmR3DataRead(pSSM, pRCPtr, sizeof(*pRCPtr));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetIOPort(PSSMHANDLE pSSM, PRTIOPORT pIOPort)
{
    return tstDevSsmR3DataRead(pSSM, pIOPort, sizeof(*pIOPort));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetSel(PSSMHANDLE pSSM, PRTSEL pSel)
{
    return tstDevSsmR3DataRead(pSSM, pSel, sizeof(*pSel));
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetMem(PSSMHANDLE pSSM, void *pv, size_t cb)
{
    return tstDevSsmR3DataRead(pSSM, pv, cb);
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetStrZEx(PSSMHANDLE pSSM, char *psz, size_t cbMax, size_t *pcbStr)
{
    /* read size prefix. */
    uint32_t u32;
    int rc = pdmR3DevHlp_SSMGetU32(pSSM, &u32);
    if (RT_SUCCESS(rc))
    {
        if (pcbStr)
            *pcbStr = u32;
        if (u32 < cbMax)
        {
            /* terminate and read string content. */
            psz[u32] = '\0';
            return tstDevSsmR3DataRead(pSSM, psz, u32);
        }
        return VERR_TOO_MUCH_DATA;
    }
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetStrZ(PSSMHANDLE pSSM, char *psz, size_t cbMax)
{
    return pdmR3DevHlp_SSMGetStrZEx(pSSM, psz, cbMax, NULL);
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMSkip(PSSMHANDLE pSSM, size_t cb)
{
    while (cb > 0)
    {
        uint8_t abBuf[8192];
        size_t  cbCur = RT_MIN(sizeof(abBuf), cb);
        cb -= cbCur;
        int rc = tstDevSsmR3DataRead(pSSM, abBuf, cbCur);
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetStruct(PSSMHANDLE pSSM, void *pvStruct, PCSSMFIELD paFields)
{
    AssertPtr(pvStruct);
    AssertPtr(paFields);

    /* begin marker. */
    uint32_t u32Magic;
    int rc = pdmR3DevHlp_SSMGetU32(pSSM, &u32Magic);
    if (RT_FAILURE(rc))
        return rc;
    AssertMsgReturn(u32Magic == SSMR3STRUCT_BEGIN, ("u32Magic=%#RX32\n", u32Magic), pSSM->rc = VERR_SSM_STRUCTURE_MAGIC);

    /* get the fields */
    for (PCSSMFIELD pCur = paFields;
         pCur->cb != UINT32_MAX && pCur->off != UINT32_MAX;
         pCur++)
    {
        if (pCur->uFirstVer <= pSSM->uCurUnitVer)
        {
            uint8_t *pbField = (uint8_t *)pvStruct + pCur->off;
            switch ((uintptr_t)pCur->pfnGetPutOrTransformer)
            {
                case SSMFIELDTRANS_NO_TRANSFORMATION:
                    rc = tstDevSsmR3DataRead(pSSM, pbField, pCur->cb);
                    break;

                case SSMFIELDTRANS_GCPTR:
                    AssertMsgBreakStmt(pCur->cb == sizeof(RTGCPTR), ("%#x (%s)\n", pCur->cb, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = pdmR3DevHlp_SSMGetGCPtr(pSSM, (PRTGCPTR)pbField);
                    break;

                case SSMFIELDTRANS_GCPHYS:
                    AssertMsgBreakStmt(pCur->cb == sizeof(RTGCPHYS), ("%#x (%s)\n", pCur->cb, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = pdmR3DevHlp_SSMGetGCPhys(pSSM, (PRTGCPHYS)pbField);
                    break;

                case SSMFIELDTRANS_RCPTR:
                    AssertMsgBreakStmt(pCur->cb == sizeof(RTRCPTR), ("%#x (%s)\n", pCur->cb, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = pdmR3DevHlp_SSMGetRCPtr(pSSM, (PRTRCPTR)pbField);
                    break;

                case SSMFIELDTRANS_RCPTR_ARRAY:
                {
                    uint32_t const cEntries = pCur->cb / sizeof(RTRCPTR);
                    AssertMsgBreakStmt(pCur->cb == cEntries * sizeof(RTRCPTR) && cEntries, ("%#x (%s)\n", pCur->cb, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = VINF_SUCCESS;
                    for (uint32_t i = 0; i < cEntries && RT_SUCCESS(rc); i++)
                        rc = pdmR3DevHlp_SSMGetRCPtr(pSSM, &((PRTRCPTR)pbField)[i]);
                    break;
                }

                default:
                    AssertMsgFailedBreakStmt(("%#x\n", pCur->pfnGetPutOrTransformer), rc = VERR_SSM_FIELD_COMPLEX);
            }
            if (RT_FAILURE(rc))
            {
                if (RT_SUCCESS(pSSM->rc))
                    pSSM->rc = rc;
                return rc;
            }
        }
    }

    /* end marker */
    rc = pdmR3DevHlp_SSMGetU32(pSSM, &u32Magic);
    if (RT_FAILURE(rc))
        return rc;
    AssertMsgReturn(u32Magic == SSMR3STRUCT_END, ("u32Magic=%#RX32\n", u32Magic), pSSM->rc = VERR_SSM_STRUCTURE_MAGIC);
    return rc;
}


/**
 * SSMR3GetStructEx helper that gets a HCPTR that is used as a NULL indicator.
 *
 * @returns VBox status code.
 *
 * @param   pSSM            The saved state handle.
 * @param   ppv             Where to return the value (0/1).
 * @param   fFlags          SSMSTRUCT_FLAGS_XXX.
 */
DECLINLINE(int) ssmR3GetHCPtrNI(PSSMHANDLE pSSM, void **ppv, uint32_t fFlags)
{
    uintptr_t uPtrNI;
    if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
    {
        if (ssmR3GetHostBits(pSSM) == 64)
        {
            uint64_t u;
            int rc = tstDevSsmR3DataRead(pSSM, &u, sizeof(u));
            if (RT_FAILURE(rc))
                return rc;
            uPtrNI = u ? 1 : 0;
        }
        else
        {
            uint32_t u;
            int rc = tstDevSsmR3DataRead(pSSM, &u, sizeof(u));
            if (RT_FAILURE(rc))
                return rc;
            uPtrNI = u ? 1 : 0;
        }
    }
    else
    {
        bool f;
        int rc = pdmR3DevHlp_SSMGetBool(pSSM, &f);
        if (RT_FAILURE(rc))
            return rc;
        uPtrNI = f ? 1 : 0;
    }
    *ppv = (void *)uPtrNI;
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMGetStructEx(PSSMHANDLE pSSM, void *pvStruct, size_t cbStruct, uint32_t fFlags, PCSSMFIELD paFields, void *pvUser)
{
    int         rc;
    uint32_t    u32Magic;

    /*
     * Validation.
     */
    AssertMsgReturn(!(fFlags & ~SSMSTRUCT_FLAGS_VALID_MASK), ("%#x\n", fFlags), pSSM->rc = VERR_INVALID_PARAMETER);
    AssertPtr(pvStruct);
    AssertPtr(paFields);

    /*
     * Begin marker.
     */
    if (!(fFlags & (SSMSTRUCT_FLAGS_NO_MARKERS | SSMSTRUCT_FLAGS_NO_LEAD_MARKER)))
    {
        rc = pdmR3DevHlp_SSMGetU32(pSSM, &u32Magic);
        if (RT_FAILURE(rc))
            return rc;
        AssertMsgReturn(u32Magic == SSMR3STRUCT_BEGIN, ("u32Magic=%#RX32\n", u32Magic), pSSM->rc = VERR_SSM_STRUCTURE_MAGIC);
    }

    /*
     * Put the fields
     */
    rc = VINF_SUCCESS;
    uint32_t off = 0;
    for (PCSSMFIELD pCur = paFields;
         pCur->cb != UINT32_MAX && pCur->off != UINT32_MAX;
         pCur++)
    {
        uint32_t const offField =    (!SSMFIELDTRANS_IS_PADDING(pCur->pfnGetPutOrTransformer) || pCur->off != UINT32_MAX / 2)
                                  &&  !SSMFIELDTRANS_IS_OLD(pCur->pfnGetPutOrTransformer)
                                ? pCur->off
                                : off;
        uint32_t const cbField  = SSMFIELDTRANS_IS_OLD(pCur->pfnGetPutOrTransformer)
                                ? 0
                                : SSMFIELDTRANS_IS_PADDING(pCur->pfnGetPutOrTransformer)
                                ? RT_HIWORD(pCur->cb)
                                : pCur->cb;
        AssertMsgReturn(   cbField            <= cbStruct
                        && offField + cbField <= cbStruct
                        && offField + cbField >= offField,
                        ("off=%#x cb=%#x cbStruct=%#x (%s)\n", cbField, offField, cbStruct, pCur->pszName),
                        pSSM->rc = VERR_SSM_FIELD_OUT_OF_BOUNDS);
        AssertMsgReturn(    !(fFlags & SSMSTRUCT_FLAGS_FULL_STRUCT)
                        || off == offField,
                        ("off=%#x offField=%#x (%s)\n", off, offField, pCur->pszName),
                        pSSM->rc = VERR_SSM_FIELD_NOT_CONSECUTIVE);

        if (pCur->uFirstVer <= pSSM->uCurUnitVer)
        {
            rc = VINF_SUCCESS;
            uint8_t       *pbField  = (uint8_t *)pvStruct + offField;
            switch ((uintptr_t)pCur->pfnGetPutOrTransformer)
            {
                case SSMFIELDTRANS_NO_TRANSFORMATION:
                    rc = tstDevSsmR3DataRead(pSSM, pbField, cbField);
                    break;

                case SSMFIELDTRANS_GCPHYS:
                    AssertMsgBreakStmt(cbField == sizeof(RTGCPHYS), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = pdmR3DevHlp_SSMGetGCPhys(pSSM, (PRTGCPHYS)pbField);
                    break;

                case SSMFIELDTRANS_GCPTR:
                    AssertMsgBreakStmt(cbField == sizeof(RTGCPTR), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = pdmR3DevHlp_SSMGetGCPtr(pSSM, (PRTGCPTR)pbField);
                    break;

                case SSMFIELDTRANS_RCPTR:
                    AssertMsgBreakStmt(cbField == sizeof(RTRCPTR), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = pdmR3DevHlp_SSMGetRCPtr(pSSM, (PRTRCPTR)pbField);
                    break;

                case SSMFIELDTRANS_RCPTR_ARRAY:
                {
                    uint32_t const cEntries = cbField / sizeof(RTRCPTR);
                    AssertMsgBreakStmt(cbField == cEntries * sizeof(RTRCPTR) && cEntries, ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = VINF_SUCCESS;
                    for (uint32_t i = 0; i < cEntries && RT_SUCCESS(rc); i++)
                        rc = pdmR3DevHlp_SSMGetRCPtr(pSSM, &((PRTRCPTR)pbField)[i]);
                    break;
                }

                case SSMFIELDTRANS_HCPTR_NI:
                    AssertMsgBreakStmt(cbField == sizeof(void *), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = ssmR3GetHCPtrNI(pSSM, (void **)pbField, fFlags);
                    break;

                case SSMFIELDTRANS_HCPTR_NI_ARRAY:
                {
                    uint32_t const cEntries = cbField / sizeof(void *);
                    AssertMsgBreakStmt(cbField == cEntries * sizeof(void *) && cEntries, ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = VINF_SUCCESS;
                    for (uint32_t i = 0; i < cEntries && RT_SUCCESS(rc); i++)
                        rc = ssmR3GetHCPtrNI(pSSM, &((void **)pbField)[i], fFlags);
                    break;
                }

                case SSMFIELDTRANS_HCPTR_HACK_U32:
                    AssertMsgBreakStmt(cbField == sizeof(void *), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    *(uintptr_t *)pbField = 0;
                    rc = tstDevSsmR3DataRead(pSSM, pbField, sizeof(uint32_t));
                    if ((fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE) && ssmR3GetHostBits(pSSM) == 64)
                    {
                        uint32_t u32;
                        rc = tstDevSsmR3DataRead(pSSM, &u32, sizeof(uint32_t));
                        AssertMsgBreakStmt(RT_FAILURE(rc) || u32 == 0 || (fFlags & SSMSTRUCT_FLAGS_SAVED_AS_MEM),
                                           ("high=%#x low=%#x (%s)\n", u32, *(uint32_t *)pbField, pCur->pszName),
                                           rc = VERR_SSM_FIELD_INVALID_VALUE);
                    }
                    break;

                case SSMFIELDTRANS_U32_ZX_U64:
                    AssertMsgBreakStmt(cbField == sizeof(uint64_t), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    ((uint32_t *)pbField)[1] = 0;
                    rc = pdmR3DevHlp_SSMGetU32(pSSM, (uint32_t *)pbField);
                    break;


                case SSMFIELDTRANS_IGNORE:
                    if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                        rc = pdmR3DevHlp_SSMSkip(pSSM, cbField);
                    break;

                case SSMFIELDTRANS_IGN_GCPHYS:
                    AssertMsgBreakStmt(cbField == sizeof(RTGCPHYS), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                        rc = pdmR3DevHlp_SSMSkip(pSSM, sizeof(RTGCPHYS));
                    break;

                case SSMFIELDTRANS_IGN_GCPTR:
                    AssertMsgBreakStmt(cbField == sizeof(RTGCPTR), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                        rc = pdmR3DevHlp_SSMSkip(pSSM, sizeof(RTGCPTR));
                    break;

                case SSMFIELDTRANS_IGN_RCPTR:
                    AssertMsgBreakStmt(cbField == sizeof(RTRCPTR), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                        rc = pdmR3DevHlp_SSMSkip(pSSM, sizeof(RTRCPTR));
                    break;

                case SSMFIELDTRANS_IGN_HCPTR:
                    AssertMsgBreakStmt(cbField == sizeof(void *), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                        rc = pdmR3DevHlp_SSMSkip(pSSM, ssmR3GetHostBits(pSSM) / 8);
                    break;


                case SSMFIELDTRANS_OLD:
                    AssertMsgBreakStmt(pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = pdmR3DevHlp_SSMSkip(pSSM, pCur->cb);
                    break;

                case SSMFIELDTRANS_OLD_GCPHYS:
                    AssertMsgBreakStmt(pCur->cb == sizeof(RTGCPHYS) && pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = pdmR3DevHlp_SSMSkip(pSSM, sizeof(RTGCPHYS));
                    break;

                case SSMFIELDTRANS_OLD_GCPTR:
                    AssertMsgBreakStmt(pCur->cb == sizeof(RTGCPTR) && pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = pdmR3DevHlp_SSMSkip(pSSM, sizeof(RTGCPTR));
                    break;

                case SSMFIELDTRANS_OLD_RCPTR:
                    AssertMsgBreakStmt(pCur->cb == sizeof(RTRCPTR) && pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = pdmR3DevHlp_SSMSkip(pSSM, sizeof(RTRCPTR));
                    break;

                case SSMFIELDTRANS_OLD_HCPTR:
                    AssertMsgBreakStmt(pCur->cb == sizeof(void *) && pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = pdmR3DevHlp_SSMSkip(pSSM, ssmR3GetHostBits(pSSM) / 8);
                    break;

                case SSMFIELDTRANS_OLD_PAD_HC:
                    AssertMsgBreakStmt(pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = pdmR3DevHlp_SSMSkip(pSSM, ssmR3GetHostBits(pSSM) == 64 ? RT_HIWORD(pCur->cb) : RT_LOWORD(pCur->cb));
                    break;

                case SSMFIELDTRANS_OLD_PAD_MSC32:
                    AssertMsgBreakStmt(pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    if (ssmR3IsHostMsc32(pSSM))
                        rc = pdmR3DevHlp_SSMSkip(pSSM, pCur->cb);
                    break;


                case SSMFIELDTRANS_PAD_HC:
                case SSMFIELDTRANS_PAD_HC32:
                case SSMFIELDTRANS_PAD_HC64:
                case SSMFIELDTRANS_PAD_HC_AUTO:
                case SSMFIELDTRANS_PAD_MSC32_AUTO:
                {
                    uint32_t cb32    = RT_BYTE1(pCur->cb);
                    uint32_t cb64    = RT_BYTE2(pCur->cb);
                    uint32_t cbCtx   =    HC_ARCH_BITS == 64
                                       || (uintptr_t)pCur->pfnGetPutOrTransformer == SSMFIELDTRANS_PAD_MSC32_AUTO
                                     ? cb64 : cb32;
                    uint32_t cbSaved =    ssmR3GetHostBits(pSSM) == 64
                                       || (   (uintptr_t)pCur->pfnGetPutOrTransformer == SSMFIELDTRANS_PAD_MSC32_AUTO
                                           && !ssmR3IsHostMsc32(pSSM))
                                     ? cb64 : cb32;
                    AssertMsgBreakStmt(    cbField == cbCtx
                                       &&  (   (   pCur->off == UINT32_MAX / 2
                                                && (   cbField == 0
                                                    || (uintptr_t)pCur->pfnGetPutOrTransformer == SSMFIELDTRANS_PAD_HC_AUTO
                                                    || (uintptr_t)pCur->pfnGetPutOrTransformer == SSMFIELDTRANS_PAD_MSC32_AUTO
                                                   )
                                               )
                                            || (pCur->off != UINT32_MAX / 2 && cbField != 0)
                                            )
                                       , ("cbField=%#x cb32=%#x cb64=%#x HC_ARCH_BITS=%u cbCtx=%#x cbSaved=%#x off=%#x\n",
                                          cbField, cb32, cb64, HC_ARCH_BITS, cbCtx, cbSaved, pCur->off),
                                       rc = VERR_SSM_FIELD_INVALID_PADDING_SIZE);
                    if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                        rc = pdmR3DevHlp_SSMSkip(pSSM, cbSaved);
                    break;
                }

                default:
                    AssertBreakStmt(pCur->pfnGetPutOrTransformer, rc = VERR_SSM_FIELD_INVALID_CALLBACK);
                    rc = pCur->pfnGetPutOrTransformer(pSSM, pCur, pvStruct, fFlags, true /*fGetOrPut*/, pvUser);
                    break;
            }
            if (RT_FAILURE(rc))
                break;
        }

        off = offField + cbField;
    }

    if (RT_SUCCESS(rc))
        AssertMsgStmt(   !(fFlags & SSMSTRUCT_FLAGS_FULL_STRUCT)
                      || off == cbStruct,
                      ("off=%#x cbStruct=%#x\n", off, cbStruct),
                      rc = VERR_SSM_FIELD_NOT_CONSECUTIVE);

    if (RT_FAILURE(rc))
    {
        if (RT_SUCCESS(pSSM->rc))
            pSSM->rc = rc;
        return rc;
    }

    /*
     * End marker
     */
    if (!(fFlags & (SSMSTRUCT_FLAGS_NO_MARKERS | SSMSTRUCT_FLAGS_NO_TAIL_MARKER)))
    {
        rc = pdmR3DevHlp_SSMGetU32(pSSM, &u32Magic);
        if (RT_FAILURE(rc))
            return rc;
        AssertMsgReturn(u32Magic == SSMR3STRUCT_END, ("u32Magic=%#RX32\n", u32Magic), pSSM->rc = VERR_SSM_STRUCTURE_MAGIC);
    }

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMSkipToEndOfUnit(PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMSetLoadError(PSSMHANDLE pSSM, int rc, RT_SRC_POS_DECL, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(6, 7)
{
    RT_NOREF(pSSM, rc, RT_SRC_POS_ARGS, pszFormat);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMSetLoadErrorV(PSSMHANDLE pSSM, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(6, 0)
{
    RT_NOREF(pSSM, rc, RT_SRC_POS_ARGS, pszFormat, va);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMSetCfgError(PSSMHANDLE pSSM, RT_SRC_POS_DECL, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(5, 6)
{
    RT_NOREF(pSSM, RT_SRC_POS_ARGS, pszFormat);
    pSSM->rc = VERR_SSM_LOAD_CONFIG_MISMATCH;
    return pSSM->rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMSetCfgErrorV(PSSMHANDLE pSSM, RT_SRC_POS_DECL, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(5, 0)
{
    RT_NOREF(pSSM, RT_SRC_POS_ARGS, pszFormat, va);
    pSSM->rc = VERR_SSM_LOAD_CONFIG_MISMATCH;
    return pSSM->rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_SSMHandleGetStatus(PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    return pSSM->rc;
}


static DECLCALLBACK(SSMAFTER) pdmR3DevHlp_SSMHandleGetAfter(PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    AssertFailed();
    return SSMAFTER_INVALID;
}


static DECLCALLBACK(bool) pdmR3DevHlp_SSMHandleIsLiveSave(PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    AssertFailed();
    return false;
}


static DECLCALLBACK(uint32_t) pdmR3DevHlp_SSMHandleMaxDowntime(PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    AssertFailed();
    return 0;
}


static DECLCALLBACK(uint32_t) pdmR3DevHlp_SSMHandleHostBits(PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    AssertFailed();
    return 0;
}


static DECLCALLBACK(uint32_t) pdmR3DevHlp_SSMHandleRevision(PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    AssertFailed();
    return 0;
}


static DECLCALLBACK(uint32_t) pdmR3DevHlp_SSMHandleVersion(PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    AssertFailed();
    return 0;
}


static DECLCALLBACK(const char *) pdmR3DevHlp_SSMHandleHostOSAndArch(PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    AssertFailed();
    return NULL;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerCreate} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerCreate(PPDMDEVINS pDevIns, TMCLOCK enmClock, PFNTMTIMERDEV pfnCallback,
                                                 void *pvUser, uint32_t fFlags, const char *pszDesc, PTMTIMERHANDLE phTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_TimerCreate: caller='%s'/%d: enmClock=%d pfnCallback=%p pvUser=%p fFlags=%#x pszDesc=%p:{%s} phTimer=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, enmClock, pfnCallback, pvUser, fFlags, pszDesc, pszDesc, phTimer));

    int rc = VINF_SUCCESS;
    PTMTIMERR3 pTimer = (PTMTIMERR3)RTMemAllocZ(sizeof(TMTIMER));
    if (RT_LIKELY(pTimer))
    {
        pTimer->enmClock       = enmClock;
        pTimer->pfnCallbackDev = pfnCallback;
        pTimer->pvUser         = pvUser;
        pTimer->fFlags         = fFlags;
        RTListAppend(&pDevIns->Internal.s.pDut->LstTimers, &pTimer->NdDevTimers);
        *phTimer = (TMTIMERHANDLE)pTimer;
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlow(("pdmR3DevHlp_TimerCreate: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerFromMicro} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TimerFromMicro(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMicroSecs)
{
    RT_NOREF(pDevIns, hTimer, cMicroSecs);
    AssertFailed();
    return 0;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerFromMilli} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TimerFromMilli(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMilliSecs)
{
    RT_NOREF(pDevIns, hTimer, cMilliSecs);
    AssertFailed();
    return 0;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerFromNano} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TimerFromNano(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cNanoSecs)
{
    RT_NOREF(pDevIns, hTimer, cNanoSecs);
    AssertFailed();
    return 0;
}

/** @interface_method_impl{PDMDEVHLPR3,pfnTimerGet} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TimerGet(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    RT_NOREF(pDevIns, hTimer);
#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    AssertFailed();
    return 0;
#else
    static uint64_t cCnt = 0;
    return cCnt++;
#endif
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerGetFreq} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TimerGetFreq(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    PTMTIMERR3 pTimer = (PTMTIMERR3)hTimer;
    switch (pTimer->enmClock)
    {
        case TMCLOCK_VIRTUAL:
        case TMCLOCK_VIRTUAL_SYNC:
            return TMCLOCK_FREQ_VIRTUAL;

        case TMCLOCK_REAL:
            return TMCLOCK_FREQ_REAL;

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pTimer->enmClock));
            return 0;
    }
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerGetNano} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TimerGetNano(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    RT_NOREF(pDevIns, hTimer);
    AssertFailed();
    return 0;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerIsActive} */
static DECLCALLBACK(bool) pdmR3DevHlp_TimerIsActive(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    RT_NOREF(pDevIns, hTimer);
#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    AssertFailed();
    return false;
#else
    return true;
#endif
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerIsLockOwner} */
static DECLCALLBACK(bool) pdmR3DevHlp_TimerIsLockOwner(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    RT_NOREF(pDevIns, hTimer);
    AssertFailed();
    return false;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerLockClock} */
static DECLCALLBACK(VBOXSTRICTRC) pdmR3DevHlp_TimerLockClock(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, int rcBusy)
{
    RT_NOREF(pDevIns, hTimer, rcBusy);

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerLockClock2} */
static DECLCALLBACK(VBOXSTRICTRC) pdmR3DevHlp_TimerLockClock2(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer,
                                                              PPDMCRITSECT pCritSect, int rcBusy)
{
    RT_NOREF(pDevIns, hTimer, pCritSect, rcBusy);

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerSet} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerSet(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t uExpire)
{
    RT_NOREF(pDevIns, hTimer, uExpire);

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerSetFrequencyHint} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerSetFrequencyHint(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint32_t uHz)
{
    RT_NOREF(pDevIns, hTimer, uHz);

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerSetMicro} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerSetMicro(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMicrosToNext)
{
    RT_NOREF(pDevIns, hTimer, cMicrosToNext);

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerSetMillies} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerSetMillies(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMilliesToNext)
{
    RT_NOREF(pDevIns, hTimer, cMilliesToNext);

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerSetNano} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerSetNano(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cNanosToNext)
{
    RT_NOREF(pDevIns, hTimer, cNanosToNext);

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerSetRelative} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerSetRelative(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cTicksToNext, uint64_t *pu64Now)
{
    RT_NOREF(pDevIns, hTimer, cTicksToNext, pu64Now);

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerStop} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerStop(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    RT_NOREF(pDevIns, hTimer);

#if 1 /** @todo */
    int rc = VINF_SUCCESS;
#else
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#endif

    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerUnlockClock} */
static DECLCALLBACK(void) pdmR3DevHlp_TimerUnlockClock(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    RT_NOREF(pDevIns, hTimer);
    AssertFailed();
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerUnlockClock2} */
static DECLCALLBACK(void) pdmR3DevHlp_TimerUnlockClock2(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect)
{
    RT_NOREF(pDevIns, hTimer, pCritSect);
    AssertFailed();
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerSetCritSect} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerSetCritSect(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    PTMTIMERR3 pTimer = (PTMTIMERR3)hTimer;
    pTimer->pCritSect = pCritSect;
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerSave} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerSave(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PSSMHANDLE pSSM)
{
    RT_NOREF(pDevIns, hTimer, pSSM);
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerLoad} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerLoad(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PSSMHANDLE pSSM)
{
    RT_NOREF(pDevIns, hTimer);

/** @name Saved state values (comes directly from TM.cpp)
 * @{ */
#define TMTIMERSTATE_SAVED_PENDING_STOP         4
#define TMTIMERSTATE_SAVED_PENDING_SCHEDULE     7
/** @}  */

    /*
     * Load the state and validate it.
     */
    uint8_t u8State;
    int rc = pdmR3DevHlp_SSMGetU8(pSSM, &u8State);
    if (RT_FAILURE(rc))
        return rc;

    /* TMTIMERSTATE_SAVED_XXX: Workaround for accidental state shift in r47786 (2009-05-26 19:12:12). */
    if (    u8State == TMTIMERSTATE_SAVED_PENDING_STOP + 1
        ||  u8State == TMTIMERSTATE_SAVED_PENDING_SCHEDULE + 1)
        u8State--;

    if (    u8State != TMTIMERSTATE_SAVED_PENDING_STOP
        &&  u8State != TMTIMERSTATE_SAVED_PENDING_SCHEDULE)
    {
        /*AssertLogRelMsgFailed(("u8State=%d\n", u8State));*/
        return VERR_TM_LOAD_STATE;
    }

    if (u8State == TMTIMERSTATE_SAVED_PENDING_SCHEDULE)
    {
        /*
         * Load the expire time.
         */
        uint64_t u64Expire;
        rc = pdmR3DevHlp_SSMGetU64(pSSM, &u64Expire);
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Set it.
         */
        Log(("u8State=%d u64Expire=%llu\n", u8State, u64Expire));
    }

    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerDestroy} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerDestroy(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    RT_NOREF(pDevIns, hTimer);
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_TimerSkipLoad(PSSMHANDLE pSSM, bool *pfActive)
{
    RT_NOREF(pSSM, pfActive);
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTMUtcNow} */
static DECLCALLBACK(PRTTIMESPEC) pdmR3DevHlp_TMUtcNow(PPDMDEVINS pDevIns, PRTTIMESPEC pTime)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_TMUtcNow: caller='%s'/%d: pTime=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pTime));

    RT_NOREF(pDevIns, pTime);
    AssertFailed();

    LogFlow(("pdmR3DevHlp_TMUtcNow: caller='%s'/%d: returns %RU64\n", pDevIns->pReg->szName, pDevIns->iInstance, RTTimeSpecGetNano(pTime)));
    return pTime;
}


static DECLCALLBACK(bool) pdmR3DevHlp_CFGMExists(PCFGMNODE pNode, const char *pszName)
{
    RT_NOREF(pNode, pszName);
    AssertFailed();
    return false;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryType(PCFGMNODE pNode, const char *pszName, PCFGMVALUETYPE penmType)
{
    RT_NOREF(pNode, pszName, penmType);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQuerySize(PCFGMNODE pNode, const char *pszName, size_t *pcb)
{
    if (!pNode)
        return VERR_CFGM_NO_PARENT;

    PCTSTDEVCFGITEM pCfgItem;
    int rc = tstDev_CfgmR3ResolveItem(pNode->pDut->pTest->paCfgItems, pNode->pDut->pTest->cCfgItems, pszName, &pCfgItem);
    if (RT_SUCCESS(rc))
    {
        switch (pCfgItem->enmType)
        {
            case TSTDEVCFGITEMTYPE_INTEGER:
                *pcb = sizeof(uint64_t);
                break;

            case TSTDEVCFGITEMTYPE_STRING:
                *pcb = strlen(pCfgItem->u.psz) + 1;
                break;

            case TSTDEVCFGITEMTYPE_BYTES:
                AssertFailed();
                break;

            default:
                rc = VERR_CFGM_IPE_1;
                AssertMsgFailed(("Invalid value type %d\n", pCfgItem->enmType));
                break;
        }
    }
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryInteger(PCFGMNODE pNode, const char *pszName, uint64_t *pu64)
{
    if (!pNode)
        return VERR_CFGM_NO_PARENT;

    PCTSTDEVCFGITEM pCfgItem;
    int rc = tstDev_CfgmR3ResolveItem(pNode->pDut->pTest->paCfgItems, pNode->pDut->pTest->cCfgItems, pszName, &pCfgItem);
    if (RT_SUCCESS(rc))
    {
        if (pCfgItem->enmType == TSTDEVCFGITEMTYPE_INTEGER)
            *pu64 = (uint64_t)pCfgItem->u.i64;
        else
            rc = VERR_CFGM_NOT_INTEGER;
    }

    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryIntegerDef(PCFGMNODE pNode, const char *pszName, uint64_t *pu64, uint64_t u64Def)
{
    int rc = pdmR3DevHlp_CFGMQueryInteger(pNode, pszName, pu64);
    if (RT_FAILURE(rc))
    {
        *pu64 = u64Def;
        if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
            rc = VINF_SUCCESS;
    }

    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryString(PCFGMNODE pNode, const char *pszName, char *pszString, size_t cchString)
{
    if (!pNode)
        return VERR_CFGM_NO_PARENT;

    PCTSTDEVCFGITEM pCfgItem;
    int rc = tstDev_CfgmR3ResolveItem(pNode->pDut->pTest->paCfgItems, pNode->pDut->pTest->cCfgItems, pszName, &pCfgItem);
    if (RT_SUCCESS(rc))
    {
        switch (pCfgItem->enmType)
        {
            case TSTDEVCFGITEMTYPE_STRING:
            {
                size_t cchVal = strlen(pCfgItem->u.psz);
                if (cchString <= cchVal + 1)
                    memcpy(pszString, pCfgItem->u.psz, cchVal);
                else
                    rc = VERR_CFGM_NOT_ENOUGH_SPACE;
                break;
            }
            case TSTDEVCFGITEMTYPE_INTEGER:
            case TSTDEVCFGITEMTYPE_BYTES:
            default:
                rc = VERR_CFGM_IPE_1;
                AssertMsgFailed(("Invalid value type %d\n", pCfgItem->enmType));
                break;
        }
    }
    else
        rc = VERR_CFGM_VALUE_NOT_FOUND;

    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryStringDef(PCFGMNODE pNode, const char *pszName, char *pszString, size_t cchString, const char *pszDef)
{
    int rc = pdmR3DevHlp_CFGMQueryString(pNode, pszName, pszString, cchString);
    if (RT_FAILURE(rc) && rc != VERR_CFGM_NOT_ENOUGH_SPACE)
    {
        size_t cchDef = strlen(pszDef);
        if (cchString > cchDef)
        {
            memcpy(pszString, pszDef, cchDef);
            memset(pszString + cchDef, 0, cchString - cchDef);
            if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
                rc = VINF_SUCCESS;
        }
        else if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
            rc = VERR_CFGM_NOT_ENOUGH_SPACE;
    }

    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryPassword(PCFGMNODE pNode, const char *pszName, char *pszString, size_t cchString)
{
    return pdmR3DevHlp_CFGMQueryString(pNode, pszName, pszString, cchString);
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryPasswordDef(PCFGMNODE pNode, const char *pszName, char *pszString, size_t cchString, const char *pszDef)
{
    return pdmR3DevHlp_CFGMQueryStringDef(pNode, pszName, pszString, cchString, pszDef);
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryBytes(PCFGMNODE pNode, const char *pszName, void *pvData, size_t cbData)
{
    RT_NOREF(pNode, pszName, pvData, cbData);

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
#else
    memset(pvData, 0, cbData);
    return VINF_SUCCESS;
#endif
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryU64(PCFGMNODE pNode, const char *pszName, uint64_t *pu64)
{
    return pdmR3DevHlp_CFGMQueryInteger(pNode, pszName, pu64);
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryU64Def(PCFGMNODE pNode, const char *pszName, uint64_t *pu64, uint64_t u64Def)
{
    return pdmR3DevHlp_CFGMQueryIntegerDef(pNode, pszName, pu64, u64Def);
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryS64(PCFGMNODE pNode, const char *pszName, int64_t *pi64)
{
    RT_NOREF(pNode, pszName, pi64);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryS64Def(PCFGMNODE pNode, const char *pszName, int64_t *pi64, int64_t i64Def)
{
    RT_NOREF(pNode, pszName, pi64, i64Def);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryU32(PCFGMNODE pNode, const char *pszName, uint32_t *pu32)
{
    uint64_t u64;
    int rc = pdmR3DevHlp_CFGMQueryInteger(pNode, pszName, &u64);
    if (RT_SUCCESS(rc))
    {
        if (!(u64 & UINT64_C(0xffffffff00000000)))
            *pu32 = (uint32_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryU32Def(PCFGMNODE pNode, const char *pszName, uint32_t *pu32, uint32_t u32Def)
{
    uint64_t u64;
    int rc = pdmR3DevHlp_CFGMQueryIntegerDef(pNode, pszName, &u64, u32Def);
    if (RT_SUCCESS(rc))
    {
        if (!(u64 & UINT64_C(0xffffffff00000000)))
            *pu32 = (uint32_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    if (RT_FAILURE(rc))
        *pu32 = u32Def;
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryS32(PCFGMNODE pNode, const char *pszName, int32_t *pi32)
{
    uint64_t u64;
    int rc = pdmR3DevHlp_CFGMQueryInteger(pNode, pszName, &u64);
    if (RT_SUCCESS(rc))
    {
        if (   !(u64 & UINT64_C(0xffffffff80000000))
            ||  (u64 & UINT64_C(0xffffffff80000000)) == UINT64_C(0xffffffff80000000))
            *pi32 = (int32_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryS32Def(PCFGMNODE pNode, const char *pszName, int32_t *pi32, int32_t i32Def)
{
    uint64_t u64;
    int rc = pdmR3DevHlp_CFGMQueryIntegerDef(pNode, pszName, &u64, i32Def);
    if (RT_SUCCESS(rc))
    {
        if (   !(u64 & UINT64_C(0xffffffff80000000))
            ||  (u64 & UINT64_C(0xffffffff80000000)) == UINT64_C(0xffffffff80000000))
            *pi32 = (int32_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    if (RT_FAILURE(rc))
        *pi32 = i32Def;
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryU16(PCFGMNODE pNode, const char *pszName, uint16_t *pu16)
{
    uint64_t u64;
    int rc = pdmR3DevHlp_CFGMQueryInteger(pNode, pszName, &u64);
    if (RT_SUCCESS(rc))
    {
        if (!(u64 & UINT64_C(0xffffffffffff0000)))
            *pu16 = (int16_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryU16Def(PCFGMNODE pNode, const char *pszName, uint16_t *pu16, uint16_t u16Def)
{
    uint64_t u64;
    int rc = pdmR3DevHlp_CFGMQueryIntegerDef(pNode, pszName, &u64, u16Def);
    if (RT_SUCCESS(rc))
    {
        if (!(u64 & UINT64_C(0xffffffffffff0000)))
            *pu16 = (int16_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    if (RT_FAILURE(rc))
        *pu16 = u16Def;
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryS16(PCFGMNODE pNode, const char *pszName, int16_t *pi16)
{
    RT_NOREF(pNode, pszName, pi16);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryS16Def(PCFGMNODE pNode, const char *pszName, int16_t *pi16, int16_t i16Def)
{
    RT_NOREF(pNode, pszName, pi16, i16Def);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryU8(PCFGMNODE pNode, const char *pszName, uint8_t *pu8)
{
    uint64_t u64;
    int rc = pdmR3DevHlp_CFGMQueryInteger(pNode, pszName, &u64);
    if (RT_SUCCESS(rc))
    {
        if (!(u64 & UINT64_C(0xffffffffffffff00)))
            *pu8 = (uint8_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryU8Def(PCFGMNODE pNode, const char *pszName, uint8_t *pu8, uint8_t u8Def)
{
    uint64_t u64;
    int rc = pdmR3DevHlp_CFGMQueryIntegerDef(pNode, pszName, &u64, u8Def);
    if (RT_SUCCESS(rc))
    {
        if (!(u64 & UINT64_C(0xffffffffffffff00)))
            *pu8 = (uint8_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    if (RT_FAILURE(rc))
        *pu8 = u8Def;
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryS8(PCFGMNODE pNode, const char *pszName, int8_t *pi8)
{
    RT_NOREF(pNode, pszName, pi8);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryS8Def(PCFGMNODE pNode, const char *pszName, int8_t *pi8, int8_t i8Def)
{
    RT_NOREF(pNode, pszName, pi8, i8Def);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryBool(PCFGMNODE pNode, const char *pszName, bool *pf)
{
    uint64_t u64;
    int rc = pdmR3DevHlp_CFGMQueryInteger(pNode, pszName, &u64);
    if (RT_SUCCESS(rc))
        *pf = u64 ? true : false;
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryBoolDef(PCFGMNODE pNode, const char *pszName, bool *pf, bool fDef)
{
    uint64_t u64;
    int rc = pdmR3DevHlp_CFGMQueryIntegerDef(pNode, pszName, &u64, fDef);
    *pf = u64 ? true : false;
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryPort(PCFGMNODE pNode, const char *pszName, PRTIOPORT pPort)
{
    RT_NOREF(pNode, pszName, pPort);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryPortDef(PCFGMNODE pNode, const char *pszName, PRTIOPORT pPort, RTIOPORT PortDef)
{
    AssertCompileSize(RTIOPORT, 2);
    return pdmR3DevHlp_CFGMQueryU16Def(pNode, pszName, pPort, PortDef);
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryUInt(PCFGMNODE pNode, const char *pszName, unsigned int *pu)
{
    RT_NOREF(pNode, pszName, pu);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryUIntDef(PCFGMNODE pNode, const char *pszName, unsigned int *pu, unsigned int uDef)
{
    RT_NOREF(pNode, pszName, pu, uDef);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQuerySInt(PCFGMNODE pNode, const char *pszName, signed int *pi)
{
    RT_NOREF(pNode, pszName, pi);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQuerySIntDef(PCFGMNODE pNode, const char *pszName, signed int *pi, signed int iDef)
{
    RT_NOREF(pNode, pszName, pi, iDef);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryPtr(PCFGMNODE pNode, const char *pszName, void **ppv)
{
    uint64_t u64;
    int rc = pdmR3DevHlp_CFGMQueryInteger(pNode, pszName, &u64);
    if (RT_SUCCESS(rc))
    {
        uintptr_t u = (uintptr_t)u64;
        if (u64 == u)
            *ppv = (void *)u;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryPtrDef(PCFGMNODE pNode, const char *pszName, void **ppv, void *pvDef)
{
    RT_NOREF(pNode, pszName, ppv, pvDef);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryGCPtr(PCFGMNODE pNode, const char *pszName, PRTGCPTR pGCPtr)
{
    RT_NOREF(pNode, pszName, pGCPtr);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryGCPtrDef(PCFGMNODE pNode, const char *pszName, PRTGCPTR pGCPtr, RTGCPTR GCPtrDef)
{
    RT_NOREF(pNode, pszName, pGCPtr, GCPtrDef);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryGCPtrU(PCFGMNODE pNode, const char *pszName, PRTGCUINTPTR pGCPtr)
{
    RT_NOREF(pNode, pszName, pGCPtr);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryGCPtrUDef(PCFGMNODE pNode, const char *pszName, PRTGCUINTPTR pGCPtr, RTGCUINTPTR GCPtrDef)
{
    RT_NOREF(pNode, pszName, pGCPtr, GCPtrDef);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryGCPtrS(PCFGMNODE pNode, const char *pszName, PRTGCINTPTR pGCPtr)
{
    RT_NOREF(pNode, pszName, pGCPtr);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryGCPtrSDef(PCFGMNODE pNode, const char *pszName, PRTGCINTPTR pGCPtr, RTGCINTPTR GCPtrDef)
{
    RT_NOREF(pNode, pszName, pGCPtr, GCPtrDef);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryStringAlloc(PCFGMNODE pNode, const char *pszName, char **ppszString)
{
    if (!pNode)
        return VERR_CFGM_NO_PARENT;

    PCTSTDEVCFGITEM pCfgItem;
    int rc = tstDev_CfgmR3ResolveItem(pNode->pDut->pTest->paCfgItems, pNode->pDut->pTest->cCfgItems, pszName, &pCfgItem);
    if (RT_SUCCESS(rc))
    {
        switch (pCfgItem->enmType)
        {
            case TSTDEVCFGITEMTYPE_STRING:
            {
                *ppszString = (char *)RTMemDup(pCfgItem->u.psz, strlen(pCfgItem->u.psz) + 1);
                if (RT_LIKELY(*ppszString))
                    rc = VINF_SUCCESS;
                else
                    rc = VERR_NO_STR_MEMORY;
                break;
            }
            case TSTDEVCFGITEMTYPE_INTEGER:
            case TSTDEVCFGITEMTYPE_BYTES:
            default:
                rc = VERR_CFGM_IPE_1;
                AssertMsgFailed(("Invalid value type %d\n", pCfgItem->enmType));
                break;
        }
    }
    else
        rc = VERR_CFGM_VALUE_NOT_FOUND;

    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMQueryStringAllocDef(PCFGMNODE pNode, const char *pszName, char **ppszString, const char *pszDef)
{
    if (!pNode)
        return VERR_CFGM_NO_PARENT;

    PCTSTDEVCFGITEM pCfgItem;
    int rc = tstDev_CfgmR3ResolveItem(pNode->pDut->pTest->paCfgItems, pNode->pDut->pTest->cCfgItems, pszName, &pCfgItem);
    if (RT_SUCCESS(rc))
    {
        switch (pCfgItem->enmType)
        {
            case TSTDEVCFGITEMTYPE_STRING:
            {
                *ppszString = (char *)RTMemDup(pCfgItem->u.psz, strlen(pCfgItem->u.psz) + 1);
                if (RT_LIKELY(*ppszString))
                    rc = VINF_SUCCESS;
                else
                    rc = VERR_NO_STR_MEMORY;
                break;
            }
            case TSTDEVCFGITEMTYPE_INTEGER:
            case TSTDEVCFGITEMTYPE_BYTES:
            default:
                rc = VERR_CFGM_IPE_1;
                AssertMsgFailed(("Invalid value type %d\n", pCfgItem->enmType));
                break;
        }
    }
    else
    {
        if (pszDef)
        {
            *ppszString = (char *)RTMemDup(pszDef, strlen(pszDef) + 1);
            if (RT_LIKELY(*ppszString))
                rc = VINF_SUCCESS;
            else
                rc = VERR_NO_STR_MEMORY;
        }
        else
        {
            *ppszString = NULL;
            rc = VINF_SUCCESS;
        }
    }

    return rc;
}


static DECLCALLBACK(PCFGMNODE) pdmR3DevHlp_CFGMGetParent(PCFGMNODE pNode)
{
    RT_NOREF(pNode);
    AssertFailed();
    return NULL;
}


static DECLCALLBACK(PCFGMNODE) pdmR3DevHlp_CFGMGetChild(PCFGMNODE pNode, const char *pszPath)
{
    RT_NOREF(pNode, pszPath);
    AssertFailed();
    return NULL;
}


static DECLCALLBACK(PCFGMNODE) pdmR3DevHlp_CFGMGetChildF(PCFGMNODE pNode, const char *pszPathFormat, ...) RT_IPRT_FORMAT_ATTR(2, 3)
{
    RT_NOREF(pNode, pszPathFormat);
    AssertFailed();
    return NULL;
}


static DECLCALLBACK(PCFGMNODE) pdmR3DevHlp_CFGMGetChildFV(PCFGMNODE pNode, const char *pszPathFormat, va_list Args) RT_IPRT_FORMAT_ATTR(3, 0)
{
    RT_NOREF(pNode, pszPathFormat, Args);
    AssertFailed();
    return NULL;
}


static DECLCALLBACK(PCFGMNODE) pdmR3DevHlp_CFGMGetFirstChild(PCFGMNODE pNode)
{
    RT_NOREF(pNode);
    AssertFailed();
    return NULL;
}


static DECLCALLBACK(PCFGMNODE) pdmR3DevHlp_CFGMGetNextChild(PCFGMNODE pCur)
{
    RT_NOREF(pCur);
    AssertFailed();
    return NULL;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMGetName(PCFGMNODE pCur, char *pszName, size_t cchName)
{
    RT_NOREF(pCur, pszName, cchName);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(size_t) pdmR3DevHlp_CFGMGetNameLen(PCFGMNODE pCur)
{
    RT_NOREF(pCur);
    AssertFailed();
    return 0;
}


static DECLCALLBACK(bool) pdmR3DevHlp_CFGMAreChildrenValid(PCFGMNODE pNode, const char *pszzValid)
{
    RT_NOREF(pNode, pszzValid);
    AssertFailed();
    return false;
}


static DECLCALLBACK(PCFGMLEAF) pdmR3DevHlp_CFGMGetFirstValue(PCFGMNODE pCur)
{
    RT_NOREF(pCur);
    AssertFailed();
    return NULL;
}


static DECLCALLBACK(PCFGMLEAF) pdmR3DevHlp_CFGMGetNextValue(PCFGMLEAF pCur)
{
    RT_NOREF(pCur);
    AssertFailed();
    return NULL;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMGetValueName(PCFGMLEAF pCur, char *pszName, size_t cchName)
{
    RT_NOREF(pCur, pszName, cchName);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(size_t) pdmR3DevHlp_CFGMGetValueNameLen(PCFGMLEAF pCur)
{
    RT_NOREF(pCur);
    AssertFailed();
    return 0;
}


static DECLCALLBACK(CFGMVALUETYPE) pdmR3DevHlp_CFGMGetValueType(PCFGMLEAF pCur)
{
    RT_NOREF(pCur);
    AssertFailed();
    return CFGMVALUETYPE_INTEGER;
}


static DECLCALLBACK(bool) pdmR3DevHlp_CFGMAreValuesValid(PCFGMNODE pNode, const char *pszzValid)
{
    if (pNode && pNode->pDut->pTest->paCfgItems)
    {
        PCTSTDEVCFGITEM pDevCfgItem = pNode->pDut->pTest->paCfgItems;
        for (uint32_t i = 0; i < pNode->pDut->pTest->cCfgItems; i++)
        {
            size_t cchKey = strlen(pDevCfgItem->pszKey);

            /* search pszzValid for the name */
            const char *psz = pszzValid;
            while (*psz)
            {
                size_t cch = strlen(psz);
                if (    cch == cchKey
                    &&  !memcmp(psz, pDevCfgItem->pszKey, cch))
                    break;

                /* next */
                psz += cch + 1;
            }

            /* if at end of pszzValid we didn't find it => failure */
            if (!*psz)
                return false;

            pDevCfgItem++;
        }
    }

    return true;
}


static DECLCALLBACK(int) pdmR3DevHlp_CFGMValidateConfig(PCFGMNODE pNode, const char *pszNode,
                                                        const char *pszValidValues, const char *pszValidNodes,
                                                        const char *pszWho, uint32_t uInstance)
{
    RT_NOREF(pNode, pszNode, pszValidValues, pszValidNodes, pszWho, uInstance);
#if 1
    return VINF_SUCCESS;
#else
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
#endif
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTMTimeVirtGet} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TMTimeVirtGet(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_TMTimeVirtGet: caller='%s'/%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    uint64_t u64Time = 0;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_TMTimeVirtGet: caller='%s'/%d: returns %RU64\n", pDevIns->pReg->szName, pDevIns->iInstance, u64Time));
    return u64Time;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTMTimeVirtGetFreq} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TMTimeVirtGetFreq(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_TMTimeVirtGetFreq: caller='%s'/%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    uint64_t u64Freq = 0;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_TMTimeVirtGetFreq: caller='%s'/%d: returns %RU64\n", pDevIns->pReg->szName, pDevIns->iInstance, u64Freq));
    return u64Freq;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTMTimeVirtGetNano} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TMTimeVirtGetNano(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_TMTimeVirtGetNano: caller='%s'/%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    uint64_t u64Nano = RTTimeNanoTS();

    LogFlow(("pdmR3DevHlp_TMTimeVirtGetNano: caller='%s'/%d: returns %RU64\n", pDevIns->pReg->szName, pDevIns->iInstance, u64Nano));
    return u64Nano;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTMCpuTicksPerSecond} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TMCpuTicksPerSecond(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_TMCpuTicksPerSecond: caller='%s'/%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    AssertFailed();
    uint64_t u64CpuTicksPerSec = 0;

    LogFlow(("pdmR3DevHlp_TMCpuTicksPerSecond: caller='%s'/%d: returns %RU64\n", pDevIns->pReg->szName, pDevIns->iInstance, u64CpuTicksPerSec));
    return u64CpuTicksPerSec;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetSupDrvSession} */
static DECLCALLBACK(PSUPDRVSESSION) pdmR3DevHlp_GetSupDrvSession(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_GetSupDrvSession: caller='%s'/%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    PSUPDRVSESSION pSession = NIL_RTR0PTR;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_GetSupDrvSession: caller='%s'/%d: returns %#p\n", pDevIns->pReg->szName, pDevIns->iInstance, pSession));
    return pSession;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnQueryGenericUserObject} */
static DECLCALLBACK(void *) pdmR3DevHlp_QueryGenericUserObject(PPDMDEVINS pDevIns, PCRTUUID pUuid)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_QueryGenericUserObject: caller='%s'/%d: pUuid=%p:%RTuuid\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pUuid, pUuid));

#if defined(DEBUG_bird) || defined(DEBUG_ramshankar) || defined(DEBUG_sunlover) || defined(DEBUG_michael) || defined(DEBUG_andy)
    AssertMsgFailed(("'%s' wants %RTuuid - external only interface!\n", pDevIns->pReg->szName, pUuid));
#endif

    void *pvRet = NULL;
    AssertFailed();

    LogRel(("pdmR3DevHlp_QueryGenericUserObject: caller='%s'/%d: returns %#p for %RTuuid\n",
            pDevIns->pReg->szName, pDevIns->iInstance, pvRet, pUuid));
    return pvRet;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPGMHandlerPhysicalTypeRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_PGMHandlerPhysicalTypeRegister(PPDMDEVINS pDevIns, PGMPHYSHANDLERKIND enmKind,
                                                                    R3PTRTYPE(PFNPGMPHYSHANDLER) pfnHandlerR3,
                                                                    const char *pszHandlerR0, const char *pszPfHandlerR0,
                                                                    const char *pszHandlerRC, const char *pszPfHandlerRC,
                                                                    const char *pszDesc, PPGMPHYSHANDLERTYPE phType)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(enmKind, pfnHandlerR3, pszHandlerR0, pszPfHandlerR0, pszHandlerRC, pszPfHandlerRC, pszDesc, phType);
    LogFlow(("pdmR3DevHlp_PGMHandlerPhysicalTypeRegister: caller='%s'/%d: enmKind=%d pfnHandlerR3=%p pszHandlerR0=%p:{%s} pszPfHandlerR0=%p:{%s} pszHandlerRC=%p:{%s} pszPfHandlerRC=%p:{%s} pszDesc=%p:{%s} phType=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pfnHandlerR3,
             pszHandlerR0, pszHandlerR0, pszPfHandlerR0, pszPfHandlerR0,
             pszHandlerRC, pszHandlerRC, pszPfHandlerRC, pszPfHandlerRC,
             pszDesc, pszDesc, phType));

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    LogRel(("pdmR3DevHlp_PGMHandlerPhysicalTypeRegister: caller='%s'/%d: returns %Rrc\n",
            pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysRead} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysRead(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead, uint32_t fFlags)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PhysRead: caller='%s'/%d: GCPhys=%RGp pvBuf=%p cbRead=%#x fFlags=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPhys, pvBuf, cbRead, fFlags));

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    VBOXSTRICTRC rcStrict = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    RTRandBytes(pvBuf, cbRead);
    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
#endif

    Log(("pdmR3DevHlp_PhysRead: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict) ));
    return VBOXSTRICTRC_VAL(rcStrict);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysWrite} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysWrite(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite, uint32_t fFlags)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PhysWrite: caller='%s'/%d: GCPhys=%RGp pvBuf=%p cbWrite=%#xfFlags=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPhys, pvBuf, cbWrite, fFlags));

    VBOXSTRICTRC rcStrict = VERR_NOT_IMPLEMENTED;;
    AssertFailed();

    Log(("pdmR3DevHlp_PhysWrite: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict) ));
    return VBOXSTRICTRC_VAL(rcStrict);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysGCPhys2CCPtr} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysGCPhys2CCPtr(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint32_t fFlags, void **ppv, PPGMPAGEMAPLOCK pLock)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PhysGCPhys2CCPtr: caller='%s'/%d: GCPhys=%RGp fFlags=%#x ppv=%p pLock=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPhys, fFlags, ppv, pLock));
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);

    int rc = VERR_NOT_IMPLEMENTED;;
    AssertFailed();

    Log(("pdmR3DevHlp_PhysGCPhys2CCPtr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysGCPhys2CCPtrReadOnly} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysGCPhys2CCPtrReadOnly(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint32_t fFlags, const void **ppv, PPGMPAGEMAPLOCK pLock)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PhysGCPhys2CCPtrReadOnly: caller='%s'/%d: GCPhys=%RGp fFlags=%#x ppv=%p pLock=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPhys, fFlags, ppv, pLock));
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);

    int rc = VERR_NOT_IMPLEMENTED;;
    AssertFailed();

    Log(("pdmR3DevHlp_PhysGCPhys2CCPtrReadOnly: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysReleasePageMappingLock} */
static DECLCALLBACK(void) pdmR3DevHlp_PhysReleasePageMappingLock(PPDMDEVINS pDevIns, PPGMPAGEMAPLOCK pLock)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PhysReleasePageMappingLock: caller='%s'/%d: pLock=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pLock));

    AssertFailed();

    Log(("pdmR3DevHlp_PhysReleasePageMappingLock: caller='%s'/%d: returns void\n", pDevIns->pReg->szName, pDevIns->iInstance));
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysBulkGCPhys2CCPtr} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysBulkGCPhys2CCPtr(PPDMDEVINS pDevIns, uint32_t cPages, PCRTGCPHYS paGCPhysPages,
                                                          uint32_t fFlags, void **papvPages, PPGMPAGEMAPLOCK paLocks)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PhysBulkGCPhys2CCPtr: caller='%s'/%d: cPages=%#x paGCPhysPages=%p (%RGp,..) fFlags=%#x papvPages=%p paLocks=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cPages, paGCPhysPages, paGCPhysPages[0], fFlags, papvPages, paLocks));
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);
    AssertReturn(cPages > 0, VERR_INVALID_PARAMETER);

    int rc = VERR_NOT_IMPLEMENTED;;
    AssertFailed();

    Log(("pdmR3DevHlp_PhysBulkGCPhys2CCPtr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysBulkGCPhys2CCPtrReadOnly} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysBulkGCPhys2CCPtrReadOnly(PPDMDEVINS pDevIns, uint32_t cPages, PCRTGCPHYS paGCPhysPages,
                                                                  uint32_t fFlags, const void **papvPages, PPGMPAGEMAPLOCK paLocks)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PhysBulkGCPhys2CCPtrReadOnly: caller='%s'/%d: cPages=%#x paGCPhysPages=%p (%RGp,...) fFlags=%#x papvPages=%p paLocks=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cPages, paGCPhysPages, paGCPhysPages[0], fFlags, papvPages, paLocks));
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);
    AssertReturn(cPages > 0, VERR_INVALID_PARAMETER);

    int rc = VERR_NOT_IMPLEMENTED;;
    AssertFailed();

    Log(("pdmR3DevHlp_PhysBulkGCPhys2CCPtrReadOnly: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysBulkReleasePageMappingLocks} */
static DECLCALLBACK(void) pdmR3DevHlp_PhysBulkReleasePageMappingLocks(PPDMDEVINS pDevIns, uint32_t cPages, PPGMPAGEMAPLOCK paLocks)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PhysBulkReleasePageMappingLocks: caller='%s'/%d: cPages=%#x paLocks=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cPages, paLocks));
    Assert(cPages > 0);

    AssertFailed();

    Log(("pdmR3DevHlp_PhysBulkReleasePageMappingLocks: caller='%s'/%d: returns void\n", pDevIns->pReg->szName, pDevIns->iInstance));
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysIsGCPhysNormal} */
static DECLCALLBACK(bool) pdmR3DevHlp_PhysIsGCPhysNormal(PPDMDEVINS pDevIns, RTGCPHYS GCPhys)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PhysIsGCPhysNormal: caller='%s'/%d: GCPhys=%RGp\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPhys));

    bool fNormal = true;
    AssertFailed();

    Log(("pdmR3DevHlp_PhysIsGCPhysNormal: caller='%s'/%d: returns %RTbool\n", pDevIns->pReg->szName, pDevIns->iInstance, fNormal));
    return fNormal;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysChangeMemBalloon} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysChangeMemBalloon(PPDMDEVINS pDevIns, bool fInflate, unsigned cPages, RTGCPHYS *paPhysPage)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PhysChangeMemBalloon: caller='%s'/%d: fInflate=%RTbool cPages=%u paPhysPage=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, fInflate, cPages, paPhysPage));

    int rc = VERR_NOT_IMPLEMENTED;;
    AssertFailed();

    Log(("pdmR3DevHlp_PhysChangeMemBalloon: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCpuGetGuestMicroarch} */
static DECLCALLBACK(CPUMMICROARCH) pdmR3DevHlp_CpuGetGuestMicroarch(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_CpuGetGuestMicroarch: caller='%s'/%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    CPUMMICROARCH enmMicroarch = kCpumMicroarch_Intel_P6;

    Log(("pdmR3DevHlp_CpuGetGuestMicroarch: caller='%s'/%d: returns %u\n", pDevIns->pReg->szName, pDevIns->iInstance, enmMicroarch));
    return enmMicroarch;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCpuGetGuestAddrWidths} */
static DECLCALLBACK(void) pdmR3DevHlp_CpuGetGuestAddrWidths(PPDMDEVINS pDevIns, uint8_t *pcPhysAddrWidth,
                                                            uint8_t *pcLinearAddrWidth)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_CpuGetGuestAddrWidths: caller='%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    AssertPtrReturnVoid(pcPhysAddrWidth);
    AssertPtrReturnVoid(pcLinearAddrWidth);

    AssertFailed();

    Log(("pdmR3DevHlp_CpuGetGuestAddrWidths: caller='%s'/%d: returns void\n", pDevIns->pReg->szName, pDevIns->iInstance));
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCpuGetGuestScalableBusFrequency} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_CpuGetGuestScalableBusFrequency(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_CpuGetGuestScalableBusFrequency: caller='%s'/%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    AssertFailed();
    uint64_t u64Fsb = 0;

    Log(("pdmR3DevHlp_CpuGetGuestScalableBusFrequency: caller='%s'/%d: returns %#RX64\n", pDevIns->pReg->szName, pDevIns->iInstance, u64Fsb));
    return u64Fsb;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysReadGCVirt} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysReadGCVirt(PPDMDEVINS pDevIns, void *pvDst, RTGCPTR GCVirtSrc, size_t cb)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PhysReadGCVirt: caller='%s'/%d: pvDst=%p GCVirt=%RGv cb=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pvDst, GCVirtSrc, cb));

    int rc = VERR_NOT_IMPLEMENTED;;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_PhysReadGCVirt: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));

    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysWriteGCVirt} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysWriteGCVirt(PPDMDEVINS pDevIns, RTGCPTR GCVirtDst, const void *pvSrc, size_t cb)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PhysWriteGCVirt: caller='%s'/%d: GCVirtDst=%RGv pvSrc=%p cb=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCVirtDst, pvSrc, cb));

    int rc = VERR_NOT_IMPLEMENTED;;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_PhysWriteGCVirt: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));

    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysGCPtr2GCPhys} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysGCPtr2GCPhys(PPDMDEVINS pDevIns, RTGCPTR GCPtr, PRTGCPHYS pGCPhys)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PhysGCPtr2GCPhys: caller='%s'/%d: GCPtr=%RGv pGCPhys=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPtr, pGCPhys));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_PhysGCPtr2GCPhys: caller='%s'/%d: returns %Rrc *pGCPhys=%RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, rc, *pGCPhys));

    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMMHeapAlloc} */
static DECLCALLBACK(void *) pdmR3DevHlp_MMHeapAlloc(PPDMDEVINS pDevIns, size_t cb)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MMHeapAlloc: caller='%s'/%d: cb=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, cb));

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    void *pv = NULL;
    AssertFailed();
#else
    void *pv = RTMemAlloc(cb);
#endif

    LogFlow(("pdmR3DevHlp_MMHeapAlloc: caller='%s'/%d: returns %p\n", pDevIns->pReg->szName, pDevIns->iInstance, pv));
    return pv;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMMHeapAllocZ} */
static DECLCALLBACK(void *) pdmR3DevHlp_MMHeapAllocZ(PPDMDEVINS pDevIns, size_t cb)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MMHeapAllocZ: caller='%s'/%d: cb=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, cb));

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    void *pv = NULL;
    AssertFailed();
#else
    void *pv = RTMemAllocZ(cb);
#endif

    LogFlow(("pdmR3DevHlp_MMHeapAllocZ: caller='%s'/%d: returns %p\n", pDevIns->pReg->szName, pDevIns->iInstance, pv));
    return pv;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMMHeapAPrintfV} */
static DECLCALLBACK(char *) pdmR3DevHlp_MMHeapAPrintfV(PPDMDEVINS pDevIns, MMTAG enmTag, const char *pszFormat, va_list va)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MMHeapAPrintfV: caller='%s'/%d: enmTag=%u pszFormat=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, enmTag, pszFormat, pszFormat));

    RT_NOREF(va);
    AssertFailed();
    char *psz = NULL;

    LogFlow(("pdmR3DevHlp_MMHeapAPrintfV: caller='%s'/%d: returns %p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, psz, psz));
    return psz;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMMHeapFree} */
static DECLCALLBACK(void) pdmR3DevHlp_MMHeapFree(PPDMDEVINS pDevIns, void *pv)
{
    PDMDEV_ASSERT_DEVINS(pDevIns); RT_NOREF_PV(pDevIns);
    LogFlow(("pdmR3DevHlp_MMHeapFree: caller='%s'/%d: pv=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pv));

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    PTSTDEVMMHEAPALLOC pHeapAlloc = (PTSTDEVMMHEAPALLOC)((uint8_t *)pv - RT_UOFFSETOF(TSTDEVMMHEAPALLOC, abAlloc[0]));
    PTSTDEVDUTINT pThis = pHeapAlloc->pDut;

    tstDevDutLockExcl(pThis);
    RTListNodeRemove(&pHeapAlloc->NdMmHeap);
    tstDevDutUnlockExcl(pThis);

    /* Poison */
    memset(&pHeapAlloc->abAlloc[0], 0xfc, pHeapAlloc->cbAlloc);
    RTMemFree(pHeapAlloc);
#else
    RTMemFree(pv);
#endif

    LogFlow(("pdmR3DevHlp_MMHeapAlloc: caller='%s'/%d: returns void\n", pDevIns->pReg->szName, pDevIns->iInstance));
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMMPhysGetRamSize} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_MMPhysGetRamSize(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns); RT_NOREF_PV(pDevIns);
    LogFlow(("pdmR3DevHlp_MMPhysGetRamSize: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    AssertFailed();
    uint64_t cb = 0;
#else
    uint64_t cb = _4G;
#endif

    LogFlow(("pdmR3DevHlp_MMPhysGetRamSize: caller='%s'/%d: returns %RU64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cb));
    return cb;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMMPhysGetRamSizeBelow4GB} */
static DECLCALLBACK(uint32_t) pdmR3DevHlp_MMPhysGetRamSizeBelow4GB(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns); RT_NOREF_PV(pDevIns);
    LogFlow(("pdmR3DevHlp_MMPhysGetRamSizeBelow4GB: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    AssertFailed();
    uint32_t cb = 0;

    LogFlow(("pdmR3DevHlp_MMPhysGetRamSizeBelow4GB: caller='%s'/%d: returns %RU32\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cb));
    return cb;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMMPhysGetRamSizeAbove4GB} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_MMPhysGetRamSizeAbove4GB(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns); RT_NOREF_PV(pDevIns);
    LogFlow(("pdmR3DevHlp_MMPhysGetRamSizeAbove4GB: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    AssertFailed();
    uint64_t cb = 0;

    LogFlow(("pdmR3DevHlp_MMPhysGetRamSizeAbove4GB: caller='%s'/%d: returns %RU64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cb));
    return cb;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMState} */
static DECLCALLBACK(VMSTATE) pdmR3DevHlp_VMState(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    VMSTATE enmVMState = VMSTATE_CREATING;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_VMState: caller='%s'/%d: returns %d\n", pDevIns->pReg->szName, pDevIns->iInstance,
             enmVMState));
    return enmVMState;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMTeleportedAndNotFullyResumedYet} */
static DECLCALLBACK(bool) pdmR3DevHlp_VMTeleportedAndNotFullyResumedYet(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    bool fRc = false;

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    AssertFailed();
#endif

    LogFlow(("pdmR3DevHlp_VMState: caller='%s'/%d: returns %RTbool\n", pDevIns->pReg->szName, pDevIns->iInstance,
             fRc));
    return fRc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMSetErrorV} */
static DECLCALLBACK(int) pdmR3DevHlp_VMSetErrorV(PPDMDEVINS pDevIns, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list va)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    RT_NOREF(pDevIns, rc, RT_SRC_POS_ARGS, pszFormat, va);
    AssertFailed();

    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMSetRuntimeErrorV} */
static DECLCALLBACK(int) pdmR3DevHlp_VMSetRuntimeErrorV(PPDMDEVINS pDevIns, uint32_t fFlags, const char *pszErrorId, const char *pszFormat, va_list va)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    RT_NOREF(pDevIns, fFlags, pszErrorId, pszFormat, va);
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMWaitForDeviceReady} */
static DECLCALLBACK(int) pdmR3DevHlp_VMWaitForDeviceReady(PPDMDEVINS pDevIns, VMCPUID idCpu)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_VMWaitForDeviceReady: caller='%s'/%d: idCpu=%u\n", pDevIns->pReg->szName, pDevIns->iInstance, idCpu));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_VMWaitForDeviceReady: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMNotifyCpuDeviceReady} */
static DECLCALLBACK(int) pdmR3DevHlp_VMNotifyCpuDeviceReady(PPDMDEVINS pDevIns, VMCPUID idCpu)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_VMNotifyCpuDeviceReady: caller='%s'/%d: idCpu=%u\n", pDevIns->pReg->szName, pDevIns->iInstance, idCpu));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_VMNotifyCpuDeviceReady: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMReqCallNoWaitV} */
static DECLCALLBACK(int) pdmR3DevHlp_VMReqCallNoWaitV(PPDMDEVINS pDevIns, VMCPUID idDstCpu, PFNRT pfnFunction, unsigned cArgs, va_list Args)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_VMReqCallNoWaitV: caller='%s'/%d: idDstCpu=%u pfnFunction=%p cArgs=%u\n",
             pDevIns->pReg->szName, pDevIns->iInstance, idDstCpu, pfnFunction, cArgs));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed(); RT_NOREF(Args);

    LogFlow(("pdmR3DevHlp_VMReqCallNoWaitV: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMReqPriorityCallWaitV} */
static DECLCALLBACK(int) pdmR3DevHlp_VMReqPriorityCallWaitV(PPDMDEVINS pDevIns, VMCPUID idDstCpu, PFNRT pfnFunction, unsigned cArgs, va_list Args)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_VMReqCallNoWaitV: caller='%s'/%d: idDstCpu=%u pfnFunction=%p cArgs=%u\n",
             pDevIns->pReg->szName, pDevIns->iInstance, idDstCpu, pfnFunction, cArgs));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed(); RT_NOREF(Args);

    LogFlow(("pdmR3DevHlp_VMReqCallNoWaitV: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDBGFStopV} */
static DECLCALLBACK(int) pdmR3DevHlp_DBGFStopV(PPDMDEVINS pDevIns, const char *pszFile, unsigned iLine, const char *pszFunction, const char *pszFormat, va_list args)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
#ifdef LOG_ENABLED
    va_list va2;
    va_copy(va2, args);
    LogFlow(("pdmR3DevHlp_DBGFStopV: caller='%s'/%d: pszFile=%p:{%s} iLine=%d pszFunction=%p:{%s} pszFormat=%p:{%s} (%N)\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pszFile, pszFile, iLine, pszFunction, pszFunction, pszFormat, pszFormat, pszFormat, &va2));
    va_end(va2);
#endif

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_DBGFStopV: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDBGFInfoRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_DBGFInfoRegister(PPDMDEVINS pDevIns, const char *pszName, const char *pszDesc, PFNDBGFHANDLERDEV pfnHandler)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DBGFInfoRegister: caller='%s'/%d: pszName=%p:{%s} pszDesc=%p:{%s} pfnHandler=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pszName, pszName, pszDesc, pszDesc, pfnHandler));

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif


    LogFlow(("pdmR3DevHlp_DBGFInfoRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDBGFInfoRegisterArgv} */
static DECLCALLBACK(int) pdmR3DevHlp_DBGFInfoRegisterArgv(PPDMDEVINS pDevIns, const char *pszName, const char *pszDesc, PFNDBGFINFOARGVDEV pfnHandler)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DBGFInfoRegisterArgv: caller='%s'/%d: pszName=%p:{%s} pszDesc=%p:{%s} pfnHandler=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pszName, pszName, pszDesc, pszDesc, pfnHandler));

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    LogFlow(("pdmR3DevHlp_DBGFInfoRegisterArgv: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDBGFRegRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_DBGFRegRegister(PPDMDEVINS pDevIns, PCDBGFREGDESC paRegisters)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DBGFRegRegister: caller='%s'/%d: paRegisters=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, paRegisters));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_DBGFRegRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDBGFTraceBuf} */
static DECLCALLBACK(RTTRACEBUF) pdmR3DevHlp_DBGFTraceBuf(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RTTRACEBUF hTraceBuf = NIL_RTTRACEBUF;
    AssertFailed();
    LogFlow(("pdmR3DevHlp_DBGFTraceBuf: caller='%s'/%d: returns %p\n", pDevIns->pReg->szName, pDevIns->iInstance, hTraceBuf));
    return hTraceBuf;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDBGFReportBugCheck} */
static DECLCALLBACK(VBOXSTRICTRC) pdmR3DevHlp_DBGFReportBugCheck(PPDMDEVINS pDevIns, DBGFEVENTTYPE enmEvent, uint64_t uBugCheck,
                                                                 uint64_t uP1, uint64_t uP2, uint64_t uP3, uint64_t uP4)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DBGFReportBugCheck: caller='%s'/%d: enmEvent=%u uBugCheck=%#x uP1=%#x uP2=%#x uP3=%#x uP4=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, enmEvent, uBugCheck, uP1, uP2, uP3, uP4));

    AssertFailed();
    VBOXSTRICTRC rcStrict = VERR_NOT_IMPLEMENTED;

    LogFlow(("pdmR3DevHlp_DBGFReportBugCheck: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDBGFCoreWrite} */
static DECLCALLBACK(int) pdmR3DevHlp_DBGFCoreWrite(PPDMDEVINS pDevIns, const char *pszFilename, bool fReplaceFile)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DBGFCoreWrite: caller='%s'/%d: pszFilename=%p:{%s} fReplaceFile=%RTbool\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pszFilename, pszFilename, fReplaceFile));

    AssertFailed();
    int rc = VERR_NOT_IMPLEMENTED;

    LogFlow(("pdmR3DevHlp_DBGFCoreWrite: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDBGFInfoLogHlp} */
static DECLCALLBACK(PCDBGFINFOHLP) pdmR3DevHlp_DBGFInfoLogHlp(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns); RT_NOREF(pDevIns);
    LogFlow(("pdmR3DevHlp_DBGFInfoLogHlp: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    AssertFailed();
    PCDBGFINFOHLP pHlp = NULL;

    LogFlow(("pdmR3DevHlp_DBGFInfoLogHlp: caller='%s'/%d: returns %p\n", pDevIns->pReg->szName, pDevIns->iInstance, pHlp));
    return pHlp;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDBGFRegNmQueryU64} */
static DECLCALLBACK(int) pdmR3DevHlp_DBGFRegNmQueryU64(PPDMDEVINS pDevIns, VMCPUID idDefCpu, const char *pszReg, uint64_t *pu64)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DBGFRegNmQueryU64: caller='%s'/%d: idDefCpu=%u pszReg=%p:{%s} pu64=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, idDefCpu, pszReg, pszReg, pu64));

    AssertFailed();
    int rc = VERR_NOT_IMPLEMENTED;

    LogFlow(("pdmR3DevHlp_DBGFRegNmQueryU64: caller='%s'/%d: returns %Rrc *pu64=%#RX64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc, *pu64));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDBGFRegPrintfV} */
static DECLCALLBACK(int) pdmR3DevHlp_DBGFRegPrintfV(PPDMDEVINS pDevIns, VMCPUID idCpu, char *pszBuf, size_t cbBuf,
                                                    const char *pszFormat, va_list va)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DBGFRegPrintfV: caller='%s'/%d: idCpu=%u pszBuf=%p cbBuf=%u pszFormat=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, idCpu, pszBuf, cbBuf, pszFormat, pszFormat));

    AssertFailed();
    RT_NOREF(va);
    int rc = VERR_NOT_IMPLEMENTED;

    LogFlow(("pdmR3DevHlp_DBGFRegPrintfV: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSTAMRegister} */
static DECLCALLBACK(void) pdmR3DevHlp_STAMRegister(PPDMDEVINS pDevIns, void *pvSample, STAMTYPE enmType, const char *pszName,
                                                   STAMUNIT enmUnit, const char *pszDesc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    RT_NOREF(pDevIns, pvSample, enmType, pszName, enmUnit, pszDesc);

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    AssertFailed();
#endif
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSTAMRegisterV} */
static DECLCALLBACK(void) pdmR3DevHlp_STAMRegisterV(PPDMDEVINS pDevIns, void *pvSample, STAMTYPE enmType, STAMVISIBILITY enmVisibility,
                                                    STAMUNIT enmUnit, const char *pszDesc, const char *pszName, va_list args)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    RT_NOREF(pDevIns, pvSample, enmType, enmVisibility, enmUnit, pszDesc, pszName, args);

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    AssertFailed();
#endif
}


/**
 * @interface_method_impl{PDMDEVHLPR3,pfnPCIRegister}
 */
static DECLCALLBACK(int) pdmR3DevHlp_PCIRegister(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t fFlags,
                                                 uint8_t uPciDevNo, uint8_t uPciFunNo, const char *pszName)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PCIRegister: caller='%s'/%d: pPciDev=%p:{.config={%#.256Rhxs} fFlags=%#x uPciDevNo=%#x uPciFunNo=%#x pszName=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPciDev, pPciDev->abConfig, fFlags, uPciDevNo, uPciFunNo, pszName, pszName ? pszName : ""));

    /*
     * Validate input.
     */
    AssertLogRelMsgReturn(pDevIns->pReg->cMaxPciDevices > 0,
                          ("'%s'/%d: cMaxPciDevices is 0\n", pDevIns->pReg->szName, pDevIns->iInstance),
                          VERR_WRONG_ORDER);
    AssertLogRelMsgReturn(RT_VALID_PTR(pPciDev),
                          ("'%s'/%d: Invalid pPciDev value: %p\n", pDevIns->pReg->szName, pDevIns->iInstance, pPciDev),
                          VERR_INVALID_POINTER);
    AssertLogRelMsgReturn(PDMPciDevGetVendorId(pPciDev),
                          ("'%s'/%d: Vendor ID is not set!\n", pDevIns->pReg->szName, pDevIns->iInstance),
                          VERR_INVALID_POINTER);
    AssertLogRelMsgReturn(   uPciDevNo < 32
                          || uPciDevNo == PDMPCIDEVREG_DEV_NO_FIRST_UNUSED
                          || uPciDevNo == PDMPCIDEVREG_DEV_NO_SAME_AS_PREV,
                          ("'%s'/%d: Invalid PCI device number: %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, uPciDevNo),
                          VERR_INVALID_PARAMETER);
    AssertLogRelMsgReturn(   uPciFunNo < 8
                          || uPciFunNo == PDMPCIDEVREG_FUN_NO_FIRST_UNUSED,
                          ("'%s'/%d: Invalid PCI funcion number: %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, uPciFunNo),
                          VERR_INVALID_PARAMETER);
    AssertLogRelMsgReturn(!(fFlags & ~PDMPCIDEVREG_F_VALID_MASK),
                          ("'%s'/%d: Invalid flags: %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, fFlags),
                          VERR_INVALID_FLAGS);
    if (!pszName)
        pszName = pDevIns->pReg->szName;

#if 0
    AssertLogRelReturn(RT_VALID_PTR(pszName), VERR_INVALID_POINTER);
    AssertLogRelReturn(!pPciDev->Int.s.fRegistered, VERR_PDM_NOT_PCI_DEVICE);
    AssertLogRelReturn(pPciDev == PDMDEV_GET_PPCIDEV(pDevIns, pPciDev->Int.s.idxSubDev), VERR_PDM_NOT_PCI_DEVICE);
    AssertLogRelReturn(pPciDev == PDMDEV_CALC_PPCIDEV(pDevIns, pPciDev->Int.s.idxSubDev), VERR_PDM_NOT_PCI_DEVICE);
    AssertMsgReturn(pPciDev->u32Magic == PDMPCIDEV_MAGIC, ("%#x\n", pPciDev->u32Magic), VERR_PDM_NOT_PCI_DEVICE);
#endif

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    LogFlow(("pdmR3DevHlp_PCIRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIRegisterMsi} */
static DECLCALLBACK(int) pdmR3DevHlp_PCIRegisterMsi(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, PPDMMSIREG pMsiReg)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    if (!pPciDev) /* NULL is an alias for the default PCI device. */
        pPciDev = pDevIns->apPciDevs[0];
    AssertReturn(pPciDev, VERR_PDM_NOT_PCI_DEVICE);
    LogFlow(("pdmR3DevHlp_PCIRegisterMsi: caller='%s'/%d: pPciDev=%p:{%#x} pMsgReg=%p:{cMsiVectors=%d, cMsixVectors=%d}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPciDev, pPciDev->uDevFn, pMsiReg, pMsiReg->cMsiVectors, pMsiReg->cMsixVectors));
    PDMPCIDEV_ASSERT_VALID_RET(pDevIns, pPciDev);

    AssertLogRelMsgReturn(pDevIns->pReg->cMaxPciDevices > 0,
                          ("'%s'/%d: cMaxPciDevices is 0\n", pDevIns->pReg->szName, pDevIns->iInstance),
                          VERR_WRONG_ORDER);
    AssertLogRelMsgReturn(pMsiReg->cMsixVectors <= pDevIns->pReg->cMaxMsixVectors,
                          ("'%s'/%d: cMsixVectors=%u cMaxMsixVectors=%u\n",
                           pDevIns->pReg->szName, pDevIns->iInstance, pMsiReg->cMsixVectors, pDevIns->pReg->cMaxMsixVectors),
                          VERR_INVALID_FLAGS);

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif


    LogFlow(("pdmR3DevHlp_PCIRegisterMsi: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIIORegionRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_PCIIORegionRegister(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                                         RTGCPHYS cbRegion, PCIADDRESSSPACE enmType, uint32_t fFlags,
                                                         uint64_t hHandle, PFNPCIIOREGIONMAP pfnMapUnmap)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    if (!pPciDev) /* NULL is an alias for the default PCI device. */
        pPciDev = pDevIns->apPciDevs[0];
    AssertReturn(pPciDev, VERR_PDM_NOT_PCI_DEVICE);
    LogFlow(("pdmR3DevHlp_PCIIORegionRegister: caller='%s'/%d: pPciDev=%p:{%#x} iRegion=%d cbRegion=%RGp enmType=%d fFlags=%#x, hHandle=%#RX64 pfnMapUnmap=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPciDev, pPciDev->uDevFn, iRegion, cbRegion, enmType, fFlags, hHandle, pfnMapUnmap));

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    PDMPCIDEV_ASSERT_VALID_RET(pDevIns, pPciDev);
#endif

    /*
     * Validate input.
     */
    if (iRegion >= VBOX_PCI_NUM_REGIONS)
    {
        Assert(iRegion < VBOX_PCI_NUM_REGIONS);
        LogFlow(("pdmR3DevHlp_PCIIORegionRegister: caller='%s'/%d: returns %Rrc (iRegion)\n", pDevIns->pReg->szName, pDevIns->iInstance, VERR_INVALID_PARAMETER));
        return VERR_INVALID_PARAMETER;
    }

    switch ((int)enmType)
    {
        case PCI_ADDRESS_SPACE_IO:
            /*
             * Sanity check: don't allow to register more than 32K of the PCI I/O space.
             */
            AssertLogRelMsgReturn(cbRegion <= _32K,
                                  ("caller='%s'/%d: %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, cbRegion),
                                  VERR_INVALID_PARAMETER);
            break;

        case PCI_ADDRESS_SPACE_MEM:
        case PCI_ADDRESS_SPACE_MEM_PREFETCH:
            /*
             * Sanity check: Don't allow to register more than 2GB of the PCI MMIO space.
             */
            AssertLogRelMsgReturn(cbRegion <= MM_MMIO_32_MAX,
                                  ("caller='%s'/%d: %RGp (max %RGp)\n",
                                   pDevIns->pReg->szName, pDevIns->iInstance, cbRegion, (RTGCPHYS)MM_MMIO_32_MAX),
                                  VERR_OUT_OF_RANGE);
            break;

        case PCI_ADDRESS_SPACE_BAR64 | PCI_ADDRESS_SPACE_MEM:
        case PCI_ADDRESS_SPACE_BAR64 | PCI_ADDRESS_SPACE_MEM_PREFETCH:
            /*
             * Sanity check: Don't allow to register more than 64GB of the 64-bit PCI MMIO space.
             */
            AssertLogRelMsgReturn(cbRegion <= MM_MMIO_64_MAX,
                                  ("caller='%s'/%d: %RGp (max %RGp)\n",
                                   pDevIns->pReg->szName, pDevIns->iInstance, cbRegion, MM_MMIO_64_MAX),
                                  VERR_OUT_OF_RANGE);
            break;

        default:
            AssertMsgFailed(("enmType=%#x is unknown\n", enmType));
            LogFlow(("pdmR3DevHlp_PCIIORegionRegister: caller='%s'/%d: returns %Rrc (enmType)\n", pDevIns->pReg->szName, pDevIns->iInstance, VERR_INVALID_PARAMETER));
            return VERR_INVALID_PARAMETER;
    }

    AssertMsgReturn(   pfnMapUnmap
                    || (   hHandle != UINT64_MAX
                        && (fFlags & PDMPCIDEV_IORGN_F_HANDLE_MASK) != PDMPCIDEV_IORGN_F_NO_HANDLE),
                    ("caller='%s'/%d: fFlags=%#x hHandle=%#RX64\n", pDevIns->pReg->szName, pDevIns->iInstance, fFlags, hHandle),
                    VERR_INVALID_PARAMETER);

    AssertMsgReturn(!(fFlags & ~PDMPCIDEV_IORGN_F_VALID_MASK), ("fFlags=%#x\n", fFlags), VERR_INVALID_FLAGS);
#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    LogFlow(("pdmR3DevHlp_PCIIORegionRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIInterceptConfigAccesses} */
static DECLCALLBACK(int) pdmR3DevHlp_PCIInterceptConfigAccesses(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                                PFNPCICONFIGREAD pfnRead, PFNPCICONFIGWRITE pfnWrite)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    if (!pPciDev) /* NULL is an alias for the default PCI device. */
        pPciDev = pDevIns->apPciDevs[0];
    AssertReturn(pPciDev, VERR_PDM_NOT_PCI_DEVICE);
    LogFlow(("pdmR3DevHlp_PCIInterceptConfigAccesses: caller='%s'/%d: pPciDev=%p pfnRead=%p pfnWrite=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPciDev, pfnRead, pfnWrite));

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    PDMPCIDEV_ASSERT_VALID_RET(pDevIns, pPciDev);

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    LogFlow(("pdmR3DevHlp_PCIInterceptConfigAccesses: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIConfigWrite} */
static DECLCALLBACK(VBOXSTRICTRC)
pdmR3DevHlp_PCIConfigWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t uAddress, unsigned cb, uint32_t u32Value)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertPtrReturn(pPciDev, VERR_PDM_NOT_PCI_DEVICE);
    LogFlow(("pdmR3DevHlp_PCIConfigWrite: caller='%s'/%d: pPciDev=%p uAddress=%#x cd=%d u32Value=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPciDev, uAddress, cb, u32Value));

    VBOXSTRICTRC rcStrict = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_PCIConfigWrite: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIConfigRead} */
static DECLCALLBACK(VBOXSTRICTRC)
pdmR3DevHlp_PCIConfigRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t uAddress, unsigned cb, uint32_t *pu32Value)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertPtrReturn(pPciDev, VERR_PDM_NOT_PCI_DEVICE);
    LogFlow(("pdmR3DevHlp_PCIConfigRead: caller='%s'/%d: pPciDev=%p uAddress=%#x cd=%d pu32Value=%p:{%#x}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPciDev, uAddress, cb, pu32Value, *pu32Value));

    VBOXSTRICTRC rcStrict = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_PCIConfigRead: caller='%s'/%d: returns %Rrc (*pu32Value=%#x)\n",
             pDevIns->pReg->szName, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict), *pu32Value));
    return rcStrict;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIPhysRead} */
static DECLCALLBACK(int)
pdmR3DevHlp_PCIPhysRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead, uint32_t fFlags)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    if (!pPciDev) /* NULL is an alias for the default PCI device. */
        pPciDev = pDevIns->apPciDevs[0];
    AssertReturn(pPciDev, VERR_PDM_NOT_PCI_DEVICE);
    //PDMPCIDEV_ASSERT_VALID_AND_REGISTERED(pDevIns, pPciDev);

#ifndef PDM_DO_NOT_RESPECT_PCI_BM_BIT
    /*
     * Just check the busmaster setting here and forward the request to the generic read helper.
     */
    if (PCIDevIsBusmaster(pPciDev))
    { /* likely */ }
    else
    {
        Log(("pdmR3DevHlp_PCIPhysRead: caller='%s'/%d: returns %Rrc - Not bus master! GCPhys=%RGp cbRead=%#zx\n",
             pDevIns->pReg->szName, pDevIns->iInstance, VERR_PDM_NOT_PCI_BUS_MASTER, GCPhys, cbRead));
        memset(pvBuf, 0xff, cbRead);
        return VERR_PDM_NOT_PCI_BUS_MASTER;
    }
#endif

    RT_NOREF(fFlags);
#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    RTRandBytes(pvBuf, cbRead);
    int rc = VINF_SUCCESS;
#endif

    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIPhysWrite} */
static DECLCALLBACK(int)
pdmR3DevHlp_PCIPhysWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite, uint32_t fFlags)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    if (!pPciDev) /* NULL is an alias for the default PCI device. */
        pPciDev = pDevIns->apPciDevs[0];
    AssertReturn(pPciDev, VERR_PDM_NOT_PCI_DEVICE);
    //PDMPCIDEV_ASSERT_VALID_AND_REGISTERED(pDevIns, pPciDev);

#ifndef PDM_DO_NOT_RESPECT_PCI_BM_BIT
    /*
     * Just check the busmaster setting here and forward the request to the generic read helper.
     */
    if (PCIDevIsBusmaster(pPciDev))
    { /* likely */ }
    else
    {
        Log(("pdmR3DevHlp_PCIPhysWrite: caller='%s'/%d: returns %Rrc - Not bus master! GCPhys=%RGp cbWrite=%#zx\n",
             pDevIns->pReg->szName, pDevIns->iInstance, VERR_PDM_NOT_PCI_BUS_MASTER, GCPhys, cbWrite));
        return VERR_PDM_NOT_PCI_BUS_MASTER;
    }
#endif

    RT_NOREF(GCPhys, pvBuf, cbWrite, fFlags);
#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIPhysGCPhys2CCPtr} */
static DECLCALLBACK(int) pdmR3DevHlp_PCIPhysGCPhys2CCPtr(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys,
                                                         uint32_t fFlags, void **ppv, PPGMPAGEMAPLOCK pLock)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    if (!pPciDev) /* NULL is an alias for the default PCI device. */
        pPciDev = pDevIns->apPciDevs[0];
    AssertReturn(pPciDev, VERR_PDM_NOT_PCI_DEVICE);
    PDMPCIDEV_ASSERT_VALID_AND_REGISTERED(pDevIns, pPciDev);

#ifndef PDM_DO_NOT_RESPECT_PCI_BM_BIT
    if (PCIDevIsBusmaster(pPciDev))
    { /* likely */ }
    else
    {
        LogFunc(("caller='%s'/%d: returns %Rrc - Not bus master! GCPhys=%RGp fFlags=%#RX32\n",
                 pDevIns->pReg->szName, pDevIns->iInstance, VERR_PDM_NOT_PCI_BUS_MASTER, GCPhys, fFlags));
        return VERR_PDM_NOT_PCI_BUS_MASTER;
    }
#endif

    AssertFailed(); RT_NOREF(ppv, pLock);
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIPhysGCPhys2CCPtrReadOnly} */
static DECLCALLBACK(int) pdmR3DevHlp_PCIPhysGCPhys2CCPtrReadOnly(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys,
                                                                 uint32_t fFlags, void const **ppv, PPGMPAGEMAPLOCK pLock)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    if (!pPciDev) /* NULL is an alias for the default PCI device. */
        pPciDev = pDevIns->apPciDevs[0];
    AssertReturn(pPciDev, VERR_PDM_NOT_PCI_DEVICE);
    PDMPCIDEV_ASSERT_VALID_AND_REGISTERED(pDevIns, pPciDev);

#ifndef PDM_DO_NOT_RESPECT_PCI_BM_BIT
    if (PCIDevIsBusmaster(pPciDev))
    { /* likely */ }
    else
    {
        LogFunc(("caller='%s'/%d: returns %Rrc - Not bus master! GCPhys=%RGp fFlags=%#RX32\n",
                 pDevIns->pReg->szName, pDevIns->iInstance, VERR_PDM_NOT_PCI_BUS_MASTER, GCPhys, fFlags));
        return VERR_PDM_NOT_PCI_BUS_MASTER;
    }
#endif

    AssertFailed(); RT_NOREF(ppv, pLock);
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIPhysBulkGCPhys2CCPtr} */
static DECLCALLBACK(int) pdmR3DevHlp_PCIPhysBulkGCPhys2CCPtr(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t cPages,
                                                             PCRTGCPHYS paGCPhysPages, uint32_t fFlags, void **papvPages,
                                                             PPGMPAGEMAPLOCK paLocks)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    if (!pPciDev) /* NULL is an alias for the default PCI device. */
        pPciDev = pDevIns->apPciDevs[0];
    AssertReturn(pPciDev, VERR_PDM_NOT_PCI_DEVICE);
    PDMPCIDEV_ASSERT_VALID_AND_REGISTERED(pDevIns, pPciDev);

#ifndef PDM_DO_NOT_RESPECT_PCI_BM_BIT
    if (PCIDevIsBusmaster(pPciDev))
    { /* likely */ }
    else
    {
        LogFunc(("caller='%s'/%d: returns %Rrc - Not bus master! cPages=%zu fFlags=%#RX32\n",
                 pDevIns->pReg->szName, pDevIns->iInstance, VERR_PDM_NOT_PCI_BUS_MASTER, cPages, fFlags));
        return VERR_PDM_NOT_PCI_BUS_MASTER;
    }
#endif

    AssertFailed(); RT_NOREF(paGCPhysPages, fFlags, papvPages, paLocks);
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIPhysBulkGCPhys2CCPtrReadOnly} */
static DECLCALLBACK(int) pdmR3DevHlp_PCIPhysBulkGCPhys2CCPtrReadOnly(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t cPages,
                                                                     PCRTGCPHYS paGCPhysPages, uint32_t fFlags,
                                                                     const void **papvPages, PPGMPAGEMAPLOCK paLocks)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    if (!pPciDev) /* NULL is an alias for the default PCI device. */
        pPciDev = pDevIns->apPciDevs[0];
    AssertReturn(pPciDev, VERR_PDM_NOT_PCI_DEVICE);
    PDMPCIDEV_ASSERT_VALID_AND_REGISTERED(pDevIns, pPciDev);

#ifndef PDM_DO_NOT_RESPECT_PCI_BM_BIT
    if (PCIDevIsBusmaster(pPciDev))
    { /* likely */ }
    else
    {
        LogFunc(("caller='%s'/%d: returns %Rrc - Not bus master! cPages=%zu fFlags=%#RX32\n",
                 pDevIns->pReg->szName, pDevIns->iInstance, VERR_PDM_NOT_PCI_BUS_MASTER, cPages, fFlags));
        return VERR_PDM_NOT_PCI_BUS_MASTER;
    }
#endif

    AssertFailed(); RT_NOREF(paGCPhysPages, fFlags, papvPages, paLocks);
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCISetIrq} */
static DECLCALLBACK(void) pdmR3DevHlp_PCISetIrq(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq, int iLevel)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    if (!pPciDev) /* NULL is an alias for the default PCI device. */
        pPciDev = pDevIns->apPciDevs[0];
    AssertReturnVoid(pPciDev);
    LogFlow(("pdmR3DevHlp_PCISetIrq: caller='%s'/%d: pPciDev=%p:{%#x} iIrq=%d iLevel=%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPciDev, pPciDev->uDevFn, iIrq, iLevel));
    //PDMPCIDEV_ASSERT_VALID_AND_REGISTERED(pDevIns, pPciDev);

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    RT_NOREF(iIrq, iLevel);
    AssertFailed();
#endif

    LogFlow(("pdmR3DevHlp_PCISetIrq: caller='%s'/%d: returns void\n", pDevIns->pReg->szName, pDevIns->iInstance));
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCISetIrqNoWait} */
static DECLCALLBACK(void) pdmR3DevHlp_PCISetIrqNoWait(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq, int iLevel)
{
    pdmR3DevHlp_PCISetIrq(pDevIns, pPciDev, iIrq, iLevel);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnISASetIrq} */
static DECLCALLBACK(void) pdmR3DevHlp_ISASetIrq(PPDMDEVINS pDevIns, int iIrq, int iLevel)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_ISASetIrq: caller='%s'/%d: iIrq=%d iLevel=%d\n", pDevIns->pReg->szName, pDevIns->iInstance, iIrq, iLevel));

    /*
     * Validate input.
     */
    Assert(iIrq < 16);
    Assert((uint32_t)iLevel <= PDM_IRQ_LEVEL_FLIP_FLOP);

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    AssertFailed();
#endif

    LogFlow(("pdmR3DevHlp_ISASetIrq: caller='%s'/%d: returns void\n", pDevIns->pReg->szName, pDevIns->iInstance));
}


/** @interface_method_impl{PDMDEVHLPR3,pfnISASetIrqNoWait} */
static DECLCALLBACK(void) pdmR3DevHlp_ISASetIrqNoWait(PPDMDEVINS pDevIns, int iIrq, int iLevel)
{
    pdmR3DevHlp_ISASetIrq(pDevIns, iIrq, iLevel);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDriverAttach} */
static DECLCALLBACK(int) pdmR3DevHlp_DriverAttach(PPDMDEVINS pDevIns, uint32_t iLun, PPDMIBASE pBaseInterface, PPDMIBASE *ppBaseInterface, const char *pszDesc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DriverAttach: caller='%s'/%d: iLun=%d pBaseInterface=%p ppBaseInterface=%p pszDesc=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, iLun, pBaseInterface, ppBaseInterface, pszDesc, pszDesc));

#if 1
    int rc = VINF_SUCCESS;
    if (iLun == PDM_STATUS_LUN)
        *ppBaseInterface = &pDevIns->Internal.s.pDut->IBaseSts;
    else
        rc = VERR_PDM_NO_ATTACHED_DRIVER;
#else
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#endif

    LogFlow(("pdmR3DevHlp_DriverAttach: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDriverDetach} */
static DECLCALLBACK(int) pdmR3DevHlp_DriverDetach(PPDMDEVINS pDevIns, PPDMDRVINS pDrvIns, uint32_t fFlags)
{
    PDMDEV_ASSERT_DEVINS(pDevIns); RT_NOREF_PV(pDevIns);
    LogFlow(("pdmR3DevHlp_DriverDetach: caller='%s'/%d: pDrvIns=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pDrvIns));

    RT_NOREF(fFlags);
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_DriverDetach: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDriverReconfigure} */
static DECLCALLBACK(int) pdmR3DevHlp_DriverReconfigure(PPDMDEVINS pDevIns, uint32_t iLun, uint32_t cDepth,
                                                       const char * const *papszDrivers, PCFGMNODE *papConfigs, uint32_t fFlags)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DriverReconfigure: caller='%s'/%d: iLun=%u cDepth=%u fFlags=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, iLun, cDepth, fFlags));

    /*
     * Validate input.
     */
    AssertReturn(cDepth <= 8, VERR_INVALID_PARAMETER);
    AssertPtrReturn(papszDrivers, VERR_INVALID_POINTER);
    AssertPtrNullReturn(papConfigs, VERR_INVALID_POINTER);
    for (uint32_t i = 0; i < cDepth; i++)
    {
        AssertPtrReturn(papszDrivers[i], VERR_INVALID_POINTER);
        size_t cchDriver = strlen(papszDrivers[i]);
        AssertReturn(cchDriver > 0 && cchDriver < RT_SIZEOFMEMB(PDMDRVREG, szName), VERR_OUT_OF_RANGE);

        if (papConfigs)
            AssertPtrNullReturn(papConfigs[i], VERR_INVALID_POINTER);
    }
    AssertReturn(fFlags == 0, VERR_INVALID_FLAGS);

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_DriverReconfigure: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnQueueCreate} */
static DECLCALLBACK(int) pdmR3DevHlp_QueueCreate(PPDMDEVINS pDevIns, size_t cbItem, uint32_t cItems, uint32_t cMilliesInterval,
                                                 PFNPDMQUEUEDEV pfnCallback, bool fRZEnabled, const char *pszName,
                                                 PDMQUEUEHANDLE *phQueue)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_QueueCreate: caller='%s'/%d: cbItem=%#x cItems=%#x cMilliesInterval=%u pfnCallback=%p fRZEnabled=%RTbool pszName=%p:{%s} phQueue=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cbItem, cItems, cMilliesInterval, pfnCallback, fRZEnabled, pszName, pszName, phQueue));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_QueueCreate: caller='%s'/%d: returns %Rrc *ppQueue=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, rc, *phQueue));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnQueueAlloc} */
static DECLCALLBACK(PPDMQUEUEITEMCORE) pdmR3DevHlp_QueueAlloc(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue)
{
    RT_NOREF(pDevIns, hQueue);
    AssertFailed();
    return NULL;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnQueueInsert} */
static DECLCALLBACK(void) pdmR3DevHlp_QueueInsert(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue, PPDMQUEUEITEMCORE pItem)
{
    RT_NOREF(pDevIns, hQueue, pItem);
    AssertFailed();
}


/** @interface_method_impl{PDMDEVHLPR3,pfnQueueFlushIfNecessary} */
static DECLCALLBACK(bool) pdmR3DevHlp_QueueFlushIfNecessary(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue)
{
    RT_NOREF(pDevIns, hQueue);
    AssertFailed();
    return false;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTaskCreate} */
static DECLCALLBACK(int) pdmR3DevHlp_TaskCreate(PPDMDEVINS pDevIns, uint32_t fFlags, const char *pszName,
                                                PFNPDMTASKDEV pfnCallback, void *pvUser, PDMTASKHANDLE *phTask)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_TaskTrigger: caller='%s'/%d: pfnCallback=%p fFlags=%#x pszName=%p:{%s} phTask=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pfnCallback, fFlags, pszName, pszName, phTask));

    RT_NOREF(pDevIns, fFlags, pszName, pfnCallback, pvUser, phTask);
#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    LogFlow(("pdmR3DevHlp_TaskTrigger: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTaskTrigger} */
static DECLCALLBACK(int) pdmR3DevHlp_TaskTrigger(PPDMDEVINS pDevIns, PDMTASKHANDLE hTask)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_TaskTrigger: caller='%s'/%d: hTask=%RU64\n", pDevIns->pReg->szName, pDevIns->iInstance, hTask));

    RT_NOREF(pDevIns, hTask);
#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    LogFlow(("pdmR3DevHlp_TaskTrigger: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventCreate} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventCreate(PPDMDEVINS pDevIns, PSUPSEMEVENT phEvent)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventCreate: caller='%s'/%d: phEvent=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, phEvent));

    RTSEMEVENT hEvt;
    int rc = RTSemEventCreate(&hEvt);
    if (RT_SUCCESS(rc))
        *phEvent = (SUPSEMEVENT)hEvt;

    LogFlow(("pdmR3DevHlp_SUPSemEventCreate: caller='%s'/%d: returns %Rrc *phEvent=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, rc, *phEvent));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventClose} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventClose(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventClose: caller='%s'/%d: hEvent=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, hEvent));

    int rc = RTSemEventDestroy((RTSEMEVENT)hEvent);

    LogFlow(("pdmR3DevHlp_SUPSemEventClose: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventSignal} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventSignal(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventSignal: caller='%s'/%d: hEvent=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, hEvent));

    int rc = RTSemEventSignal((RTSEMEVENT)hEvent);

    LogFlow(("pdmR3DevHlp_SUPSemEventSignal: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventWaitNoResume} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventWaitNoResume(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint32_t cMillies)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventWaitNoResume: caller='%s'/%d: hEvent=%p cNsTimeout=%RU32\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEvent, cMillies));

    int rc = RTSemEventWaitNoResume((RTSEMEVENT)hEvent, cMillies);

    LogFlow(("pdmR3DevHlp_SUPSemEventWaitNoResume: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventWaitNsAbsIntr} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventWaitNsAbsIntr(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint64_t uNsTimeout)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventWaitNsAbsIntr: caller='%s'/%d: hEvent=%p uNsTimeout=%RU64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEvent, uNsTimeout));

    int rc = RTSemEventWait((RTSEMEVENT)hEvent, uNsTimeout / RT_NS_1MS);

    LogFlow(("pdmR3DevHlp_SUPSemEventWaitNsAbsIntr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventWaitNsRelIntr} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventWaitNsRelIntr(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint64_t cNsTimeout)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventWaitNsRelIntr: caller='%s'/%d: hEvent=%p cNsTimeout=%RU64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEvent, cNsTimeout));

    int rc = RTSemEventWait((RTSEMEVENT)hEvent, cNsTimeout / RT_NS_1MS);

    LogFlow(("pdmR3DevHlp_SUPSemEventWaitNsRelIntr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventGetResolution} */
static DECLCALLBACK(uint32_t) pdmR3DevHlp_SUPSemEventGetResolution(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventGetResolution: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    RT_NOREF(pDevIns);
    uint32_t cNsResolution = 0;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_SUPSemEventGetResolution: caller='%s'/%d: returns %u\n", pDevIns->pReg->szName, pDevIns->iInstance, cNsResolution));
    return cNsResolution;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventMultiCreate} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventMultiCreate(PPDMDEVINS pDevIns, PSUPSEMEVENTMULTI phEventMulti)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventMultiCreate: caller='%s'/%d: phEventMulti=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, phEventMulti));

    RT_NOREF(pDevIns, phEventMulti);
#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    LogFlow(("pdmR3DevHlp_SUPSemEventMultiCreate: caller='%s'/%d: returns %Rrc *phEventMulti=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, rc, *phEventMulti));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventMultiClose} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventMultiClose(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventMultiClose: caller='%s'/%d: hEventMulti=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, hEventMulti));

    RT_NOREF(pDevIns, hEventMulti);
#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    LogFlow(("pdmR3DevHlp_SUPSemEventMultiClose: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventMultiSignal} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventMultiSignal(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventMultiSignal: caller='%s'/%d: hEventMulti=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, hEventMulti));

    RT_NOREF(pDevIns, hEventMulti);
#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    LogFlow(("pdmR3DevHlp_SUPSemEventMultiSignal: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventMultiReset} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventMultiReset(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventMultiReset: caller='%s'/%d: hEventMulti=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, hEventMulti));

    RT_NOREF(pDevIns, hEventMulti);
#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    LogFlow(("pdmR3DevHlp_SUPSemEventMultiReset: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventMultiWaitNoResume} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventMultiWaitNoResume(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti,
                                                                  uint32_t cMillies)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventMultiWaitNoResume: caller='%s'/%d: hEventMulti=%p cMillies=%RU32\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEventMulti, cMillies));

    RT_NOREF(pDevIns, hEventMulti, cMillies);
#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif
    LogFlow(("pdmR3DevHlp_SUPSemEventMultiWaitNoResume: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventMultiWaitNsAbsIntr} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventMultiWaitNsAbsIntr(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti,
                                                                   uint64_t uNsTimeout)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventMultiWaitNsAbsIntr: caller='%s'/%d: hEventMulti=%p uNsTimeout=%RU64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEventMulti, uNsTimeout));

    RT_NOREF(pDevIns, hEventMulti, uNsTimeout);
#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    LogFlow(("pdmR3DevHlp_SUPSemEventMultiWaitNsAbsIntr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventMultiWaitNsRelIntr} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventMultiWaitNsRelIntr(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti,
                                                                   uint64_t cNsTimeout)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventMultiWaitNsRelIntr: caller='%s'/%d: hEventMulti=%p cNsTimeout=%RU64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEventMulti, cNsTimeout));

    RT_NOREF(pDevIns, hEventMulti, cNsTimeout);
#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    LogFlow(("pdmR3DevHlp_SUPSemEventMultiWaitNsRelIntr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventMultiGetResolution} */
static DECLCALLBACK(uint32_t) pdmR3DevHlp_SUPSemEventMultiGetResolution(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventMultiGetResolution: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    uint32_t cNsResolution = 0;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_SUPSemEventMultiGetResolution: caller='%s'/%d: returns %u\n", pDevIns->pReg->szName, pDevIns->iInstance, cNsResolution));
    return cNsResolution;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectInit} */
static DECLCALLBACK(int) pdmR3DevHlp_CritSectInit(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, RT_SRC_POS_DECL,
                                                  const char *pszNameFmt, va_list va)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_CritSectInit: caller='%s'/%d: pCritSect=%p pszNameFmt=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pCritSect, pszNameFmt, pszNameFmt));

    RT_NOREF(RT_SRC_POS_ARGS, pszNameFmt, va);
    int rc = RTCritSectInit(&pCritSect->s.CritSect);

    LogFlow(("pdmR3DevHlp_CritSectInit: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectGetNop} */
static DECLCALLBACK(PPDMCRITSECT) pdmR3DevHlp_CritSectGetNop(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    PPDMCRITSECT pCritSect = &pDevIns->Internal.s.pDut->CritSectNop;

    LogFlow(("pdmR3DevHlp_CritSectGetNop: caller='%s'/%d: return %p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pCritSect));
    return pCritSect;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSetDeviceCritSect} */
static DECLCALLBACK(int) pdmR3DevHlp_SetDeviceCritSect(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    /*
     * Validate input.
     */
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertPtrReturn(pCritSect, VERR_INVALID_POINTER);

    LogFlow(("pdmR3DevHlp_SetDeviceCritSect: caller='%s'/%d: pCritSect=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pCritSect));

    pDevIns->pCritSectRoR3 = pCritSect;
    LogFlow(("pdmR3DevHlp_SetDeviceCritSect: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectYield} */
static DECLCALLBACK(bool)     pdmR3DevHlp_CritSectYield(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    RT_NOREF(pDevIns, pCritSect);
    AssertFailed();
    return false;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectEnter} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectEnter(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    RT_NOREF(pDevIns, rcBusy);
    return RTCritSectEnter(&pCritSect->s.CritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectEnterDebug} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectEnterDebug(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, int rcBusy, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    RT_NOREF(pDevIns, rcBusy, uId, RT_SRC_POS_ARGS);
    return RTCritSectEnter(&pCritSect->s.CritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectTryEnter} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectTryEnter(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    RT_NOREF(pDevIns, pCritSect);
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectTryEnterDebug} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectTryEnterDebug(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    RT_NOREF(pDevIns, pCritSect, uId, RT_SRC_POS_ARGS);
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectLeave} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectLeave(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    return RTCritSectLeave(&pCritSect->s.CritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectIsOwner} */
static DECLCALLBACK(bool)     pdmR3DevHlp_CritSectIsOwner(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    return RTCritSectIsOwner(&pCritSect->s.CritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectIsInitialized} */
static DECLCALLBACK(bool)     pdmR3DevHlp_CritSectIsInitialized(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    RT_NOREF(pDevIns, pCritSect);
#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    AssertFailed();
    return false;
#else
    return true;
#endif
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectHasWaiters} */
static DECLCALLBACK(bool)     pdmR3DevHlp_CritSectHasWaiters(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns, pCritSect);
    AssertFailed();
    return false;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectGetRecursion} */
static DECLCALLBACK(uint32_t) pdmR3DevHlp_CritSectGetRecursion(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    RT_NOREF(pDevIns, pCritSect);
    AssertFailed();
    return 0;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectScheduleExitEvent} */
static DECLCALLBACK(int) pdmR3DevHlp_CritSectScheduleExitEvent(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect,
                                                               SUPSEMEVENT hEventToSignal)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    RT_NOREF(pDevIns, pCritSect, hEventToSignal);
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectDelete} */
static DECLCALLBACK(int) pdmR3DevHlp_CritSectDelete(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    RT_NOREF(pDevIns, pCritSect);
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwInit} */
static DECLCALLBACK(int) pdmR3DevHlp_CritSectRwInit(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, RT_SRC_POS_DECL,
                                                    const char *pszNameFmt, va_list va)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_CritSectRwInit: caller='%s'/%d: pCritSect=%p pszNameFmt=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pCritSect, pszNameFmt, pszNameFmt));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed(); RT_NOREF(RT_SRC_POS_ARGS, va);

    LogFlow(("pdmR3DevHlp_CritSectInit: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwDelete} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwDelete(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed(); RT_NOREF(pCritSect);
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwEnterShared} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwEnterShared(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed(); RT_NOREF(pCritSect, rcBusy);
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwEnterSharedDebug} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwEnterSharedDebug(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, int rcBusy,
                                                                     RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed(); RT_NOREF(pCritSect, rcBusy, uId, RT_SRC_POS_ARGS);
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwTryEnterShared} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwTryEnterShared(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed(); RT_NOREF(pCritSect);
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwTryEnterSharedDebug} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwTryEnterSharedDebug(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect,
                                                                        RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed(); RT_NOREF(pCritSect, uId, RT_SRC_POS_ARGS);
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwLeaveShared} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwLeaveShared(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed(); RT_NOREF(pCritSect);
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwEnterExcl} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwEnterExcl(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed(); RT_NOREF(pCritSect, rcBusy);
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwEnterExclDebug} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwEnterExclDebug(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, int rcBusy,
                                                                   RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed(); RT_NOREF(pCritSect, rcBusy, uId, RT_SRC_POS_ARGS);
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwTryEnterExcl} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwTryEnterExcl(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed(); RT_NOREF(pCritSect);
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwTryEnterExclDebug} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwTryEnterExclDebug(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect,
                                                                      RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed(); RT_NOREF(pCritSect, uId, RT_SRC_POS_ARGS);
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwLeaveExcl} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwLeaveExcl(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed(); RT_NOREF(pCritSect);
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwIsWriteOwner} */
static DECLCALLBACK(bool)     pdmR3DevHlp_CritSectRwIsWriteOwner(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed(); RT_NOREF(pCritSect);
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwIsReadOwner} */
static DECLCALLBACK(bool)     pdmR3DevHlp_CritSectRwIsReadOwner(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, bool fWannaHear)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed(); RT_NOREF(pCritSect, fWannaHear);
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwGetWriteRecursion} */
static DECLCALLBACK(uint32_t) pdmR3DevHlp_CritSectRwGetWriteRecursion(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns, pCritSect);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwGetWriterReadRecursion} */
static DECLCALLBACK(uint32_t) pdmR3DevHlp_CritSectRwGetWriterReadRecursion(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns, pCritSect);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwGetReadCount} */
static DECLCALLBACK(uint32_t) pdmR3DevHlp_CritSectRwGetReadCount(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns, pCritSect);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwIsInitialized} */
static DECLCALLBACK(bool)     pdmR3DevHlp_CritSectRwIsInitialized(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns, pCritSect);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) pdmR3DevHlp_ThreadCreate(PPDMDEVINS pDevIns, PPPDMTHREAD ppThread, void *pvUser, PFNPDMTHREADDEV pfnThread,
                                                  PFNPDMTHREADWAKEUPDEV pfnWakeup, size_t cbStack, RTTHREADTYPE enmType, const char *pszName)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_ThreadCreate: caller='%s'/%d: ppThread=%p pvUser=%p pfnThread=%p pfnWakeup=%p cbStack=%#zx enmType=%d pszName=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, ppThread, pvUser, pfnThread, pfnWakeup, cbStack, enmType, pszName, pszName));

    int rc = tstDevPdmR3ThreadCreateDevice(pDevIns->Internal.s.pDut, pDevIns, ppThread, pvUser, pfnThread, pfnWakeup, cbStack, enmType, pszName);

    LogFlow(("pdmR3DevHlp_ThreadCreate: caller='%s'/%d: returns %Rrc *ppThread=%RTthrd\n", pDevIns->pReg->szName, pDevIns->iInstance,
            rc, *ppThread));
    return rc;
}


static DECLCALLBACK(int) pdmR3DevHlp_ThreadDestroy(PPDMTHREAD pThread, int *pRcThread)
{
    return tstDevPdmR3ThreadDestroy(pThread, pRcThread);
}


static DECLCALLBACK(int) pdmR3DevHlp_ThreadIAmSuspending(PPDMTHREAD pThread)
{
    return tstDevPdmR3ThreadIAmSuspending(pThread);
}


static DECLCALLBACK(int) pdmR3DevHlp_ThreadIamRunning(PPDMTHREAD pThread)
{
    return tstDevPdmR3ThreadIAmRunning(pThread);
}


static DECLCALLBACK(int) pdmR3DevHlp_ThreadSleep(PPDMTHREAD pThread, RTMSINTERVAL cMillies)
{
    return tstDevPdmR3ThreadSleep(pThread, cMillies);
}


static DECLCALLBACK(int) pdmR3DevHlp_ThreadSuspend(PPDMTHREAD pThread)
{
    return tstDevPdmR3ThreadSuspend(pThread);
}


static DECLCALLBACK(int) pdmR3DevHlp_ThreadResume(PPDMTHREAD pThread)
{
    return tstDevPdmR3ThreadResume(pThread);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSetAsyncNotification} */
static DECLCALLBACK(int) pdmR3DevHlp_SetAsyncNotification(PPDMDEVINS pDevIns, PFNPDMDEVASYNCNOTIFY pfnAsyncNotify)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SetAsyncNotification: caller='%s'/%d: pfnAsyncNotify=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pfnAsyncNotify));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertStmt(pfnAsyncNotify, rc = VERR_INVALID_PARAMETER);
#if 0
    AssertStmt(!pDevIns->Internal.s.pfnAsyncNotify, rc = VERR_WRONG_ORDER);
    AssertStmt(pDevIns->Internal.s.fIntFlags & (PDMDEVINSINT_FLAGS_SUSPENDED | PDMDEVINSINT_FLAGS_RESET), rc = VERR_WRONG_ORDER);
#endif

    AssertFailed();

    LogFlow(("pdmR3DevHlp_SetAsyncNotification: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnAsyncNotificationCompleted} */
static DECLCALLBACK(void) pdmR3DevHlp_AsyncNotificationCompleted(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    AssertFailed();
}


/** @interface_method_impl{PDMDEVHLPR3,pfnRTCRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_RTCRegister(PPDMDEVINS pDevIns, PCPDMRTCREG pRtcReg, PCPDMRTCHLP *ppRtcHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_RTCRegister: caller='%s'/%d: pRtcReg=%p:{.u32Version=%#x, .pfnWrite=%p, .pfnRead=%p} ppRtcHlp=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pRtcReg, pRtcReg->u32Version, pRtcReg->pfnWrite,
             pRtcReg->pfnWrite, ppRtcHlp));

    /*
     * Validate input.
     */
    if (pRtcReg->u32Version != PDM_RTCREG_VERSION)
    {
        AssertMsgFailed(("u32Version=%#x expected %#x\n", pRtcReg->u32Version,
                         PDM_RTCREG_VERSION));
        LogFlow(("pdmR3DevHlp_RTCRegister: caller='%s'/%d: returns %Rrc (version)\n",
                 pDevIns->pReg->szName, pDevIns->iInstance, VERR_INVALID_PARAMETER));
        return VERR_INVALID_PARAMETER;
    }
    if (    !pRtcReg->pfnWrite
        ||  !pRtcReg->pfnRead)
    {
        Assert(pRtcReg->pfnWrite);
        Assert(pRtcReg->pfnRead);
        LogFlow(("pdmR3DevHlp_RTCRegister: caller='%s'/%d: returns %Rrc (callbacks)\n",
                 pDevIns->pReg->szName, pDevIns->iInstance, VERR_INVALID_PARAMETER));
        return VERR_INVALID_PARAMETER;
    }

    if (!ppRtcHlp)
    {
        Assert(ppRtcHlp);
        LogFlow(("pdmR3DevHlp_RTCRegister: caller='%s'/%d: returns %Rrc (ppRtcHlp)\n",
                 pDevIns->pReg->szName, pDevIns->iInstance, VERR_INVALID_PARAMETER));
        return VERR_INVALID_PARAMETER;
    }

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_RTCRegister: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDMARegister} */
static DECLCALLBACK(int) pdmR3DevHlp_DMARegister(PPDMDEVINS pDevIns, unsigned uChannel, PFNDMATRANSFERHANDLER pfnTransferHandler, void *pvUser)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DMARegister: caller='%s'/%d: uChannel=%d pfnTransferHandler=%p pvUser=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, uChannel, pfnTransferHandler, pvUser));

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    LogFlow(("pdmR3DevHlp_DMARegister: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDMAReadMemory} */
static DECLCALLBACK(int) pdmR3DevHlp_DMAReadMemory(PPDMDEVINS pDevIns, unsigned uChannel, void *pvBuffer, uint32_t off, uint32_t cbBlock, uint32_t *pcbRead)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DMAReadMemory: caller='%s'/%d: uChannel=%d pvBuffer=%p off=%#x cbBlock=%#x pcbRead=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, uChannel, pvBuffer, off, cbBlock, pcbRead));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_DMAReadMemory: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDMAWriteMemory} */
static DECLCALLBACK(int) pdmR3DevHlp_DMAWriteMemory(PPDMDEVINS pDevIns, unsigned uChannel, const void *pvBuffer, uint32_t off, uint32_t cbBlock, uint32_t *pcbWritten)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DMAWriteMemory: caller='%s'/%d: uChannel=%d pvBuffer=%p off=%#x cbBlock=%#x pcbWritten=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, uChannel, pvBuffer, off, cbBlock, pcbWritten));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_DMAWriteMemory: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDMASetDREQ} */
static DECLCALLBACK(int) pdmR3DevHlp_DMASetDREQ(PPDMDEVINS pDevIns, unsigned uChannel, unsigned uLevel)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DMASetDREQ: caller='%s'/%d: uChannel=%d uLevel=%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance, uChannel, uLevel));

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    LogFlow(("pdmR3DevHlp_DMASetDREQ: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}

/** @interface_method_impl{PDMDEVHLPR3,pfnDMAGetChannelMode} */
static DECLCALLBACK(uint8_t) pdmR3DevHlp_DMAGetChannelMode(PPDMDEVINS pDevIns, unsigned uChannel)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DMAGetChannelMode: caller='%s'/%d: uChannel=%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance, uChannel));

    uint8_t u8Mode = (3 << 2); /* Invalid mode. */
    AssertFailed();

    LogFlow(("pdmR3DevHlp_DMAGetChannelMode: caller='%s'/%d: returns %#04x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, u8Mode));
    return u8Mode;
}

/** @interface_method_impl{PDMDEVHLPR3,pfnDMASchedule} */
static DECLCALLBACK(void) pdmR3DevHlp_DMASchedule(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DMASchedule: caller='%s'/%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    AssertFailed();
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCMOSWrite} */
static DECLCALLBACK(int) pdmR3DevHlp_CMOSWrite(PPDMDEVINS pDevIns, unsigned iReg, uint8_t u8Value)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_CMOSWrite: caller='%s'/%d: iReg=%#04x u8Value=%#04x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, iReg, u8Value));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_CMOSWrite: caller='%s'/%d: return %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCMOSRead} */
static DECLCALLBACK(int) pdmR3DevHlp_CMOSRead(PPDMDEVINS pDevIns, unsigned iReg, uint8_t *pu8Value)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_CMOSWrite: caller='%s'/%d: iReg=%#04x pu8Value=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, iReg, pu8Value));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_CMOSWrite: caller='%s'/%d: return %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnAssertEMT} */
static DECLCALLBACK(bool) pdmR3DevHlp_AssertEMT(PPDMDEVINS pDevIns, const char *pszFile, unsigned iLine, const char *pszFunction)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    char szMsg[100];
    RTStrPrintf(szMsg, sizeof(szMsg), "AssertEMT '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance);
    RTAssertMsg1Weak(szMsg, iLine, pszFile, pszFunction);
    AssertBreakpoint();
    return false;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnAssertOther} */
static DECLCALLBACK(bool) pdmR3DevHlp_AssertOther(PPDMDEVINS pDevIns, const char *pszFile, unsigned iLine, const char *pszFunction)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    char szMsg[100];
    RTStrPrintf(szMsg, sizeof(szMsg), "AssertOther '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance);
    RTAssertMsg1Weak(szMsg, iLine, pszFile, pszFunction);
    AssertBreakpoint();
    return false;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnLdrGetRCInterfaceSymbols} */
static DECLCALLBACK(int) pdmR3DevHlp_LdrGetRCInterfaceSymbols(PPDMDEVINS pDevIns, void *pvInterface, size_t cbInterface,
                                                              const char *pszSymPrefix, const char *pszSymList)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PDMLdrGetRCInterfaceSymbols: caller='%s'/%d: pvInterface=%p cbInterface=%zu pszSymPrefix=%p:{%s} pszSymList=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pvInterface, cbInterface, pszSymPrefix, pszSymPrefix, pszSymList, pszSymList));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_PDMLdrGetRCInterfaceSymbols: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName,
             pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnLdrGetR0InterfaceSymbols} */
static DECLCALLBACK(int) pdmR3DevHlp_LdrGetR0InterfaceSymbols(PPDMDEVINS pDevIns, void *pvInterface, size_t cbInterface,
                                                              const char *pszSymPrefix, const char *pszSymList)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PDMLdrGetR0InterfaceSymbols: caller='%s'/%d: pvInterface=%p cbInterface=%zu pszSymPrefix=%p:{%s} pszSymList=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pvInterface, cbInterface, pszSymPrefix, pszSymPrefix, pszSymList, pszSymList));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_PDMLdrGetR0InterfaceSymbols: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName,
             pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCallR0} */
static DECLCALLBACK(int) pdmR3DevHlp_CallR0(PPDMDEVINS pDevIns, uint32_t uOperation, uint64_t u64Arg)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_CallR0: caller='%s'/%d: uOperation=%#x u64Arg=%#RX64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, uOperation, u64Arg));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_CallR0: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName,
             pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMGetSuspendReason} */
static DECLCALLBACK(VMSUSPENDREASON) pdmR3DevHlp_VMGetSuspendReason(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    VMSUSPENDREASON enmReason = VMSUSPENDREASON_INVALID;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_VMGetSuspendReason: caller='%s'/%d: returns %d\n",
             pDevIns->pReg->szName, pDevIns->iInstance, enmReason));
    return enmReason;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMGetResumeReason} */
static DECLCALLBACK(VMRESUMEREASON) pdmR3DevHlp_VMGetResumeReason(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    VMRESUMEREASON enmReason = VMRESUMEREASON_INVALID;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_VMGetResumeReason: caller='%s'/%d: returns %d\n",
             pDevIns->pReg->szName, pDevIns->iInstance, enmReason));
    return enmReason;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetUVM} */
static DECLCALLBACK(PUVM) pdmR3DevHlp_GetUVM(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    AssertFailed();
    LogFlow(("pdmR3DevHlp_GetUVM: caller='%s'/%d: returns %p\n", pDevIns->pReg->szName, pDevIns->iInstance, NULL));
    return NULL;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetVM} */
static DECLCALLBACK(PVMCC) pdmR3DevHlp_GetVM(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    LogFlow(("pdmR3DevHlp_GetVM: caller='%s'/%d: returns %p\n", pDevIns->pReg->szName, pDevIns->iInstance, pDevIns->Internal.s.pDut->pVm));
    return pDevIns->Internal.s.pDut->pVm;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetVMCPU} */
static DECLCALLBACK(PVMCPU) pdmR3DevHlp_GetVMCPU(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    AssertFailed();
    LogFlow(("pdmR3DevHlp_GetVMCPU: caller='%s'/%d for CPU %u\n", pDevIns->pReg->szName, pDevIns->iInstance, NULL));
    return NULL;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetCurrentCpuId} */
static DECLCALLBACK(VMCPUID) pdmR3DevHlp_GetCurrentCpuId(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    VMCPUID idCpu = 0;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_GetCurrentCpuId: caller='%s'/%d for CPU %u\n", pDevIns->pReg->szName, pDevIns->iInstance, idCpu));
    return idCpu;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIBusRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_PCIBusRegister(PPDMDEVINS pDevIns, PPDMPCIBUSREGR3 pPciBusReg,
                                                    PCPDMPCIHLPR3 *ppPciHlp, uint32_t *piBus)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PCIBusRegister: caller='%s'/%d: pPciBusReg=%p:{.u32Version=%#x, .pfnRegisterR3=%p, .pfnIORegionRegisterR3=%p, "
             ".pfnInterceptConfigAccesses=%p, pfnConfigRead=%p, pfnConfigWrite=%p, .pfnSetIrqR3=%p, .u32EndVersion=%#x} ppPciHlpR3=%p piBus=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPciBusReg, pPciBusReg->u32Version, pPciBusReg->pfnRegisterR3,
             pPciBusReg->pfnIORegionRegisterR3, pPciBusReg->pfnInterceptConfigAccesses, pPciBusReg->pfnConfigRead,
             pPciBusReg->pfnConfigWrite, pPciBusReg->pfnSetIrqR3, pPciBusReg->u32EndVersion, ppPciHlp, piBus));

    /*
     * Validate the structure and output parameters.
     */
    AssertLogRelMsgReturn(pPciBusReg->u32Version == PDM_PCIBUSREGR3_VERSION,
                          ("u32Version=%#x expected %#x\n", pPciBusReg->u32Version, PDM_PCIBUSREGR3_VERSION),
                          VERR_INVALID_PARAMETER);
    AssertPtrReturn(pPciBusReg->pfnRegisterR3, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pPciBusReg->pfnRegisterMsiR3, VERR_INVALID_POINTER);
    AssertPtrReturn(pPciBusReg->pfnIORegionRegisterR3, VERR_INVALID_POINTER);
    AssertPtrReturn(pPciBusReg->pfnInterceptConfigAccesses, VERR_INVALID_POINTER);
    AssertPtrReturn(pPciBusReg->pfnConfigWrite, VERR_INVALID_POINTER);
    AssertPtrReturn(pPciBusReg->pfnConfigRead, VERR_INVALID_POINTER);
    AssertPtrReturn(pPciBusReg->pfnSetIrqR3, VERR_INVALID_POINTER);
    AssertLogRelMsgReturn(pPciBusReg->u32EndVersion == PDM_PCIBUSREGR3_VERSION,
                          ("u32Version=%#x expected %#x\n", pPciBusReg->u32Version, PDM_PCIBUSREGR3_VERSION),
                          VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppPciHlp, VERR_INVALID_POINTER);
    AssertPtrNullReturn(piBus, VERR_INVALID_POINTER);

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    Log(("PDM: Registered PCI bus device '%s'/%d pDevIns=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pDevIns));

    LogFlow(("pdmR3DevHlp_PCIBusRegister: caller='%s'/%d: returns %Rrc *piBus=%u\n", pDevIns->pReg->szName, pDevIns->iInstance, rc, *piBus));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnIommuRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_IommuRegister(PPDMDEVINS pDevIns, PPDMIOMMUREGR3 pIommuReg, PCPDMIOMMUHLPR3 *ppIommuHlp,
                                                   uint32_t *pidxIommu)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_IommuRegister: caller='%s'/%d: pIommuReg=%p:{.u32Version=%#x, .u32TheEnd=%#x } ppIommuHlp=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pIommuReg, pIommuReg->u32Version, pIommuReg->u32TheEnd, ppIommuHlp));

    /*
     * Validate input.
     */
    AssertMsgReturn(pIommuReg->u32Version == PDM_IOMMUREGR3_VERSION,
                    ("%s/%d: u32Version=%#x expected %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, pIommuReg->u32Version, PDM_IOMMUREGR3_VERSION),
                    VERR_INVALID_PARAMETER);
    AssertPtrReturn(pIommuReg->pfnMsiRemap, VERR_INVALID_POINTER);
    AssertMsgReturn(pIommuReg->u32TheEnd == PDM_IOMMUREGR3_VERSION,
                    ("%s/%d: u32TheEnd=%#x expected %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, pIommuReg->u32TheEnd, PDM_IOMMUREGR3_VERSION),
                    VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppIommuHlp, VERR_INVALID_POINTER);

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed(); RT_NOREF(pidxIommu);
    Log(("PDM: Registered IOMMU device '%s'/%d pDevIns=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pDevIns));

    LogFlow(("pdmR3DevHlp_IommuRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPICRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_PICRegister(PPDMDEVINS pDevIns, PPDMPICREG pPicReg, PCPDMPICHLP *ppPicHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PICRegister: caller='%s'/%d: pPicReg=%p:{.u32Version=%#x, .pfnSetIrq=%p, .pfnGetInterrupt=%p, .u32TheEnd=%#x } ppPicHlp=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPicReg, pPicReg->u32Version, pPicReg->pfnSetIrq, pPicReg->pfnGetInterrupt, pPicReg->u32TheEnd, ppPicHlp));

    /*
     * Validate input.
     */
    AssertMsgReturn(pPicReg->u32Version == PDM_PICREG_VERSION,
                    ("%s/%d: u32Version=%#x expected %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, pPicReg->u32Version, PDM_PICREG_VERSION),
                    VERR_INVALID_PARAMETER);
    AssertPtrReturn(pPicReg->pfnSetIrq, VERR_INVALID_POINTER);
    AssertPtrReturn(pPicReg->pfnGetInterrupt, VERR_INVALID_POINTER);
    AssertMsgReturn(pPicReg->u32TheEnd == PDM_PICREG_VERSION,
                    ("%s/%d: u32TheEnd=%#x expected %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, pPicReg->u32TheEnd, PDM_PICREG_VERSION),
                    VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppPicHlp, VERR_INVALID_POINTER);

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
    Log(("PDM: Registered PIC device '%s'/%d pDevIns=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pDevIns));

    LogFlow(("pdmR3DevHlp_PICRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnApicRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_ApicRegister(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_ApicRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnIoApicRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_IoApicRegister(PPDMDEVINS pDevIns, PPDMIOAPICREG pIoApicReg, PCPDMIOAPICHLP *ppIoApicHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_IoApicRegister: caller='%s'/%d: pIoApicReg=%p:{.u32Version=%#x, .pfnSetIrq=%p, .pfnSendMsi=%p, .pfnSetEoi=%p, .u32TheEnd=%#x } ppIoApicHlp=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pIoApicReg, pIoApicReg->u32Version, pIoApicReg->pfnSetIrq, pIoApicReg->pfnSendMsi, pIoApicReg->pfnSetEoi, pIoApicReg->u32TheEnd, ppIoApicHlp));

    /*
     * Validate input.
     */
    AssertMsgReturn(pIoApicReg->u32Version == PDM_IOAPICREG_VERSION,
                    ("%s/%d: u32Version=%#x expected %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, pIoApicReg->u32Version, PDM_IOAPICREG_VERSION),
                    VERR_VERSION_MISMATCH);
    AssertPtrReturn(pIoApicReg->pfnSetIrq, VERR_INVALID_POINTER);
    AssertPtrReturn(pIoApicReg->pfnSendMsi, VERR_INVALID_POINTER);
    AssertPtrReturn(pIoApicReg->pfnSetEoi, VERR_INVALID_POINTER);
    AssertMsgReturn(pIoApicReg->u32TheEnd == PDM_IOAPICREG_VERSION,
                    ("%s/%d: u32TheEnd=%#x expected %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, pIoApicReg->u32TheEnd, PDM_IOAPICREG_VERSION),
                    VERR_VERSION_MISMATCH);
    AssertPtrReturn(ppIoApicHlp, VERR_INVALID_POINTER);

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_IoApicRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnHpetRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_HpetRegister(PPDMDEVINS pDevIns, PPDMHPETREG pHpetReg, PCPDMHPETHLPR3 *ppHpetHlpR3)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_HpetRegister: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    /*
     * Validate input.
     */
    AssertMsgReturn(pHpetReg->u32Version == PDM_HPETREG_VERSION,
                    ("%s/%u: u32Version=%#x expected %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, pHpetReg->u32Version, PDM_HPETREG_VERSION),
                    VERR_VERSION_MISMATCH);
    AssertPtrReturn(ppHpetHlpR3, VERR_INVALID_POINTER);

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_HpetRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPciRawRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_PciRawRegister(PPDMDEVINS pDevIns, PPDMPCIRAWREG pPciRawReg, PCPDMPCIRAWHLPR3 *ppPciRawHlpR3)
{
    PDMDEV_ASSERT_DEVINS(pDevIns); RT_NOREF_PV(pDevIns);
    LogFlow(("pdmR3DevHlp_PciRawRegister: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    /*
     * Validate input.
     */
    if (pPciRawReg->u32Version != PDM_PCIRAWREG_VERSION)
    {
        AssertMsgFailed(("u32Version=%#x expected %#x\n", pPciRawReg->u32Version, PDM_PCIRAWREG_VERSION));
        LogFlow(("pdmR3DevHlp_PciRawRegister: caller='%s'/%d: returns %Rrc (version)\n", pDevIns->pReg->szName, pDevIns->iInstance, VERR_INVALID_PARAMETER));
        return VERR_INVALID_PARAMETER;
    }

    if (!ppPciRawHlpR3)
    {
        Assert(ppPciRawHlpR3);
        LogFlow(("pdmR3DevHlp_PciRawRegister: caller='%s'/%d: returns %Rrc (ppPciRawHlpR3)\n", pDevIns->pReg->szName, pDevIns->iInstance, VERR_INVALID_PARAMETER));
        return VERR_INVALID_PARAMETER;
    }

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_PciRawRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDMACRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_DMACRegister(PPDMDEVINS pDevIns, PPDMDMACREG pDmacReg, PCPDMDMACHLP *ppDmacHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DMACRegister: caller='%s'/%d: pDmacReg=%p:{.u32Version=%#x, .pfnRun=%p, .pfnRegister=%p, .pfnReadMemory=%p, .pfnWriteMemory=%p, .pfnSetDREQ=%p, .pfnGetChannelMode=%p} ppDmacHlp=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pDmacReg, pDmacReg->u32Version, pDmacReg->pfnRun, pDmacReg->pfnRegister,
             pDmacReg->pfnReadMemory, pDmacReg->pfnWriteMemory, pDmacReg->pfnSetDREQ, pDmacReg->pfnGetChannelMode, ppDmacHlp));

    /*
     * Validate input.
     */
    if (pDmacReg->u32Version != PDM_DMACREG_VERSION)
    {
        AssertMsgFailed(("u32Version=%#x expected %#x\n", pDmacReg->u32Version,
                         PDM_DMACREG_VERSION));
        LogFlow(("pdmR3DevHlp_DMACRegister: caller='%s'/%d: returns %Rrc (version)\n",
                 pDevIns->pReg->szName, pDevIns->iInstance, VERR_INVALID_PARAMETER));
        return VERR_INVALID_PARAMETER;
    }
    if (    !pDmacReg->pfnRun
        ||  !pDmacReg->pfnRegister
        ||  !pDmacReg->pfnReadMemory
        ||  !pDmacReg->pfnWriteMemory
        ||  !pDmacReg->pfnSetDREQ
        ||  !pDmacReg->pfnGetChannelMode)
    {
        Assert(pDmacReg->pfnRun);
        Assert(pDmacReg->pfnRegister);
        Assert(pDmacReg->pfnReadMemory);
        Assert(pDmacReg->pfnWriteMemory);
        Assert(pDmacReg->pfnSetDREQ);
        Assert(pDmacReg->pfnGetChannelMode);
        LogFlow(("pdmR3DevHlp_DMACRegister: caller='%s'/%d: returns %Rrc (callbacks)\n",
                 pDevIns->pReg->szName, pDevIns->iInstance, VERR_INVALID_PARAMETER));
        return VERR_INVALID_PARAMETER;
    }

    if (!ppDmacHlp)
    {
        Assert(ppDmacHlp);
        LogFlow(("pdmR3DevHlp_DMACRegister: caller='%s'/%d: returns %Rrc (ppDmacHlp)\n",
                 pDevIns->pReg->szName, pDevIns->iInstance, VERR_INVALID_PARAMETER));
        return VERR_INVALID_PARAMETER;
    }

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_DMACRegister: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/**
 * @copydoc PDMDEVHLPR3::pfnRegisterVMMDevHeap
 */
static DECLCALLBACK(int) pdmR3DevHlp_RegisterVMMDevHeap(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, RTR3PTR pvHeap, unsigned cbHeap)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_RegisterVMMDevHeap: caller='%s'/%d: GCPhys=%RGp pvHeap=%p cbHeap=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPhys, pvHeap, cbHeap));

#ifndef VBOX_TSTDEV_NOT_IMPLEMENTED_STUBS_FAKE_SUCCESS
    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();
#else
    int rc = VINF_SUCCESS;
#endif

    LogFlow(("pdmR3DevHlp_RegisterVMMDevHeap: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/**
 * @interface_method_impl{PDMDEVHLPR3,pfnFirmwareRegister}
 */
static DECLCALLBACK(int) pdmR3DevHlp_FirmwareRegister(PPDMDEVINS pDevIns, PCPDMFWREG pFwReg, PCPDMFWHLPR3 *ppFwHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_FirmwareRegister: caller='%s'/%d: pFWReg=%p:{.u32Version=%#x, .pfnIsHardReset=%p, .u32TheEnd=%#x} ppFwHlp=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pFwReg, pFwReg->u32Version, pFwReg->pfnIsHardReset, pFwReg->u32TheEnd, ppFwHlp));

    /*
     * Validate input.
     */
    if (pFwReg->u32Version != PDM_FWREG_VERSION)
    {
        AssertMsgFailed(("u32Version=%#x expected %#x\n", pFwReg->u32Version, PDM_FWREG_VERSION));
        LogFlow(("pdmR3DevHlp_FirmwareRegister: caller='%s'/%d: returns %Rrc (version)\n",
                 pDevIns->pReg->szName, pDevIns->iInstance, VERR_INVALID_PARAMETER));
        return VERR_INVALID_PARAMETER;
    }
    if (!pFwReg->pfnIsHardReset)
    {
        Assert(pFwReg->pfnIsHardReset);
        LogFlow(("pdmR3DevHlp_FirmwareRegister: caller='%s'/%d: returns %Rrc (callbacks)\n",
                 pDevIns->pReg->szName, pDevIns->iInstance, VERR_INVALID_PARAMETER));
        return VERR_INVALID_PARAMETER;
    }

    if (!ppFwHlp)
    {
        Assert(ppFwHlp);
        LogFlow(("pdmR3DevHlp_FirmwareRegister: caller='%s'/%d: returns %Rrc (ppFwHlp)\n",
                 pDevIns->pReg->szName, pDevIns->iInstance, VERR_INVALID_PARAMETER));
        return VERR_INVALID_PARAMETER;
    }

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_FirmwareRegister: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMReset} */
static DECLCALLBACK(int) pdmR3DevHlp_VMReset(PPDMDEVINS pDevIns, uint32_t fFlags)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_VMReset: caller='%s'/%d: fFlags=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, fFlags));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_VMReset: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMSuspend} */
static DECLCALLBACK(int) pdmR3DevHlp_VMSuspend(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_VMSuspend: caller='%s'/%d:\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_VMSuspend: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMSuspendSaveAndPowerOff} */
static DECLCALLBACK(int) pdmR3DevHlp_VMSuspendSaveAndPowerOff(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_VMSuspendSaveAndPowerOff: caller='%s'/%d:\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_VMSuspendSaveAndPowerOff: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMPowerOff} */
static DECLCALLBACK(int) pdmR3DevHlp_VMPowerOff(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_VMPowerOff: caller='%s'/%d:\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_VMPowerOff: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnA20IsEnabled} */
static DECLCALLBACK(bool) pdmR3DevHlp_A20IsEnabled(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    bool fRc = false;
    AssertFailed();

    LogFlow(("pdmR3DevHlp_A20IsEnabled: caller='%s'/%d: returns %d\n", pDevIns->pReg->szName, pDevIns->iInstance, fRc));
    return fRc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnA20Set} */
static DECLCALLBACK(void) pdmR3DevHlp_A20Set(PPDMDEVINS pDevIns, bool fEnable)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_A20Set: caller='%s'/%d: fEnable=%d\n", pDevIns->pReg->szName, pDevIns->iInstance, fEnable));
    AssertFailed();
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetCpuId} */
static DECLCALLBACK(void) pdmR3DevHlp_GetCpuId(PPDMDEVINS pDevIns, uint32_t iLeaf,
                                               uint32_t *pEax, uint32_t *pEbx, uint32_t *pEcx, uint32_t *pEdx)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_GetCpuId: caller='%s'/%d: iLeaf=%d pEax=%p pEbx=%p pEcx=%p pEdx=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, iLeaf, pEax, pEbx, pEcx, pEdx));
    AssertPtr(pEax); AssertPtr(pEbx); AssertPtr(pEcx); AssertPtr(pEdx);

    AssertFailed();

    LogFlow(("pdmR3DevHlp_GetCpuId: caller='%s'/%d: returns void - *pEax=%#x *pEbx=%#x *pEcx=%#x *pEdx=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, *pEax, *pEbx, *pEcx, *pEdx));
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetMainExecutionEngine} */
static DECLCALLBACK(uint8_t) pdmR3DevHlp_GetMainExecutionEngine(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    //VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DevHlp_GetMainExecutionEngine: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return VM_EXEC_ENGINE_NOT_SET;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPGMHandlerPhysicalRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_PGMHandlerPhysicalRegister(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, RTGCPHYS GCPhysLast,
                                                                PGMPHYSHANDLERTYPE hType, RTR3PTR pvUserR3, RTR0PTR pvUserR0,
                                                                RTRCPTR pvUserRC, R3PTRTYPE(const char *) pszDesc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(GCPhys, GCPhysLast, hType, pvUserR3, pvUserR0, pvUserRC, pszDesc);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPGMHandlerPhysicalDeregister} */
static DECLCALLBACK(int) pdmR3DevHlp_PGMHandlerPhysicalDeregister(PPDMDEVINS pDevIns, RTGCPHYS GCPhys)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(GCPhys);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPGMHandlerPhysicalPageTempOff} */
static DECLCALLBACK(int) pdmR3DevHlp_PGMHandlerPhysicalPageTempOff(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, RTGCPHYS GCPhysPage)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(GCPhys, GCPhysPage);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPGMHandlerPhysicalReset} */
static DECLCALLBACK(int) pdmR3DevHlp_PGMHandlerPhysicalReset(PPDMDEVINS pDevIns, RTGCPHYS GCPhys)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(GCPhys);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMMRegisterPatchMemory} */
static DECLCALLBACK(int) pdmR3DevHlp_VMMRegisterPatchMemory(PPDMDEVINS pDevIns, RTGCPTR GCPtrPatchMem, uint32_t cbPatchMem)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(GCPtrPatchMem, cbPatchMem);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMMDeregisterPatchMemory} */
static DECLCALLBACK(int) pdmR3DevHlp_VMMDeregisterPatchMemory(PPDMDEVINS pDevIns, RTGCPTR GCPtrPatchMem, uint32_t cbPatchMem)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(GCPtrPatchMem, cbPatchMem);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSharedModuleRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_SharedModuleRegister(PPDMDEVINS pDevIns, VBOXOSFAMILY enmGuestOS, char *pszModuleName, char *pszVersion,
                                                                    RTGCPTR GCBaseAddr, uint32_t cbModule,
                                                                    uint32_t cRegions, VMMDEVSHAREDREGIONDESC const *paRegions)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(enmGuestOS, pszModuleName, pszVersion, GCBaseAddr, cbModule, cRegions, paRegions);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSharedModuleUnregister} */
static DECLCALLBACK(int) pdmR3DevHlp_SharedModuleUnregister(PPDMDEVINS pDevIns, char *pszModuleName, char *pszVersion,
                                                                      RTGCPTR GCBaseAddr, uint32_t cbModule)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pszModuleName, pszVersion, GCBaseAddr, cbModule);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSharedModuleGetPageState} */
static DECLCALLBACK(int) pdmR3DevHlp_SharedModuleGetPageState(PPDMDEVINS pDevIns, RTGCPTR GCPtrPage, bool *pfShared, uint64_t *pfPageFlags)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(GCPtrPage, pfShared, pfPageFlags);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSharedModuleCheckAll} */
static DECLCALLBACK(int) pdmR3DevHlp_SharedModuleCheckAll(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnQueryLun} */
static DECLCALLBACK(int) pdmR3DevHlp_QueryLun(PPDMDEVINS pDevIns, const char *pszDevice, unsigned iInstance, unsigned iLun, PPDMIBASE *ppBase)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pszDevice, iInstance, iLun, ppBase);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGIMDeviceRegister} */
static DECLCALLBACK(void) pdmR3DevHlp_GIMDeviceRegister(PPDMDEVINS pDevIns, PGIMDEBUG pDbg)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDbg);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGIMGetDebugSetup} */
static DECLCALLBACK(int) pdmR3DevHlp_GIMGetDebugSetup(PPDMDEVINS pDevIns, PGIMDEBUGSETUP pDbgSetup)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDbgSetup);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGIMGetMmio2Regions} */
static DECLCALLBACK(PGIMMMIO2REGION) pdmR3DevHlp_GIMGetMmio2Regions(PPDMDEVINS pDevIns, uint32_t *pcRegions)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pcRegions);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return NULL;
}

const PDMDEVHLPR3 g_tstDevPdmDevHlpR3 =
{
    PDM_DEVHLPR3_VERSION,
    pdmR3DevHlp_IoPortCreateEx,
    pdmR3DevHlp_IoPortMap,
    pdmR3DevHlp_IoPortUnmap,
    pdmR3DevHlp_IoPortGetMappingAddress,
    pdmR3DevHlp_IoPortWrite,
    pdmR3DevHlp_MmioCreateEx,
    pdmR3DevHlp_MmioMap,
    pdmR3DevHlp_MmioUnmap,
    pdmR3DevHlp_MmioReduce,
    pdmR3DevHlp_MmioGetMappingAddress,
    pdmR3DevHlp_Mmio2Create,
    pdmR3DevHlp_Mmio2Destroy,
    pdmR3DevHlp_Mmio2Map,
    pdmR3DevHlp_Mmio2Unmap,
    pdmR3DevHlp_Mmio2Reduce,
    pdmR3DevHlp_Mmio2GetMappingAddress,
    pdmR3DevHlp_Mmio2QueryAndResetDirtyBitmap,
    pdmR3DevHlp_Mmio2ControlDirtyPageTracking,
    pdmR3DevHlp_Mmio2ChangeRegionNo,
    pdmR3DevHlp_MmioMapMmio2Page,
    pdmR3DevHlp_MmioResetRegion,
    pdmR3DevHlp_ROMRegister,
    pdmR3DevHlp_ROMProtectShadow,
    pdmR3DevHlp_SSMRegister,
    pdmR3DevHlp_SSMRegisterLegacy,
    pdmR3DevHlp_SSMPutStruct,
    pdmR3DevHlp_SSMPutStructEx,
    pdmR3DevHlp_SSMPutBool,
    pdmR3DevHlp_SSMPutU8,
    pdmR3DevHlp_SSMPutS8,
    pdmR3DevHlp_SSMPutU16,
    pdmR3DevHlp_SSMPutS16,
    pdmR3DevHlp_SSMPutU32,
    pdmR3DevHlp_SSMPutS32,
    pdmR3DevHlp_SSMPutU64,
    pdmR3DevHlp_SSMPutS64,
    pdmR3DevHlp_SSMPutU128,
    pdmR3DevHlp_SSMPutS128,
    pdmR3DevHlp_SSMPutUInt,
    pdmR3DevHlp_SSMPutSInt,
    pdmR3DevHlp_SSMPutGCUInt,
    pdmR3DevHlp_SSMPutGCUIntReg,
    pdmR3DevHlp_SSMPutGCPhys32,
    pdmR3DevHlp_SSMPutGCPhys64,
    pdmR3DevHlp_SSMPutGCPhys,
    pdmR3DevHlp_SSMPutGCPtr,
    pdmR3DevHlp_SSMPutGCUIntPtr,
    pdmR3DevHlp_SSMPutRCPtr,
    pdmR3DevHlp_SSMPutIOPort,
    pdmR3DevHlp_SSMPutSel,
    pdmR3DevHlp_SSMPutMem,
    pdmR3DevHlp_SSMPutStrZ,
    pdmR3DevHlp_SSMGetStruct,
    pdmR3DevHlp_SSMGetStructEx,
    pdmR3DevHlp_SSMGetBool,
    pdmR3DevHlp_SSMGetBoolV,
    pdmR3DevHlp_SSMGetU8,
    pdmR3DevHlp_SSMGetU8V,
    pdmR3DevHlp_SSMGetS8,
    pdmR3DevHlp_SSMGetS8V,
    pdmR3DevHlp_SSMGetU16,
    pdmR3DevHlp_SSMGetU16V,
    pdmR3DevHlp_SSMGetS16,
    pdmR3DevHlp_SSMGetS16V,
    pdmR3DevHlp_SSMGetU32,
    pdmR3DevHlp_SSMGetU32V,
    pdmR3DevHlp_SSMGetS32,
    pdmR3DevHlp_SSMGetS32V,
    pdmR3DevHlp_SSMGetU64,
    pdmR3DevHlp_SSMGetU64V,
    pdmR3DevHlp_SSMGetS64,
    pdmR3DevHlp_SSMGetS64V,
    pdmR3DevHlp_SSMGetU128,
    pdmR3DevHlp_SSMGetU128V,
    pdmR3DevHlp_SSMGetS128,
    pdmR3DevHlp_SSMGetS128V,
    pdmR3DevHlp_SSMGetGCPhys32,
    pdmR3DevHlp_SSMGetGCPhys32V,
    pdmR3DevHlp_SSMGetGCPhys64,
    pdmR3DevHlp_SSMGetGCPhys64V,
    pdmR3DevHlp_SSMGetGCPhys,
    pdmR3DevHlp_SSMGetGCPhysV,
    pdmR3DevHlp_SSMGetUInt,
    pdmR3DevHlp_SSMGetSInt,
    pdmR3DevHlp_SSMGetGCUInt,
    pdmR3DevHlp_SSMGetGCUIntReg,
    pdmR3DevHlp_SSMGetGCPtr,
    pdmR3DevHlp_SSMGetGCUIntPtr,
    pdmR3DevHlp_SSMGetRCPtr,
    pdmR3DevHlp_SSMGetIOPort,
    pdmR3DevHlp_SSMGetSel,
    pdmR3DevHlp_SSMGetMem,
    pdmR3DevHlp_SSMGetStrZ,
    pdmR3DevHlp_SSMGetStrZEx,
    pdmR3DevHlp_SSMSkip,
    pdmR3DevHlp_SSMSkipToEndOfUnit,
    pdmR3DevHlp_SSMSetLoadError,
    pdmR3DevHlp_SSMSetLoadErrorV,
    pdmR3DevHlp_SSMSetCfgError,
    pdmR3DevHlp_SSMSetCfgErrorV,
    pdmR3DevHlp_SSMHandleGetStatus,
    pdmR3DevHlp_SSMHandleGetAfter,
    pdmR3DevHlp_SSMHandleIsLiveSave,
    pdmR3DevHlp_SSMHandleMaxDowntime,
    pdmR3DevHlp_SSMHandleHostBits,
    pdmR3DevHlp_SSMHandleRevision,
    pdmR3DevHlp_SSMHandleVersion,
    pdmR3DevHlp_SSMHandleHostOSAndArch,
    pdmR3DevHlp_TimerCreate,
    pdmR3DevHlp_TimerFromMicro,
    pdmR3DevHlp_TimerFromMilli,
    pdmR3DevHlp_TimerFromNano,
    pdmR3DevHlp_TimerGet,
    pdmR3DevHlp_TimerGetFreq,
    pdmR3DevHlp_TimerGetNano,
    pdmR3DevHlp_TimerIsActive,
    pdmR3DevHlp_TimerIsLockOwner,
    pdmR3DevHlp_TimerLockClock,
    pdmR3DevHlp_TimerLockClock2,
    pdmR3DevHlp_TimerSet,
    pdmR3DevHlp_TimerSetFrequencyHint,
    pdmR3DevHlp_TimerSetMicro,
    pdmR3DevHlp_TimerSetMillies,
    pdmR3DevHlp_TimerSetNano,
    pdmR3DevHlp_TimerSetRelative,
    pdmR3DevHlp_TimerStop,
    pdmR3DevHlp_TimerUnlockClock,
    pdmR3DevHlp_TimerUnlockClock2,
    pdmR3DevHlp_TimerSetCritSect,
    pdmR3DevHlp_TimerSave,
    pdmR3DevHlp_TimerLoad,
    pdmR3DevHlp_TimerDestroy,
    pdmR3DevHlp_TimerSkipLoad,
    pdmR3DevHlp_TMUtcNow,
    pdmR3DevHlp_CFGMExists,
    pdmR3DevHlp_CFGMQueryType,
    pdmR3DevHlp_CFGMQuerySize,
    pdmR3DevHlp_CFGMQueryInteger,
    pdmR3DevHlp_CFGMQueryIntegerDef,
    pdmR3DevHlp_CFGMQueryString,
    pdmR3DevHlp_CFGMQueryStringDef,
    pdmR3DevHlp_CFGMQueryPassword,
    pdmR3DevHlp_CFGMQueryPasswordDef,
    pdmR3DevHlp_CFGMQueryBytes,
    pdmR3DevHlp_CFGMQueryU64,
    pdmR3DevHlp_CFGMQueryU64Def,
    pdmR3DevHlp_CFGMQueryS64,
    pdmR3DevHlp_CFGMQueryS64Def,
    pdmR3DevHlp_CFGMQueryU32,
    pdmR3DevHlp_CFGMQueryU32Def,
    pdmR3DevHlp_CFGMQueryS32,
    pdmR3DevHlp_CFGMQueryS32Def,
    pdmR3DevHlp_CFGMQueryU16,
    pdmR3DevHlp_CFGMQueryU16Def,
    pdmR3DevHlp_CFGMQueryS16,
    pdmR3DevHlp_CFGMQueryS16Def,
    pdmR3DevHlp_CFGMQueryU8,
    pdmR3DevHlp_CFGMQueryU8Def,
    pdmR3DevHlp_CFGMQueryS8,
    pdmR3DevHlp_CFGMQueryS8Def,
    pdmR3DevHlp_CFGMQueryBool,
    pdmR3DevHlp_CFGMQueryBoolDef,
    pdmR3DevHlp_CFGMQueryPort,
    pdmR3DevHlp_CFGMQueryPortDef,
    pdmR3DevHlp_CFGMQueryUInt,
    pdmR3DevHlp_CFGMQueryUIntDef,
    pdmR3DevHlp_CFGMQuerySInt,
    pdmR3DevHlp_CFGMQuerySIntDef,
    pdmR3DevHlp_CFGMQueryPtr,
    pdmR3DevHlp_CFGMQueryPtrDef,
    pdmR3DevHlp_CFGMQueryGCPtr,
    pdmR3DevHlp_CFGMQueryGCPtrDef,
    pdmR3DevHlp_CFGMQueryGCPtrU,
    pdmR3DevHlp_CFGMQueryGCPtrUDef,
    pdmR3DevHlp_CFGMQueryGCPtrS,
    pdmR3DevHlp_CFGMQueryGCPtrSDef,
    pdmR3DevHlp_CFGMQueryStringAlloc,
    pdmR3DevHlp_CFGMQueryStringAllocDef,
    pdmR3DevHlp_CFGMGetParent,
    pdmR3DevHlp_CFGMGetChild,
    pdmR3DevHlp_CFGMGetChildF,
    pdmR3DevHlp_CFGMGetChildFV,
    pdmR3DevHlp_CFGMGetFirstChild,
    pdmR3DevHlp_CFGMGetNextChild,
    pdmR3DevHlp_CFGMGetName,
    pdmR3DevHlp_CFGMGetNameLen,
    pdmR3DevHlp_CFGMAreChildrenValid,
    pdmR3DevHlp_CFGMGetFirstValue,
    pdmR3DevHlp_CFGMGetNextValue,
    pdmR3DevHlp_CFGMGetValueName,
    pdmR3DevHlp_CFGMGetValueNameLen,
    pdmR3DevHlp_CFGMGetValueType,
    pdmR3DevHlp_CFGMAreValuesValid,
    pdmR3DevHlp_CFGMValidateConfig,
    pdmR3DevHlp_PhysRead,
    pdmR3DevHlp_PhysWrite,
    pdmR3DevHlp_PhysGCPhys2CCPtr,
    pdmR3DevHlp_PhysGCPhys2CCPtrReadOnly,
    pdmR3DevHlp_PhysReleasePageMappingLock,
    pdmR3DevHlp_PhysReadGCVirt,
    pdmR3DevHlp_PhysWriteGCVirt,
    pdmR3DevHlp_PhysGCPtr2GCPhys,
    pdmR3DevHlp_PhysIsGCPhysNormal,
    pdmR3DevHlp_PhysChangeMemBalloon,
    pdmR3DevHlp_MMHeapAlloc,
    pdmR3DevHlp_MMHeapAllocZ,
    pdmR3DevHlp_MMHeapAPrintfV,
    pdmR3DevHlp_MMHeapFree,
    pdmR3DevHlp_MMPhysGetRamSize,
    pdmR3DevHlp_MMPhysGetRamSizeBelow4GB,
    pdmR3DevHlp_MMPhysGetRamSizeAbove4GB,
    pdmR3DevHlp_VMState,
    pdmR3DevHlp_VMTeleportedAndNotFullyResumedYet,
    pdmR3DevHlp_VMSetErrorV,
    pdmR3DevHlp_VMSetRuntimeErrorV,
    pdmR3DevHlp_VMWaitForDeviceReady,
    pdmR3DevHlp_VMNotifyCpuDeviceReady,
    pdmR3DevHlp_VMReqCallNoWaitV,
    pdmR3DevHlp_VMReqPriorityCallWaitV,
    pdmR3DevHlp_DBGFStopV,
    pdmR3DevHlp_DBGFInfoRegister,
    pdmR3DevHlp_DBGFInfoRegisterArgv,
    pdmR3DevHlp_DBGFRegRegister,
    pdmR3DevHlp_DBGFTraceBuf,
    pdmR3DevHlp_DBGFReportBugCheck,
    pdmR3DevHlp_DBGFCoreWrite,
    pdmR3DevHlp_DBGFInfoLogHlp,
    pdmR3DevHlp_DBGFRegNmQueryU64,
    pdmR3DevHlp_DBGFRegPrintfV,
    pdmR3DevHlp_STAMRegister,
    pdmR3DevHlp_STAMRegisterV,
    pdmR3DevHlp_PCIRegister,
    pdmR3DevHlp_PCIRegisterMsi,
    pdmR3DevHlp_PCIIORegionRegister,
    pdmR3DevHlp_PCIInterceptConfigAccesses,
    pdmR3DevHlp_PCIConfigWrite,
    pdmR3DevHlp_PCIConfigRead,
    pdmR3DevHlp_PCIPhysRead,
    pdmR3DevHlp_PCIPhysWrite,
    pdmR3DevHlp_PCIPhysGCPhys2CCPtr,
    pdmR3DevHlp_PCIPhysGCPhys2CCPtrReadOnly,
    pdmR3DevHlp_PCIPhysBulkGCPhys2CCPtr,
    pdmR3DevHlp_PCIPhysBulkGCPhys2CCPtrReadOnly,
    pdmR3DevHlp_PCISetIrq,
    pdmR3DevHlp_PCISetIrqNoWait,
    pdmR3DevHlp_ISASetIrq,
    pdmR3DevHlp_ISASetIrqNoWait,
    pdmR3DevHlp_DriverAttach,
    pdmR3DevHlp_DriverDetach,
    pdmR3DevHlp_DriverReconfigure,
    pdmR3DevHlp_QueueCreate,
    pdmR3DevHlp_QueueAlloc,
    pdmR3DevHlp_QueueInsert,
    pdmR3DevHlp_QueueFlushIfNecessary,
    pdmR3DevHlp_TaskCreate,
    pdmR3DevHlp_TaskTrigger,
    pdmR3DevHlp_SUPSemEventCreate,
    pdmR3DevHlp_SUPSemEventClose,
    pdmR3DevHlp_SUPSemEventSignal,
    pdmR3DevHlp_SUPSemEventWaitNoResume,
    pdmR3DevHlp_SUPSemEventWaitNsAbsIntr,
    pdmR3DevHlp_SUPSemEventWaitNsRelIntr,
    pdmR3DevHlp_SUPSemEventGetResolution,
    pdmR3DevHlp_SUPSemEventMultiCreate,
    pdmR3DevHlp_SUPSemEventMultiClose,
    pdmR3DevHlp_SUPSemEventMultiSignal,
    pdmR3DevHlp_SUPSemEventMultiReset,
    pdmR3DevHlp_SUPSemEventMultiWaitNoResume,
    pdmR3DevHlp_SUPSemEventMultiWaitNsAbsIntr,
    pdmR3DevHlp_SUPSemEventMultiWaitNsRelIntr,
    pdmR3DevHlp_SUPSemEventMultiGetResolution,
    pdmR3DevHlp_CritSectInit,
    pdmR3DevHlp_CritSectGetNop,
    pdmR3DevHlp_SetDeviceCritSect,
    pdmR3DevHlp_CritSectYield,
    pdmR3DevHlp_CritSectEnter,
    pdmR3DevHlp_CritSectEnterDebug,
    pdmR3DevHlp_CritSectTryEnter,
    pdmR3DevHlp_CritSectTryEnterDebug,
    pdmR3DevHlp_CritSectLeave,
    pdmR3DevHlp_CritSectIsOwner,
    pdmR3DevHlp_CritSectIsInitialized,
    pdmR3DevHlp_CritSectHasWaiters,
    pdmR3DevHlp_CritSectGetRecursion,
    pdmR3DevHlp_CritSectScheduleExitEvent,
    pdmR3DevHlp_CritSectDelete,
    pdmR3DevHlp_CritSectRwInit,
    pdmR3DevHlp_CritSectRwDelete,
    pdmR3DevHlp_CritSectRwEnterShared,
    pdmR3DevHlp_CritSectRwEnterSharedDebug,
    pdmR3DevHlp_CritSectRwTryEnterShared,
    pdmR3DevHlp_CritSectRwTryEnterSharedDebug,
    pdmR3DevHlp_CritSectRwLeaveShared,
    pdmR3DevHlp_CritSectRwEnterExcl,
    pdmR3DevHlp_CritSectRwEnterExclDebug,
    pdmR3DevHlp_CritSectRwTryEnterExcl,
    pdmR3DevHlp_CritSectRwTryEnterExclDebug,
    pdmR3DevHlp_CritSectRwLeaveExcl,
    pdmR3DevHlp_CritSectRwIsWriteOwner,
    pdmR3DevHlp_CritSectRwIsReadOwner,
    pdmR3DevHlp_CritSectRwGetWriteRecursion,
    pdmR3DevHlp_CritSectRwGetWriterReadRecursion,
    pdmR3DevHlp_CritSectRwGetReadCount,
    pdmR3DevHlp_CritSectRwIsInitialized,
    pdmR3DevHlp_ThreadCreate,
    pdmR3DevHlp_ThreadDestroy,
    pdmR3DevHlp_ThreadIAmSuspending,
    pdmR3DevHlp_ThreadIamRunning,
    pdmR3DevHlp_ThreadSleep,
    pdmR3DevHlp_ThreadSuspend,
    pdmR3DevHlp_ThreadResume,
    pdmR3DevHlp_SetAsyncNotification,
    pdmR3DevHlp_AsyncNotificationCompleted,
    pdmR3DevHlp_RTCRegister,
    pdmR3DevHlp_PCIBusRegister,
    pdmR3DevHlp_IommuRegister,
    pdmR3DevHlp_PICRegister,
    pdmR3DevHlp_ApicRegister,
    pdmR3DevHlp_IoApicRegister,
    pdmR3DevHlp_HpetRegister,
    pdmR3DevHlp_PciRawRegister,
    pdmR3DevHlp_DMACRegister,
    pdmR3DevHlp_DMARegister,
    pdmR3DevHlp_DMAReadMemory,
    pdmR3DevHlp_DMAWriteMemory,
    pdmR3DevHlp_DMASetDREQ,
    pdmR3DevHlp_DMAGetChannelMode,
    pdmR3DevHlp_DMASchedule,
    pdmR3DevHlp_CMOSWrite,
    pdmR3DevHlp_CMOSRead,
    pdmR3DevHlp_AssertEMT,
    pdmR3DevHlp_AssertOther,
    pdmR3DevHlp_LdrGetRCInterfaceSymbols,
    pdmR3DevHlp_LdrGetR0InterfaceSymbols,
    pdmR3DevHlp_CallR0,
    pdmR3DevHlp_VMGetSuspendReason,
    pdmR3DevHlp_VMGetResumeReason,
    pdmR3DevHlp_PhysBulkGCPhys2CCPtr,
    pdmR3DevHlp_PhysBulkGCPhys2CCPtrReadOnly,
    pdmR3DevHlp_PhysBulkReleasePageMappingLocks,
    pdmR3DevHlp_CpuGetGuestMicroarch,
    pdmR3DevHlp_CpuGetGuestAddrWidths,
    pdmR3DevHlp_CpuGetGuestScalableBusFrequency,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    pdmR3DevHlp_GetUVM,
    pdmR3DevHlp_GetVM,
    pdmR3DevHlp_GetVMCPU,
    pdmR3DevHlp_GetCurrentCpuId,
    pdmR3DevHlp_RegisterVMMDevHeap,
    pdmR3DevHlp_FirmwareRegister,
    pdmR3DevHlp_VMReset,
    pdmR3DevHlp_VMSuspend,
    pdmR3DevHlp_VMSuspendSaveAndPowerOff,
    pdmR3DevHlp_VMPowerOff,
    pdmR3DevHlp_A20IsEnabled,
    pdmR3DevHlp_A20Set,
    pdmR3DevHlp_GetCpuId,
    pdmR3DevHlp_GetMainExecutionEngine,
    pdmR3DevHlp_TMTimeVirtGet,
    pdmR3DevHlp_TMTimeVirtGetFreq,
    pdmR3DevHlp_TMTimeVirtGetNano,
    pdmR3DevHlp_TMCpuTicksPerSecond,
    pdmR3DevHlp_GetSupDrvSession,
    pdmR3DevHlp_QueryGenericUserObject,
    pdmR3DevHlp_PGMHandlerPhysicalTypeRegister,
    pdmR3DevHlp_PGMHandlerPhysicalRegister,
    pdmR3DevHlp_PGMHandlerPhysicalDeregister,
    pdmR3DevHlp_PGMHandlerPhysicalPageTempOff,
    pdmR3DevHlp_PGMHandlerPhysicalReset,
    pdmR3DevHlp_VMMRegisterPatchMemory,
    pdmR3DevHlp_VMMDeregisterPatchMemory,
    pdmR3DevHlp_SharedModuleRegister,
    pdmR3DevHlp_SharedModuleUnregister,
    pdmR3DevHlp_SharedModuleGetPageState,
    pdmR3DevHlp_SharedModuleCheckAll,
    pdmR3DevHlp_QueryLun,
    pdmR3DevHlp_GIMDeviceRegister,
    pdmR3DevHlp_GIMGetDebugSetup,
    pdmR3DevHlp_GIMGetMmio2Regions,
    PDM_DEVHLPR3_VERSION /* the end */
};

