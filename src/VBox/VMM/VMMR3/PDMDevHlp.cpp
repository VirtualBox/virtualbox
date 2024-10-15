/* $Id: PDMDevHlp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM - Pluggable Device and Driver Manager, Device Helpers.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_PDM_DEVICE
#define PDMPCIDEV_INCLUDE_PRIVATE  /* Hack to get pdmpcidevint.h included at the right point. */
#include "PDMInternal.h"
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/vmapi.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/vmcc.h>

#include <VBox/version.h>
#include <VBox/log.h>
#include <VBox/pci.h>
#include <VBox/err.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/mem.h>

#include "dtrace/VBoxVMM.h"
#include "PDMInline.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def PDM_DEVHLP_DEADLOCK_DETECTION
 * Define this to enable the deadlock detection when accessing physical memory.
 */
#if /*defined(DEBUG_bird) ||*/ defined(DOXYGEN_RUNNING)
# define PDM_DEVHLP_DEADLOCK_DETECTION /**< @todo enable DevHlp deadlock detection! */
#endif



/** @name R3 DevHlp
 * @{
 */


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
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);

    int rc = IOMR3IoPortCreate(pVM, pDevIns, cPorts, fFlags, pPciDev, iPciRegion,
                               pfnOut, pfnIn, pfnOutStr, pfnInStr, pvUser, pszDesc, paExtDescs, phIoPorts);

    LogFlow(("pdmR3DevHlp_IoPortCreateEx: caller='%s'/%d: returns %Rrc (*phIoPorts=%#x)\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc, *phIoPorts));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnIoPortMap} */
static DECLCALLBACK(int) pdmR3DevHlp_IoPortMap(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts, RTIOPORT Port)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_IoPortMap: caller='%s'/%d: hIoPorts=%#x Port=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, hIoPorts, Port));
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);

    int rc = IOMR3IoPortMap(pVM, pDevIns, hIoPorts, Port);

    LogFlow(("pdmR3DevHlp_IoPortMap: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnIoPortUnmap} */
static DECLCALLBACK(int) pdmR3DevHlp_IoPortUnmap(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_IoPortMap: caller='%s'/%d: hIoPorts=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, hIoPorts));
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);

    int rc = IOMR3IoPortUnmap(pVM, pDevIns, hIoPorts);

    LogFlow(("pdmR3DevHlp_IoPortMap: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnIoPortGetMappingAddress} */
static DECLCALLBACK(uint32_t) pdmR3DevHlp_IoPortGetMappingAddress(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_IoPortGetMappingAddress: caller='%s'/%d: hIoPorts=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, hIoPorts));

    uint32_t uAddress = IOMR3IoPortGetMappingAddress(pDevIns->Internal.s.pVMR3, pDevIns, hIoPorts);

    LogFlow(("pdmR3DevHlp_IoPortGetMappingAddress: caller='%s'/%d: returns %#RX32\n", pDevIns->pReg->szName, pDevIns->iInstance, uAddress));
    return uAddress;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnIoPortRead} */
static DECLCALLBACK(VBOXSTRICTRC) pdmR3DevHlp_IoPortRead(PPDMDEVINS pDevIns, RTIOPORT Port, uint32_t *pu32Value, size_t cbValue)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_IoPortRead: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);

    PVMCPU pVCpu = VMMGetCpu(pVM);
    AssertPtrReturn(pVCpu, VERR_ACCESS_DENIED);

    VBOXSTRICTRC rcStrict = IOMIOPortRead(pVM, pVCpu, Port, pu32Value, cbValue);

    LogFlow(("pdmR3DevHlp_IoPortRead: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnIoPortWrite} */
static DECLCALLBACK(VBOXSTRICTRC) pdmR3DevHlp_IoPortWrite(PPDMDEVINS pDevIns, RTIOPORT Port, uint32_t u32Value, size_t cbValue)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_IoPortWrite: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);

    PVMCPU pVCpu = VMMGetCpu(pVM);
    AssertPtrReturn(pVCpu, VERR_ACCESS_DENIED);

    VBOXSTRICTRC rcStrict = IOMIOPortWrite(pVM, pVCpu, Port, u32Value, cbValue);

    LogFlow(("pdmR3DevHlp_IoPortWrite: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmioCreateEx} */
static DECLCALLBACK(int) pdmR3DevHlp_MmioCreateEx(PPDMDEVINS pDevIns, RTGCPHYS cbRegion, uint32_t fFlags,
                                                  PPDMPCIDEV pPciDev, uint32_t iPciRegion, PFNIOMMMIONEWWRITE pfnWrite,
                                                  PFNIOMMMIONEWREAD pfnRead, PFNIOMMMIONEWFILL pfnFill,
                                                  void *pvUser, const char *pszDesc, PIOMMMIOHANDLE phRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MmioCreateEx: caller='%s'/%d: cbRegion=%#RGp fFlags=%#x pPciDev=%p iPciRegion=%#x pfnWrite=%p pfnRead=%p pfnFill=%p pvUser=%p pszDesc=%p:{%s} phRegion=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cbRegion, fFlags, pPciDev, iPciRegion, pfnWrite, pfnRead, pfnFill, pvUser, pszDesc, pszDesc, phRegion));
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);

    if (pDevIns->iInstance > 0)
    {
        pszDesc = MMR3HeapAPrintf(pVM, MM_TAG_PDM_DEVICE_DESC, "%s [%u]", pszDesc, pDevIns->iInstance);
        AssertReturn(pszDesc, VERR_NO_STR_MEMORY);
    }

    /* HACK ALERT! Round the size up to page size.  The PCI bus should do something similar before mapping it. */
    /** @todo It's possible we need to do dummy MMIO fill-in of the PCI bus or
     *        guest adds more alignment to an region. */
    cbRegion = RT_ALIGN_T(cbRegion, GUEST_PAGE_SIZE, RTGCPHYS);

    int rc = IOMR3MmioCreate(pVM, pDevIns, cbRegion, fFlags, pPciDev, iPciRegion,
                             pfnWrite, pfnRead, pfnFill, pvUser, pszDesc, phRegion);

    LogFlow(("pdmR3DevHlp_MmioCreateEx: caller='%s'/%d: returns %Rrc (*phRegion=%#x)\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc, *phRegion));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmioMap} */
static DECLCALLBACK(int) pdmR3DevHlp_MmioMap(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, RTGCPHYS GCPhys)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MmioMap: caller='%s'/%d: hRegion=%#x GCPhys=%#RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion, GCPhys));

    PVM    const pVM   = pDevIns->Internal.s.pVMR3;
    PVMCPU const pVCpu = VMMGetCpu(pVM);
    AssertReturn(pVCpu, VERR_VM_THREAD_NOT_EMT);

    int rc = IOMR3MmioMap(pVM, pVCpu, pDevIns, hRegion, GCPhys);

    LogFlow(("pdmR3DevHlp_MmioMap: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmioUnmap} */
static DECLCALLBACK(int) pdmR3DevHlp_MmioUnmap(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MmioUnmap: caller='%s'/%d: hRegion=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion));

    PVM    const pVM   = pDevIns->Internal.s.pVMR3;
    PVMCPU const pVCpu = VMMGetCpu(pVM);
    AssertReturn(pVCpu, VERR_VM_THREAD_NOT_EMT);

    int rc = IOMR3MmioUnmap(pVM, pVCpu, pDevIns, hRegion);

    LogFlow(("pdmR3DevHlp_MmioUnmap: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmioReduce} */
static DECLCALLBACK(int) pdmR3DevHlp_MmioReduce(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, RTGCPHYS cbRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MmioReduce: caller='%s'/%d: hRegion=%#x cbRegion=%#RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion, cbRegion));
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_LOADING, VERR_VM_INVALID_VM_STATE);

    int rc = IOMR3MmioReduce(pVM, pDevIns, hRegion, cbRegion);

    LogFlow(("pdmR3DevHlp_MmioReduce: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmioGetMappingAddress} */
static DECLCALLBACK(RTGCPHYS) pdmR3DevHlp_MmioGetMappingAddress(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MmioGetMappingAddress: caller='%s'/%d: hRegion=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion));

    RTGCPHYS GCPhys = IOMR3MmioGetMappingAddress(pDevIns->Internal.s.pVMR3, pDevIns, hRegion);

    LogFlow(("pdmR3DevHlp_MmioGetMappingAddress: caller='%s'/%d: returns %RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, GCPhys));
    return GCPhys;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmio2Create} */
static DECLCALLBACK(int) pdmR3DevHlp_Mmio2Create(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iPciRegion, RTGCPHYS cbRegion,
                                                 uint32_t fFlags, const char *pszDesc, void **ppvMapping, PPGMMMIO2HANDLE phRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DevHlp_Mmio2Create: caller='%s'/%d: pPciDev=%p (%#x) iPciRegion=%#x cbRegion=%#RGp fFlags=%RX32 pszDesc=%p:{%s} ppvMapping=%p phRegion=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPciDev, pPciDev ? pPciDev->uDevFn : UINT32_MAX, iPciRegion, cbRegion,
             fFlags, pszDesc, pszDesc, ppvMapping, phRegion));
    *ppvMapping = NULL;
    *phRegion   = NIL_PGMMMIO2HANDLE;
    AssertReturn(!pPciDev || pPciDev->Int.s.pDevInsR3 == pDevIns, VERR_INVALID_PARAMETER);

    PVM const pVM = pDevIns->Internal.s.pVMR3;

    AssertReturn(!(iPciRegion & UINT16_MAX), VERR_INVALID_PARAMETER); /* not implemented. */

    /** @todo PGMR3PhysMmio2Register mangles the description, move it here and
     *        use a real string cache. */
    int rc = PGMR3PhysMmio2Register(pVM, pDevIns, pPciDev ? pPciDev->Int.s.idxDevCfg : 254, iPciRegion >> 16,
                                    cbRegion, fFlags, pszDesc, ppvMapping, phRegion);

    LogFlow(("pdmR3DevHlp_Mmio2Create: caller='%s'/%d: returns %Rrc *ppvMapping=%p phRegion=%#RX64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc, *ppvMapping, *phRegion));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmio2Destroy} */
static DECLCALLBACK(int) pdmR3DevHlp_Mmio2Destroy(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DevHlp_Mmio2Destroy: caller='%s'/%d: hRegion=%#RX64\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion));

    int rc = PGMR3PhysMmio2Deregister(pDevIns->Internal.s.pVMR3, pDevIns, hRegion);

    LogFlow(("pdmR3DevHlp_Mmio2Destroy: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmio2Map} */
static DECLCALLBACK(int) pdmR3DevHlp_Mmio2Map(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion, RTGCPHYS GCPhys)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_Mmio2Map: caller='%s'/%d: hRegion=%#RX64 GCPhys=%RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion, GCPhys));

    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);

    int rc = PGMR3PhysMmio2Map(pDevIns->Internal.s.pVMR3, pDevIns, hRegion, GCPhys);

    LogFlow(("pdmR3DevHlp_Mmio2Map: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmio2Unmap} */
static DECLCALLBACK(int) pdmR3DevHlp_Mmio2Unmap(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_Mmio2Unmap: caller='%s'/%d: hRegion=%#RX64\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion));

    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);

    int rc = PGMR3PhysMmio2Unmap(pDevIns->Internal.s.pVMR3, pDevIns, hRegion, NIL_RTGCPHYS);

    LogFlow(("pdmR3DevHlp_Mmio2Unmap: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmio2Reduce} */
static DECLCALLBACK(int) pdmR3DevHlp_Mmio2Reduce(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion, RTGCPHYS cbRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_Mmio2Reduce: caller='%s'/%d: hRegion=%#RX64 cbRegion=%RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion, cbRegion));
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_LOADING, VERR_VM_INVALID_VM_STATE);

    int rc = PGMR3PhysMmio2Reduce(pDevIns->Internal.s.pVMR3, pDevIns, hRegion, cbRegion);

    LogFlow(("pdmR3DevHlp_Mmio2Reduce: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmio2GetMappingAddress} */
static DECLCALLBACK(RTGCPHYS) pdmR3DevHlp_Mmio2GetMappingAddress(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_Mmio2GetMappingAddress: caller='%s'/%d: hRegion=%#RX64\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion));
    VM_ASSERT_EMT0_RETURN(pVM, NIL_RTGCPHYS);

    RTGCPHYS GCPhys = PGMR3PhysMmio2GetMappingAddress(pVM, pDevIns, hRegion);

    LogFlow(("pdmR3DevHlp_Mmio2GetMappingAddress: caller='%s'/%d: returns %RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, GCPhys));
    return GCPhys;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmio2QueryAndResetDirtyBitmap} */
static DECLCALLBACK(int) pdmR3DevHlp_Mmio2QueryAndResetDirtyBitmap(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion,
                                                                   void *pvBitmap, size_t cbBitmap)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_Mmio2QueryAndResetDirtyBitmap: caller='%s'/%d: hRegion=%#RX64 pvBitmap=%p cbBitmap=%#zx\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hRegion, pvBitmap, cbBitmap));

    int rc = PGMR3PhysMmio2QueryAndResetDirtyBitmap(pVM, pDevIns, hRegion, pvBitmap, cbBitmap);

    LogFlow(("pdmR3DevHlp_Mmio2QueryAndResetDirtyBitmap: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmio2ControlDirtyPageTracking} */
static DECLCALLBACK(int) pdmR3DevHlp_Mmio2ControlDirtyPageTracking(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion, bool fEnabled)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_Mmio2ControlDirtyPageTracking: caller='%s'/%d: hRegion=%#RX64 fEnabled=%RTbool\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hRegion, fEnabled));

    int rc = PGMR3PhysMmio2ControlDirtyPageTracking(pVM, pDevIns, hRegion, fEnabled);

    LogFlow(("pdmR3DevHlp_Mmio2ControlDirtyPageTracking: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/**
 * @copydoc PDMDEVHLPR3::pfnMmio2ChangeRegionNo
 */
static DECLCALLBACK(int) pdmR3DevHlp_Mmio2ChangeRegionNo(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion, uint32_t iNewRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_Mmio2ChangeRegionNo: caller='%s'/%d: hRegion=%#RX64 iNewRegion=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion, iNewRegion));
    VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);

    int rc = PGMR3PhysMmio2ChangeRegionNo(pVM, pDevIns, hRegion, iNewRegion);

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

    int rc = IOMMmioMapMmio2Page(pDevIns->Internal.s.pVMR3, pDevIns, hRegion, offRegion, hMmio2, offMmio2, fPageFlags);

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

    int rc = IOMMmioResetRegion(pDevIns->Internal.s.pVMR3, pDevIns, hRegion);

    Log(("pdmR3DevHlp_MmioResetRegion: caller='%s'/%d: returns %Rrc\n",
         pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnROMRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_ROMRegister(PPDMDEVINS pDevIns, RTGCPHYS GCPhysStart, uint32_t cbRange,
                                                 const void *pvBinary, uint32_t cbBinary, uint32_t fFlags, const char *pszDesc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DevHlp_ROMRegister: caller='%s'/%d: GCPhysStart=%RGp cbRange=%#x pvBinary=%p cbBinary=%#x fFlags=%#RX32 pszDesc=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPhysStart, cbRange, pvBinary, cbBinary, fFlags, pszDesc, pszDesc));

/** @todo can we mangle pszDesc? */
    int rc = PGMR3PhysRomRegister(pDevIns->Internal.s.pVMR3, pDevIns, GCPhysStart, cbRange, pvBinary, cbBinary, fFlags, pszDesc);

    LogFlow(("pdmR3DevHlp_ROMRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnROMProtectShadow} */
static DECLCALLBACK(int) pdmR3DevHlp_ROMProtectShadow(PPDMDEVINS pDevIns, RTGCPHYS GCPhysStart, uint32_t cbRange, PGMROMPROT enmProt)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_ROMProtectShadow: caller='%s'/%d: GCPhysStart=%RGp cbRange=%#x enmProt=%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPhysStart, cbRange, enmProt));

    int rc = PGMR3PhysRomProtect(pDevIns->Internal.s.pVMR3, GCPhysStart, cbRange, enmProt);

    LogFlow(("pdmR3DevHlp_ROMProtectShadow: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSSMRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_SSMRegister(PPDMDEVINS pDevIns, uint32_t uVersion, size_t cbGuess, const char *pszBefore,
                                                 PFNSSMDEVLIVEPREP pfnLivePrep, PFNSSMDEVLIVEEXEC pfnLiveExec, PFNSSMDEVLIVEVOTE pfnLiveVote,
                                                 PFNSSMDEVSAVEPREP pfnSavePrep, PFNSSMDEVSAVEEXEC pfnSaveExec, PFNSSMDEVSAVEDONE pfnSaveDone,
                                                 PFNSSMDEVLOADPREP pfnLoadPrep, PFNSSMDEVLOADEXEC pfnLoadExec, PFNSSMDEVLOADDONE pfnLoadDone)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DevHlp_SSMRegister: caller='%s'/%d: uVersion=%#x cbGuess=%#x pszBefore=%p:{%s}\n"
             "    pfnLivePrep=%p pfnLiveExec=%p pfnLiveVote=%p pfnSavePrep=%p pfnSaveExec=%p pfnSaveDone=%p pfnLoadPrep=%p pfnLoadExec=%p pfnLoadDone=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, uVersion, cbGuess, pszBefore, pszBefore,
             pfnLivePrep, pfnLiveExec, pfnLiveVote,
             pfnSavePrep, pfnSaveExec, pfnSaveDone,
             pfnLoadPrep, pfnLoadExec, pfnLoadDone));

    int rc = SSMR3RegisterDevice(pDevIns->Internal.s.pVMR3, pDevIns, pDevIns->pReg->szName, pDevIns->iInstance,
                                 uVersion, cbGuess, pszBefore,
                                 pfnLivePrep, pfnLiveExec, pfnLiveVote,
                                 pfnSavePrep, pfnSaveExec, pfnSaveDone,
                                 pfnLoadPrep, pfnLoadExec, pfnLoadDone);

    LogFlow(("pdmR3DevHlp_SSMRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSSMRegisterLegacy} */
static DECLCALLBACK(int) pdmR3DevHlp_SSMRegisterLegacy(PPDMDEVINS pDevIns, const char *pszOldName, PFNSSMDEVLOADPREP pfnLoadPrep,
                                                       PFNSSMDEVLOADEXEC pfnLoadExec, PFNSSMDEVLOADDONE pfnLoadDone)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DevHlp_SSMRegisterLegacy: caller='%s'/%d: pszOldName=%p:{%s} pfnLoadPrep=%p pfnLoadExec=%p pfnLoadDone=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pszOldName, pszOldName, pfnLoadPrep, pfnLoadExec, pfnLoadDone));

    int rc = SSMR3RegisterDevice(pDevIns->Internal.s.pVMR3, pDevIns, pszOldName, pDevIns->iInstance,
                                 0 /*uVersion*/, 0 /*cbGuess*/, NULL /*pszBefore*/,
                                 NULL, NULL, NULL,
                                 NULL, NULL, NULL,
                                 pfnLoadPrep, pfnLoadExec, pfnLoadDone);

    LogFlow(("pdmR3DevHlp_SSMRegisterLegacy: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerCreate} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerCreate(PPDMDEVINS pDevIns, TMCLOCK enmClock, PFNTMTIMERDEV pfnCallback,
                                                 void *pvUser, uint32_t fFlags, const char *pszDesc, PTMTIMERHANDLE phTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DevHlp_TimerCreate: caller='%s'/%d: enmClock=%d pfnCallback=%p pvUser=%p fFlags=%#x pszDesc=%p:{%s} phTimer=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, enmClock, pfnCallback, pvUser, fFlags, pszDesc, pszDesc, phTimer));

    /* Mangle the timer name if there are more than one instance of this device. */
    char szName[32];
    AssertReturn(strlen(pszDesc) < sizeof(szName) - 3, VERR_INVALID_NAME);
    if (pDevIns->iInstance > 0)
    {
        RTStrPrintf(szName, sizeof(szName), "%s[%u]", pszDesc, pDevIns->iInstance);
        pszDesc = szName;
    }

    /* Clear the ring-0 flag if the device isn't configured for ring-0. */
    if (fFlags & TMTIMER_FLAGS_RING0)
    {
        Assert(pDevIns->Internal.s.pDevR3->pReg->fFlags & PDM_DEVREG_FLAGS_R0);
        if (!(pDevIns->Internal.s.fIntFlags & PDMDEVINSINT_FLAGS_R0_ENABLED))
            fFlags &= ~TMTIMER_FLAGS_RING0;
    }
    else
        Assert(fFlags & TMTIMER_FLAGS_NO_RING0 /* just to make sure all devices has been considered */);

    int rc = TMR3TimerCreateDevice(pVM, pDevIns, enmClock, pfnCallback, pvUser, fFlags, pszDesc, phTimer);

    LogFlow(("pdmR3DevHlp_TimerCreate: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerFromMicro} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TimerFromMicro(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMicroSecs)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerFromMicro(pDevIns->Internal.s.pVMR3, hTimer, cMicroSecs);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerFromMilli} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TimerFromMilli(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMilliSecs)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerFromMilli(pDevIns->Internal.s.pVMR3, hTimer, cMilliSecs);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerFromNano} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TimerFromNano(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cNanoSecs)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerFromNano(pDevIns->Internal.s.pVMR3, hTimer, cNanoSecs);
}

/** @interface_method_impl{PDMDEVHLPR3,pfnTimerGet} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TimerGet(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerGet(pDevIns->Internal.s.pVMR3, hTimer);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerGetFreq} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TimerGetFreq(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerGetFreq(pDevIns->Internal.s.pVMR3, hTimer);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerGetNano} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TimerGetNano(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerGetNano(pDevIns->Internal.s.pVMR3, hTimer);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerIsActive} */
static DECLCALLBACK(bool) pdmR3DevHlp_TimerIsActive(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerIsActive(pDevIns->Internal.s.pVMR3, hTimer);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerIsLockOwner} */
static DECLCALLBACK(bool) pdmR3DevHlp_TimerIsLockOwner(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerIsLockOwner(pDevIns->Internal.s.pVMR3, hTimer);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerLockClock} */
static DECLCALLBACK(VBOXSTRICTRC) pdmR3DevHlp_TimerLockClock(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerLock(pDevIns->Internal.s.pVMR3, hTimer, rcBusy);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerLockClock2} */
static DECLCALLBACK(VBOXSTRICTRC) pdmR3DevHlp_TimerLockClock2(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer,
                                                              PPDMCRITSECT pCritSect, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM const pVM = pDevIns->Internal.s.pVMR3;
    VBOXSTRICTRC rc = TMTimerLock(pVM, hTimer, rcBusy);
    if (rc == VINF_SUCCESS)
    {
        rc = PDMCritSectEnter(pVM, pCritSect, rcBusy);
        if (rc == VINF_SUCCESS)
            return rc;
        AssertRC(VBOXSTRICTRC_VAL(rc));
        TMTimerUnlock(pVM, hTimer);
    }
    else
        AssertRC(VBOXSTRICTRC_VAL(rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerSet} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerSet(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t uExpire)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerSet(pDevIns->Internal.s.pVMR3, hTimer, uExpire);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerSetFrequencyHint} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerSetFrequencyHint(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint32_t uHz)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerSetFrequencyHint(pDevIns->Internal.s.pVMR3, hTimer, uHz);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerSetMicro} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerSetMicro(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMicrosToNext)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerSetMicro(pDevIns->Internal.s.pVMR3, hTimer, cMicrosToNext);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerSetMillies} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerSetMillies(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMilliesToNext)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerSetMillies(pDevIns->Internal.s.pVMR3, hTimer, cMilliesToNext);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerSetNano} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerSetNano(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cNanosToNext)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerSetNano(pDevIns->Internal.s.pVMR3, hTimer, cNanosToNext);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerSetRelative} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerSetRelative(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cTicksToNext, uint64_t *pu64Now)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerSetRelative(pDevIns->Internal.s.pVMR3, hTimer, cTicksToNext, pu64Now);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerStop} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerStop(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerStop(pDevIns->Internal.s.pVMR3, hTimer);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerUnlockClock} */
static DECLCALLBACK(void) pdmR3DevHlp_TimerUnlockClock(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    TMTimerUnlock(pDevIns->Internal.s.pVMR3, hTimer);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerUnlockClock2} */
static DECLCALLBACK(void) pdmR3DevHlp_TimerUnlockClock2(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM const pVM = pDevIns->Internal.s.pVMR3;
    TMTimerUnlock(pVM, hTimer);
    int rc = PDMCritSectLeave(pVM, pCritSect);
    AssertRC(rc);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerSetCritSect} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerSetCritSect(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMR3TimerSetCritSect(pDevIns->Internal.s.pVMR3, hTimer, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerSave} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerSave(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PSSMHANDLE pSSM)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMR3TimerSave(pDevIns->Internal.s.pVMR3, hTimer, pSSM);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerLoad} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerLoad(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PSSMHANDLE pSSM)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMR3TimerLoad(pDevIns->Internal.s.pVMR3, hTimer, pSSM);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTimerDestroy} */
static DECLCALLBACK(int) pdmR3DevHlp_TimerDestroy(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMR3TimerDestroy(pDevIns->Internal.s.pVMR3, hTimer);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTMUtcNow} */
static DECLCALLBACK(PRTTIMESPEC) pdmR3DevHlp_TMUtcNow(PPDMDEVINS pDevIns, PRTTIMESPEC pTime)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_TMUtcNow: caller='%s'/%d: pTime=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pTime));

    pTime = TMR3UtcNow(pDevIns->Internal.s.pVMR3, pTime);

    LogFlow(("pdmR3DevHlp_TMUtcNow: caller='%s'/%d: returns %RU64\n", pDevIns->pReg->szName, pDevIns->iInstance, RTTimeSpecGetNano(pTime)));
    return pTime;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTMTimeVirtGet} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TMTimeVirtGet(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_TMTimeVirtGet: caller='%s'/%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    uint64_t u64Time = TMVirtualSyncGet(pDevIns->Internal.s.pVMR3);

    LogFlow(("pdmR3DevHlp_TMTimeVirtGet: caller='%s'/%d: returns %RU64\n", pDevIns->pReg->szName, pDevIns->iInstance, u64Time));
    return u64Time;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTMTimeVirtGetFreq} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TMTimeVirtGetFreq(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_TMTimeVirtGetFreq: caller='%s'/%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    uint64_t u64Freq = TMVirtualGetFreq(pDevIns->Internal.s.pVMR3);

    LogFlow(("pdmR3DevHlp_TMTimeVirtGetFreq: caller='%s'/%d: returns %RU64\n", pDevIns->pReg->szName, pDevIns->iInstance, u64Freq));
    return u64Freq;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTMTimeVirtGetNano} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TMTimeVirtGetNano(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_TMTimeVirtGetNano: caller='%s'/%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    uint64_t u64Time = TMVirtualGet(pDevIns->Internal.s.pVMR3);
    uint64_t u64Nano = TMVirtualToNano(pDevIns->Internal.s.pVMR3, u64Time);

    LogFlow(("pdmR3DevHlp_TMTimeVirtGetNano: caller='%s'/%d: returns %RU64\n", pDevIns->pReg->szName, pDevIns->iInstance, u64Nano));
    return u64Nano;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTMCpuTicksPerSecond} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_TMCpuTicksPerSecond(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_TMCpuTicksPerSecond: caller='%s'/%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    uint64_t u64CpuTicksPerSec = TMCpuTicksPerSecond(pDevIns->Internal.s.pVMR3);

    LogFlow(("pdmR3DevHlp_TMCpuTicksPerSecond: caller='%s'/%d: returns %RU64\n", pDevIns->pReg->szName, pDevIns->iInstance, u64CpuTicksPerSec));
    return u64CpuTicksPerSec;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetSupDrvSession} */
static DECLCALLBACK(PSUPDRVSESSION) pdmR3DevHlp_GetSupDrvSession(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_GetSupDrvSession: caller='%s'/%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    PSUPDRVSESSION pSession = pDevIns->Internal.s.pVMR3->pSession;

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

    void *pvRet;
    PUVM  pUVM = pDevIns->Internal.s.pVMR3->pUVM;
    if (pUVM->pVmm2UserMethods->pfnQueryGenericObject)
        pvRet = pUVM->pVmm2UserMethods->pfnQueryGenericObject(pUVM->pVmm2UserMethods, pUVM, pUuid);
    else
        pvRet = NULL;

    LogRel(("pdmR3DevHlp_QueryGenericUserObject: caller='%s'/%d: returns %#p for %RTuuid\n",
            pDevIns->pReg->szName, pDevIns->iInstance, pvRet, pUuid));
    return pvRet;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPGMHandlerPhysicalTypeRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_PGMHandlerPhysicalTypeRegister(PPDMDEVINS pDevIns, PGMPHYSHANDLERKIND enmKind,
                                                                    PFNPGMPHYSHANDLER pfnHandler, const char *pszDesc,
                                                                    PPGMPHYSHANDLERTYPE phType)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM  pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_PGMHandlerPhysicalTypeRegister: caller='%s'/%d: enmKind=%d pfnHandler=%p pszDesc=%p:{%s} phType=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, enmKind, pfnHandler, pszDesc, pszDesc, phType));

    int rc = PGMR3HandlerPhysicalTypeRegister(pVM, enmKind,
                                              pDevIns->Internal.s.fIntFlags & PDMDEVINSINT_FLAGS_R0_ENABLED
                                              ? PGMPHYSHANDLER_F_R0_DEVINS_IDX : 0,
                                              pfnHandler, pszDesc, phType);

    Log(("pdmR3DevHlp_PGMHandlerPhysicalTypeRegister: caller='%s'/%d: returns %Rrc\n",
         pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPGMHandlerPhysicalRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_PGMHandlerPhysicalRegister(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, RTGCPHYS GCPhysLast,
                                                                PGMPHYSHANDLERTYPE hType, R3PTRTYPE(const char *) pszDesc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM  pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_PGMHandlerPhysicalRegister: caller='%s'/%d: GCPhys=%RGp GCPhysLast=%RGp hType=%u pszDesc=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPhys, GCPhysLast, hType, pszDesc, pszDesc));

    int rc = PGMHandlerPhysicalRegister(pVM, GCPhys, GCPhysLast, hType,
                                        pDevIns->Internal.s.fIntFlags & PDMDEVINSINT_FLAGS_R0_ENABLED
                                        ? pDevIns->Internal.s.idxR0Device : (uintptr_t)pDevIns,
                                        pszDesc);

    Log(("pdmR3DevHlp_PGMHandlerPhysicalRegister: caller='%s'/%d: returns %Rrc\n",
         pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPGMHandlerPhysicalDeregister} */
static DECLCALLBACK(int) pdmR3DevHlp_PGMHandlerPhysicalDeregister(PPDMDEVINS pDevIns, RTGCPHYS GCPhys)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM  pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_PGMHandlerPhysicalDeregister: caller='%s'/%d: GCPhys=%RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, GCPhys));

    int rc = PGMHandlerPhysicalDeregister(pVM, GCPhys);

    Log(("pdmR3DevHlp_PGMHandlerPhysicalDeregister: caller='%s'/%d: returns %Rrc\n",
         pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPGMHandlerPhysicalPageTempOff} */
static DECLCALLBACK(int) pdmR3DevHlp_PGMHandlerPhysicalPageTempOff(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, RTGCPHYS GCPhysPage)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM  pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_PGMHandlerPhysicalPageTempOff: caller='%s'/%d: GCPhys=%RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, GCPhys));

    int rc = PGMHandlerPhysicalPageTempOff(pVM, GCPhys, GCPhysPage);

    Log(("pdmR3DevHlp_PGMHandlerPhysicalPageTempOff: caller='%s'/%d: returns %Rrc\n",
         pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPGMHandlerPhysicalReset} */
static DECLCALLBACK(int) pdmR3DevHlp_PGMHandlerPhysicalReset(PPDMDEVINS pDevIns, RTGCPHYS GCPhys)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM  pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_PGMHandlerPhysicalReset: caller='%s'/%d: GCPhys=%RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, GCPhys));

    int rc = PGMHandlerPhysicalReset(pVM, GCPhys);

    Log(("pdmR3DevHlp_PGMHandlerPhysicalReset: caller='%s'/%d: returns %Rrc\n",
         pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysRead} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysRead(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead, uint32_t fFlags)
{
    RT_NOREF(fFlags);

    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_PhysRead: caller='%s'/%d: GCPhys=%RGp pvBuf=%p cbRead=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPhys, pvBuf, cbRead));

#if defined(VBOX_STRICT) && defined(PDM_DEVHLP_DEADLOCK_DETECTION)
    if (!VM_IS_EMT(pVM))
    {
        char szNames[128];
        uint32_t cLocks = PDMR3CritSectCountOwned(pVM, szNames, sizeof(szNames));
        AssertMsg(cLocks == 0, ("cLocks=%u %s\n", cLocks, szNames));
    }
#endif

    VBOXSTRICTRC rcStrict;
    if (VM_IS_EMT(pVM))
        rcStrict = PGMPhysRead(pVM, GCPhys, pvBuf, cbRead, PGMACCESSORIGIN_DEVICE);
    else
        rcStrict = PGMR3PhysReadExternal(pVM, GCPhys, pvBuf, cbRead, PGMACCESSORIGIN_DEVICE);
    AssertMsg(rcStrict == VINF_SUCCESS, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict))); /** @todo track down the users for this bugger. */

    Log(("pdmR3DevHlp_PhysRead: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict) ));
    return VBOXSTRICTRC_VAL(rcStrict);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysWrite} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysWrite(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite, uint32_t fFlags)
{
    RT_NOREF(fFlags);

    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_PhysWrite: caller='%s'/%d: GCPhys=%RGp pvBuf=%p cbWrite=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPhys, pvBuf, cbWrite));

#if defined(VBOX_STRICT) && defined(PDM_DEVHLP_DEADLOCK_DETECTION)
    if (!VM_IS_EMT(pVM))
    {
        char szNames[128];
        uint32_t cLocks = PDMR3CritSectCountOwned(pVM, szNames, sizeof(szNames));
        AssertMsg(cLocks == 0, ("cLocks=%u %s\n", cLocks, szNames));
    }
#endif

    VBOXSTRICTRC rcStrict;
    if (VM_IS_EMT(pVM))
        rcStrict = PGMPhysWrite(pVM, GCPhys, pvBuf, cbWrite, PGMACCESSORIGIN_DEVICE);
    else
        rcStrict = PGMR3PhysWriteExternal(pVM, GCPhys, pvBuf, cbWrite, PGMACCESSORIGIN_DEVICE);
    AssertMsg(rcStrict == VINF_SUCCESS, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict))); /** @todo track down the users for this bugger. */

    Log(("pdmR3DevHlp_PhysWrite: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict) ));
    return VBOXSTRICTRC_VAL(rcStrict);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysGCPhys2CCPtr} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysGCPhys2CCPtr(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint32_t fFlags, void **ppv, PPGMPAGEMAPLOCK pLock)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_PhysGCPhys2CCPtr: caller='%s'/%d: GCPhys=%RGp fFlags=%#x ppv=%p pLock=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPhys, fFlags, ppv, pLock));
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);

#if defined(VBOX_STRICT) && defined(PDM_DEVHLP_DEADLOCK_DETECTION)
    if (!VM_IS_EMT(pVM))
    {
        char szNames[128];
        uint32_t cLocks = PDMR3CritSectCountOwned(pVM, szNames, sizeof(szNames));
        AssertMsg(cLocks == 0, ("cLocks=%u %s\n", cLocks, szNames));
    }
#endif

    int rc = PGMR3PhysGCPhys2CCPtrExternal(pVM, GCPhys, ppv, pLock);

    Log(("pdmR3DevHlp_PhysGCPhys2CCPtr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysGCPhys2CCPtrReadOnly} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysGCPhys2CCPtrReadOnly(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint32_t fFlags, const void **ppv, PPGMPAGEMAPLOCK pLock)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_PhysGCPhys2CCPtrReadOnly: caller='%s'/%d: GCPhys=%RGp fFlags=%#x ppv=%p pLock=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPhys, fFlags, ppv, pLock));
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);

#if defined(VBOX_STRICT) && defined(PDM_DEVHLP_DEADLOCK_DETECTION)
    if (!VM_IS_EMT(pVM))
    {
        char szNames[128];
        uint32_t cLocks = PDMR3CritSectCountOwned(pVM, szNames, sizeof(szNames));
        AssertMsg(cLocks == 0, ("cLocks=%u %s\n", cLocks, szNames));
    }
#endif

    int rc = PGMR3PhysGCPhys2CCPtrReadOnlyExternal(pVM, GCPhys, ppv, pLock);

    Log(("pdmR3DevHlp_PhysGCPhys2CCPtrReadOnly: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysReleasePageMappingLock} */
static DECLCALLBACK(void) pdmR3DevHlp_PhysReleasePageMappingLock(PPDMDEVINS pDevIns, PPGMPAGEMAPLOCK pLock)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_PhysReleasePageMappingLock: caller='%s'/%d: pLock=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pLock));

    PGMPhysReleasePageMappingLock(pVM, pLock);

    Log(("pdmR3DevHlp_PhysReleasePageMappingLock: caller='%s'/%d: returns void\n", pDevIns->pReg->szName, pDevIns->iInstance));
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysBulkGCPhys2CCPtr} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysBulkGCPhys2CCPtr(PPDMDEVINS pDevIns, uint32_t cPages, PCRTGCPHYS paGCPhysPages,
                                                          uint32_t fFlags, void **papvPages, PPGMPAGEMAPLOCK paLocks)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_PhysBulkGCPhys2CCPtr: caller='%s'/%d: cPages=%#x paGCPhysPages=%p (%RGp,..) fFlags=%#x papvPages=%p paLocks=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cPages, paGCPhysPages, paGCPhysPages[0], fFlags, papvPages, paLocks));
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);
    AssertReturn(cPages > 0, VERR_INVALID_PARAMETER);

#if defined(VBOX_STRICT) && defined(PDM_DEVHLP_DEADLOCK_DETECTION)
    if (!VM_IS_EMT(pVM))
    {
        char szNames[128];
        uint32_t cLocks = PDMR3CritSectCountOwned(pVM, szNames, sizeof(szNames));
        AssertMsg(cLocks == 0, ("cLocks=%u %s\n", cLocks, szNames));
    }
#endif

    int rc = PGMR3PhysBulkGCPhys2CCPtrExternal(pVM, cPages, paGCPhysPages, papvPages, paLocks);

    Log(("pdmR3DevHlp_PhysBulkGCPhys2CCPtr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysBulkGCPhys2CCPtrReadOnly} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysBulkGCPhys2CCPtrReadOnly(PPDMDEVINS pDevIns, uint32_t cPages, PCRTGCPHYS paGCPhysPages,
                                                                  uint32_t fFlags, const void **papvPages, PPGMPAGEMAPLOCK paLocks)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_PhysBulkGCPhys2CCPtrReadOnly: caller='%s'/%d: cPages=%#x paGCPhysPages=%p (%RGp,...) fFlags=%#x papvPages=%p paLocks=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cPages, paGCPhysPages, paGCPhysPages[0], fFlags, papvPages, paLocks));
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);
    AssertReturn(cPages > 0, VERR_INVALID_PARAMETER);

#if defined(VBOX_STRICT) && defined(PDM_DEVHLP_DEADLOCK_DETECTION)
    if (!VM_IS_EMT(pVM))
    {
        char szNames[128];
        uint32_t cLocks = PDMR3CritSectCountOwned(pVM, szNames, sizeof(szNames));
        AssertMsg(cLocks == 0, ("cLocks=%u %s\n", cLocks, szNames));
    }
#endif

    int rc = PGMR3PhysBulkGCPhys2CCPtrReadOnlyExternal(pVM, cPages, paGCPhysPages, papvPages, paLocks);

    Log(("pdmR3DevHlp_PhysBulkGCPhys2CCPtrReadOnly: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysBulkReleasePageMappingLocks} */
static DECLCALLBACK(void) pdmR3DevHlp_PhysBulkReleasePageMappingLocks(PPDMDEVINS pDevIns, uint32_t cPages, PPGMPAGEMAPLOCK paLocks)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_PhysBulkReleasePageMappingLocks: caller='%s'/%d: cPages=%#x paLocks=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cPages, paLocks));
    Assert(cPages > 0);

    PGMPhysBulkReleasePageMappingLocks(pVM, cPages, paLocks);

    Log(("pdmR3DevHlp_PhysBulkReleasePageMappingLocks: caller='%s'/%d: returns void\n", pDevIns->pReg->szName, pDevIns->iInstance));
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysIsGCPhysNormal} */
static DECLCALLBACK(bool) pdmR3DevHlp_PhysIsGCPhysNormal(PPDMDEVINS pDevIns, RTGCPHYS GCPhys)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PhysIsGCPhysNormal: caller='%s'/%d: GCPhys=%RGp\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPhys));

    bool fNormal = PGMPhysIsGCPhysNormal(pDevIns->Internal.s.pVMR3, GCPhys);

    Log(("pdmR3DevHlp_PhysIsGCPhysNormal: caller='%s'/%d: returns %RTbool\n", pDevIns->pReg->szName, pDevIns->iInstance, fNormal));
    return fNormal;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysChangeMemBalloon} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysChangeMemBalloon(PPDMDEVINS pDevIns, bool fInflate, unsigned cPages, RTGCPHYS *paPhysPage)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_PhysChangeMemBalloon: caller='%s'/%d: fInflate=%RTbool cPages=%u paPhysPage=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, fInflate, cPages, paPhysPage));

    int rc = PGMR3PhysChangeMemBalloon(pDevIns->Internal.s.pVMR3, fInflate, cPages, paPhysPage);

    Log(("pdmR3DevHlp_PhysChangeMemBalloon: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCpuGetGuestArch} */
static DECLCALLBACK(CPUMARCH) pdmR3DevHlp_CpuGetGuestArch(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_CpuGetGuestArch: caller='%s'/%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    CPUMARCH enmArch = CPUMGetGuestArch(pVM);

    Log(("pdmR3DevHlp_CpuGetGuestArch: caller='%s'/%d: returns %u\n", pDevIns->pReg->szName, pDevIns->iInstance, enmArch));
    return enmArch;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCpuGetGuestMicroarch} */
static DECLCALLBACK(CPUMMICROARCH) pdmR3DevHlp_CpuGetGuestMicroarch(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_CpuGetGuestMicroarch: caller='%s'/%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    CPUMMICROARCH enmMicroarch = CPUMGetGuestMicroarch(pVM);

    Log(("pdmR3DevHlp_CpuGetGuestMicroarch: caller='%s'/%d: returns %u\n", pDevIns->pReg->szName, pDevIns->iInstance, enmMicroarch));
    return enmMicroarch;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCpuGetGuestAddrWidths} */
static DECLCALLBACK(void) pdmR3DevHlp_CpuGetGuestAddrWidths(PPDMDEVINS pDevIns, uint8_t *pcPhysAddrWidth,
                                                            uint8_t *pcLinearAddrWidth)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    LogFlow(("pdmR3DevHlp_CpuGetGuestAddrWidths: caller='%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    AssertPtrReturnVoid(pcPhysAddrWidth);
    AssertPtrReturnVoid(pcLinearAddrWidth);

    CPUMGetGuestAddrWidths(pVM, pcPhysAddrWidth, pcLinearAddrWidth);

    Log(("pdmR3DevHlp_CpuGetGuestAddrWidths: caller='%s'/%d: returns void\n", pDevIns->pReg->szName, pDevIns->iInstance));
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCpuGetGuestScalableBusFrequency} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_CpuGetGuestScalableBusFrequency(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_CpuGetGuestScalableBusFrequency: caller='%s'/%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

#if defined(VBOX_VMM_TARGET_ARMV8)
    uint64_t u64Fsb = 0;
    AssertReleaseFailed();
    RT_NOREF(pDevIns);
#else
    uint64_t u64Fsb = CPUMGetGuestScalableBusFrequency(pDevIns->Internal.s.pVMR3);
#endif

    Log(("pdmR3DevHlp_CpuGetGuestScalableBusFrequency: caller='%s'/%d: returns %#RX64\n", pDevIns->pReg->szName, pDevIns->iInstance, u64Fsb));
    return u64Fsb;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysReadGCVirt} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysReadGCVirt(PPDMDEVINS pDevIns, void *pvDst, RTGCPTR GCVirtSrc, size_t cb)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DevHlp_PhysReadGCVirt: caller='%s'/%d: pvDst=%p GCVirt=%RGv cb=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pvDst, GCVirtSrc, cb));

    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        return VERR_ACCESS_DENIED;
#if defined(VBOX_STRICT) && defined(PDM_DEVHLP_DEADLOCK_DETECTION)
    /** @todo SMP. */
#endif

    int rc = PGMPhysSimpleReadGCPtr(pVCpu, pvDst, GCVirtSrc, cb);

    LogFlow(("pdmR3DevHlp_PhysReadGCVirt: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));

    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysWriteGCVirt} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysWriteGCVirt(PPDMDEVINS pDevIns, RTGCPTR GCVirtDst, const void *pvSrc, size_t cb)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DevHlp_PhysWriteGCVirt: caller='%s'/%d: GCVirtDst=%RGv pvSrc=%p cb=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCVirtDst, pvSrc, cb));

    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        return VERR_ACCESS_DENIED;
#if defined(VBOX_STRICT) && defined(PDM_DEVHLP_DEADLOCK_DETECTION)
    /** @todo SMP. */
#endif

    int rc = PGMPhysSimpleWriteGCPtr(pVCpu, GCVirtDst, pvSrc, cb);

    LogFlow(("pdmR3DevHlp_PhysWriteGCVirt: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));

    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysGCPtr2GCPhys} */
static DECLCALLBACK(int) pdmR3DevHlp_PhysGCPtr2GCPhys(PPDMDEVINS pDevIns, RTGCPTR GCPtr, PRTGCPHYS pGCPhys)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DevHlp_PhysGCPtr2GCPhys: caller='%s'/%d: GCPtr=%RGv pGCPhys=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPtr, pGCPhys));

    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        return VERR_ACCESS_DENIED;
#if defined(VBOX_STRICT) && defined(PDM_DEVHLP_DEADLOCK_DETECTION)
    /** @todo SMP. */
#endif

    int rc = PGMPhysGCPtr2GCPhys(pVCpu, GCPtr, pGCPhys);

    LogFlow(("pdmR3DevHlp_PhysGCPtr2GCPhys: caller='%s'/%d: returns %Rrc *pGCPhys=%RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, rc, *pGCPhys));

    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMMHeapAlloc} */
static DECLCALLBACK(void *) pdmR3DevHlp_MMHeapAlloc(PPDMDEVINS pDevIns, size_t cb)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MMHeapAlloc: caller='%s'/%d: cb=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, cb));

    void *pv = MMR3HeapAlloc(pDevIns->Internal.s.pVMR3, MM_TAG_PDM_DEVICE_USER, cb);

    LogFlow(("pdmR3DevHlp_MMHeapAlloc: caller='%s'/%d: returns %p\n", pDevIns->pReg->szName, pDevIns->iInstance, pv));
    return pv;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMMHeapAllocZ} */
static DECLCALLBACK(void *) pdmR3DevHlp_MMHeapAllocZ(PPDMDEVINS pDevIns, size_t cb)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MMHeapAllocZ: caller='%s'/%d: cb=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, cb));

    void *pv = MMR3HeapAllocZ(pDevIns->Internal.s.pVMR3, MM_TAG_PDM_DEVICE_USER, cb);

    LogFlow(("pdmR3DevHlp_MMHeapAllocZ: caller='%s'/%d: returns %p\n", pDevIns->pReg->szName, pDevIns->iInstance, pv));
    return pv;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMMHeapAPrintfV} */
static DECLCALLBACK(char *) pdmR3DevHlp_MMHeapAPrintfV(PPDMDEVINS pDevIns, MMTAG enmTag, const char *pszFormat, va_list va)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MMHeapAPrintfV: caller='%s'/%d: enmTag=%u pszFormat=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, enmTag, pszFormat, pszFormat));

    char *psz = MMR3HeapAPrintfV(pDevIns->Internal.s.pVMR3, enmTag, pszFormat, va);

    LogFlow(("pdmR3DevHlp_MMHeapAPrintfV: caller='%s'/%d: returns %p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, psz, psz));
    return psz;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMMHeapFree} */
static DECLCALLBACK(void) pdmR3DevHlp_MMHeapFree(PPDMDEVINS pDevIns, void *pv)
{
    PDMDEV_ASSERT_DEVINS(pDevIns); RT_NOREF_PV(pDevIns);
    LogFlow(("pdmR3DevHlp_MMHeapFree: caller='%s'/%d: pv=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pv));

    MMR3HeapFree(pv);

    LogFlow(("pdmR3DevHlp_MMHeapAlloc: caller='%s'/%d: returns void\n", pDevIns->pReg->szName, pDevIns->iInstance));
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMMPhysGetRamSize} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_MMPhysGetRamSize(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns); RT_NOREF_PV(pDevIns);
    LogFlow(("pdmR3DevHlp_MMPhysGetRamSize: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    uint64_t cb = MMR3PhysGetRamSize(pDevIns->Internal.s.pVMR3);

    LogFlow(("pdmR3DevHlp_MMPhysGetRamSize: caller='%s'/%d: returns %RU64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cb));
    return cb;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMMPhysGetRamSizeBelow4GB} */
static DECLCALLBACK(uint32_t) pdmR3DevHlp_MMPhysGetRamSizeBelow4GB(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns); RT_NOREF_PV(pDevIns);
    LogFlow(("pdmR3DevHlp_MMPhysGetRamSizeBelow4GB: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    uint32_t cb = MMR3PhysGetRamSizeBelow4GB(pDevIns->Internal.s.pVMR3);

    LogFlow(("pdmR3DevHlp_MMPhysGetRamSizeBelow4GB: caller='%s'/%d: returns %RU32\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cb));
    return cb;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMMPhysGetRamSizeAbove4GB} */
static DECLCALLBACK(uint64_t) pdmR3DevHlp_MMPhysGetRamSizeAbove4GB(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns); RT_NOREF_PV(pDevIns);
    LogFlow(("pdmR3DevHlp_MMPhysGetRamSizeAbove4GB: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    uint64_t cb = MMR3PhysGetRamSizeAbove4GB(pDevIns->Internal.s.pVMR3);

    LogFlow(("pdmR3DevHlp_MMPhysGetRamSizeAbove4GB: caller='%s'/%d: returns %RU64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cb));
    return cb;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMState} */
static DECLCALLBACK(VMSTATE) pdmR3DevHlp_VMState(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    VMSTATE enmVMState = VMR3GetState(pDevIns->Internal.s.pVMR3);

    LogFlow(("pdmR3DevHlp_VMState: caller='%s'/%d: returns %d (%s)\n", pDevIns->pReg->szName, pDevIns->iInstance,
             enmVMState, VMR3GetStateName(enmVMState)));
    return enmVMState;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMTeleportedAndNotFullyResumedYet} */
static DECLCALLBACK(bool) pdmR3DevHlp_VMTeleportedAndNotFullyResumedYet(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    bool fRc = VMR3TeleportedAndNotFullyResumedYet(pDevIns->Internal.s.pVMR3);

    LogFlow(("pdmR3DevHlp_VMState: caller='%s'/%d: returns %RTbool\n", pDevIns->pReg->szName, pDevIns->iInstance,
             fRc));
    return fRc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMSetErrorV} */
static DECLCALLBACK(int) pdmR3DevHlp_VMSetErrorV(PPDMDEVINS pDevIns, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list va)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    int rc2 = VMSetErrorV(pDevIns->Internal.s.pVMR3, rc, RT_SRC_POS_ARGS, pszFormat, va); Assert(rc2 == rc); NOREF(rc2);
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMSetRuntimeErrorV} */
static DECLCALLBACK(int) pdmR3DevHlp_VMSetRuntimeErrorV(PPDMDEVINS pDevIns, uint32_t fFlags, const char *pszErrorId, const char *pszFormat, va_list va)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    int rc = VMSetRuntimeErrorV(pDevIns->Internal.s.pVMR3, fFlags, pszErrorId, pszFormat, va);
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMWaitForDeviceReady} */
static DECLCALLBACK(int) pdmR3DevHlp_VMWaitForDeviceReady(PPDMDEVINS pDevIns, VMCPUID idCpu)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_VMWaitForDeviceReady: caller='%s'/%d: idCpu=%u\n", pDevIns->pReg->szName, pDevIns->iInstance, idCpu));

    int rc = VMR3WaitForDeviceReady(pDevIns->Internal.s.pVMR3, idCpu);

    LogFlow(("pdmR3DevHlp_VMWaitForDeviceReady: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMNotifyCpuDeviceReady} */
static DECLCALLBACK(int) pdmR3DevHlp_VMNotifyCpuDeviceReady(PPDMDEVINS pDevIns, VMCPUID idCpu)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_VMNotifyCpuDeviceReady: caller='%s'/%d: idCpu=%u\n", pDevIns->pReg->szName, pDevIns->iInstance, idCpu));

    int rc = VMR3NotifyCpuDeviceReady(pDevIns->Internal.s.pVMR3, idCpu);

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

    int rc = VMR3ReqCallVU(pDevIns->Internal.s.pVMR3->pUVM, idDstCpu, NULL, 0, VMREQFLAGS_VBOX_STATUS | VMREQFLAGS_NO_WAIT,
                           pfnFunction, cArgs, Args);

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

    PVMREQ pReq;
    int rc = VMR3ReqCallVU(pDevIns->Internal.s.pVMR3->pUVM, idDstCpu, &pReq, RT_INDEFINITE_WAIT,
                           VMREQFLAGS_VBOX_STATUS | VMREQFLAGS_PRIORITY, pfnFunction, cArgs, Args);
    if (RT_SUCCESS(rc))
        rc = pReq->iStatus;
    VMR3ReqFree(pReq);

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

    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    int rc = DBGFR3EventSrcV(pVM, DBGFEVENT_DEV_STOP, pszFile, iLine, pszFunction, pszFormat, args);
    if (rc == VERR_DBGF_NOT_ATTACHED)
        rc = VINF_SUCCESS;

    LogFlow(("pdmR3DevHlp_DBGFStopV: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDBGFInfoRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_DBGFInfoRegister(PPDMDEVINS pDevIns, const char *pszName, const char *pszDesc, PFNDBGFHANDLERDEV pfnHandler)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DBGFInfoRegister: caller='%s'/%d: pszName=%p:{%s} pszDesc=%p:{%s} pfnHandler=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pszName, pszName, pszDesc, pszDesc, pfnHandler));

    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    int rc = DBGFR3InfoRegisterDevice(pVM, pszName, pszDesc, pfnHandler, pDevIns);

    LogFlow(("pdmR3DevHlp_DBGFInfoRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDBGFInfoRegisterArgv} */
static DECLCALLBACK(int) pdmR3DevHlp_DBGFInfoRegisterArgv(PPDMDEVINS pDevIns, const char *pszName, const char *pszDesc, PFNDBGFINFOARGVDEV pfnHandler)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DBGFInfoRegisterArgv: caller='%s'/%d: pszName=%p:{%s} pszDesc=%p:{%s} pfnHandler=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pszName, pszName, pszDesc, pszDesc, pfnHandler));

    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    int rc = DBGFR3InfoRegisterDeviceArgv(pVM, pszName, pszDesc, pfnHandler, pDevIns);

    LogFlow(("pdmR3DevHlp_DBGFInfoRegisterArgv: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDBGFRegRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_DBGFRegRegister(PPDMDEVINS pDevIns, PCDBGFREGDESC paRegisters)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DBGFRegRegister: caller='%s'/%d: paRegisters=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, paRegisters));

    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    int rc = DBGFR3RegRegisterDevice(pVM, paRegisters, pDevIns, pDevIns->pReg->szName, pDevIns->iInstance);

    LogFlow(("pdmR3DevHlp_DBGFRegRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDBGFTraceBuf} */
static DECLCALLBACK(RTTRACEBUF) pdmR3DevHlp_DBGFTraceBuf(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RTTRACEBUF hTraceBuf = pDevIns->Internal.s.pVMR3->hTraceBufR3;
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

    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    VBOXSTRICTRC rcStrict = DBGFR3ReportBugCheck(pVM, VMMGetCpu(pVM), enmEvent, uBugCheck, uP1, uP2, uP3, uP4);

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

    int rc = DBGFR3CoreWrite(pDevIns->Internal.s.pVMR3->pUVM, pszFilename, fReplaceFile);

    LogFlow(("pdmR3DevHlp_DBGFCoreWrite: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDBGFInfoLogHlp} */
static DECLCALLBACK(PCDBGFINFOHLP) pdmR3DevHlp_DBGFInfoLogHlp(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns); RT_NOREF(pDevIns);
    LogFlow(("pdmR3DevHlp_DBGFInfoLogHlp: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    PCDBGFINFOHLP pHlp = DBGFR3InfoLogHlp();

    LogFlow(("pdmR3DevHlp_DBGFInfoLogHlp: caller='%s'/%d: returns %p\n", pDevIns->pReg->szName, pDevIns->iInstance, pHlp));
    return pHlp;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDBGFRegNmQueryU64} */
static DECLCALLBACK(int) pdmR3DevHlp_DBGFRegNmQueryU64(PPDMDEVINS pDevIns, VMCPUID idDefCpu, const char *pszReg, uint64_t *pu64)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_DBGFRegNmQueryU64: caller='%s'/%d: idDefCpu=%u pszReg=%p:{%s} pu64=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, idDefCpu, pszReg, pszReg, pu64));

    int rc = DBGFR3RegNmQueryU64(pDevIns->Internal.s.pVMR3->pUVM, idDefCpu, pszReg, pu64);

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

    int rc = DBGFR3RegPrintfV(pDevIns->Internal.s.pVMR3->pUVM, idCpu, pszBuf, cbBuf, pszFormat, va);

    LogFlow(("pdmR3DevHlp_DBGFRegPrintfV: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSTAMRegister} */
static DECLCALLBACK(void) pdmR3DevHlp_STAMRegister(PPDMDEVINS pDevIns, void *pvSample, STAMTYPE enmType, const char *pszName,
                                                   STAMUNIT enmUnit, const char *pszDesc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

    int rc;
    if (*pszName == '/')
        rc = STAMR3Register(pVM, pvSample, enmType, STAMVISIBILITY_ALWAYS, pszName, enmUnit, pszDesc);
    /* Provide default device statistics prefix: */
    else if (pDevIns->pReg->cMaxInstances == 1)
        rc = STAMR3RegisterF(pVM, pvSample, enmType, STAMVISIBILITY_ALWAYS, enmUnit, pszDesc,
                             "/Devices/%s/%s", pDevIns->pReg->szName, pszName);
    else
        rc = STAMR3RegisterF(pVM, pvSample, enmType, STAMVISIBILITY_ALWAYS, enmUnit, pszDesc,
                             "/Devices/%s#%u/%s", pDevIns->pReg->szName, pDevIns->iInstance, pszName);
    AssertRC(rc);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSTAMRegisterV} */
static DECLCALLBACK(void) pdmR3DevHlp_STAMRegisterV(PPDMDEVINS pDevIns, void *pvSample, STAMTYPE enmType, STAMVISIBILITY enmVisibility,
                                                    STAMUNIT enmUnit, const char *pszDesc, const char *pszName, va_list args)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

    int rc;
    if (*pszName == '/')
        rc = STAMR3RegisterV(pVM, pvSample, enmType, enmVisibility, enmUnit, pszDesc, pszName, args);
    else
    {
        /* Provide default device statistics prefix: */
        va_list vaCopy;
        va_copy(vaCopy, args);
        if (pDevIns->pReg->cMaxInstances == 1)
            rc = STAMR3RegisterF(pVM, pvSample, enmType, enmVisibility, enmUnit, pszDesc,
                                 "/Devices/%s/%N", pDevIns->pReg->szName, pszName, &vaCopy);
        else
            rc = STAMR3RegisterF(pVM, pvSample, enmType, enmVisibility, enmUnit, pszDesc,
                                 "/Devices/%s#%u/%N", pDevIns->pReg->szName, pDevIns->iInstance, pszName, &vaCopy);
        va_end(vaCopy);
    }
    AssertRC(rc);
}


/**
 * @interface_method_impl{PDMDEVHLPR3,pfnSTAMDeregisterByPrefix}
 */
static DECLCALLBACK(int) pdmR3DevHlp_STAMDeregisterByPrefix(PPDMDEVINS pDevIns, const char *pszPrefix)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

    int rc;
    if (*pszPrefix == '/')
        rc = STAMR3DeregisterByPrefix(pVM->pUVM, pszPrefix);
    else
    {
        char    szQualifiedPrefix[1024];
        ssize_t cch;
        if (pDevIns->pReg->cMaxInstances == 1)
            cch = RTStrPrintf2(szQualifiedPrefix, sizeof(szQualifiedPrefix), "/Devices/%s/%s", pDevIns->pReg->szName, pszPrefix);
        else
            cch = RTStrPrintf2(szQualifiedPrefix, sizeof(szQualifiedPrefix), "/Devices/%s#%u/%s",
                               pDevIns->pReg->szName, pDevIns->iInstance, pszPrefix);
        AssertReturn(cch > 0, VERR_OUT_OF_RANGE);
        rc = STAMR3DeregisterByPrefix(pVM->pUVM, szQualifiedPrefix);
    }
    AssertRC(rc);
    return rc;
}


/**
 * @interface_method_impl{PDMDEVHLPR3,pfnPCIRegister}
 */
static DECLCALLBACK(int) pdmR3DevHlp_PCIRegister(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t fFlags,
                                                 uint8_t uPciDevNo, uint8_t uPciFunNo, const char *pszName)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
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
    AssertLogRelReturn(RT_VALID_PTR(pszName), VERR_INVALID_POINTER);
    AssertLogRelReturn(!pPciDev->Int.s.fRegistered, VERR_PDM_NOT_PCI_DEVICE);
    AssertLogRelReturn(pPciDev == PDMDEV_GET_PPCIDEV(pDevIns, pPciDev->Int.s.idxSubDev), VERR_PDM_NOT_PCI_DEVICE);
    AssertLogRelReturn(pPciDev == PDMDEV_CALC_PPCIDEV(pDevIns, pPciDev->Int.s.idxSubDev), VERR_PDM_NOT_PCI_DEVICE);
    AssertMsgReturn(pPciDev->u32Magic == PDMPCIDEV_MAGIC, ("%#x\n", pPciDev->u32Magic), VERR_PDM_NOT_PCI_DEVICE);

    /*
     * Check the registration order - must be following PDMDEVINSR3::apPciDevs.
     */
    PPDMPCIDEV const pPrevPciDev = pPciDev->Int.s.idxSubDev == 0 ? NULL
                                 : PDMDEV_GET_PPCIDEV(pDevIns, pPciDev->Int.s.idxSubDev - 1);
    if (pPrevPciDev)
    {
        AssertLogRelReturn(pPrevPciDev->u32Magic == PDMPCIDEV_MAGIC, VERR_INVALID_MAGIC);
        AssertLogRelReturn(pPrevPciDev->Int.s.fRegistered, VERR_WRONG_ORDER);
    }

    /*
     * Resolve the PCI configuration node for the device.  The default (zero'th)
     * is the same as the PDM device, the rest are "PciCfg1..255" CFGM sub-nodes.
     */
    PCFGMNODE pCfg = pDevIns->Internal.s.pCfgHandle;
    if (pPciDev->Int.s.idxSubDev > 0)
        pCfg = CFGMR3GetChildF(pDevIns->Internal.s.pCfgHandle, "PciCfg%u", pPciDev->Int.s.idxSubDev);

    /*
     * We resolve PDMPCIDEVREG_DEV_NO_SAME_AS_PREV, the PCI bus handles
     * PDMPCIDEVREG_DEV_NO_FIRST_UNUSED and PDMPCIDEVREG_FUN_NO_FIRST_UNUSED.
     */
    uint8_t const uPciDevNoRaw = uPciDevNo;
    uint32_t      uDefPciBusNo = 0;
    if (uPciDevNo == PDMPCIDEVREG_DEV_NO_SAME_AS_PREV)
    {
        if (pPrevPciDev)
        {
            uPciDevNo    = pPrevPciDev->uDevFn >> 3;
            uDefPciBusNo = pPrevPciDev->Int.s.idxPdmBus;
        }
        else
        {
            /* Look for PCI device registered with an earlier device instance so we can more
               easily have multiple functions spanning multiple PDM device instances. */
            PPDMDEVINS pPrevIns = pDevIns->Internal.s.pDevR3->pInstances;
            for (;;)
            {
                AssertLogRelMsgReturn(pPrevIns && pPrevIns != pDevIns,
                                      ("'%s'/%d: Can't use PDMPCIDEVREG_DEV_NO_SAME_AS_PREV without a previously registered PCI device by the same or earlier PDM device instance!\n",
                                       pDevIns->pReg->szName, pDevIns->iInstance), VERR_WRONG_ORDER);
                if (pPrevIns->Internal.s.pNextR3 == pDevIns)
                    break;
                pPrevIns = pPrevIns->Internal.s.pNextR3;
            }

            PPDMPCIDEV pOtherPciDev = PDMDEV_GET_PPCIDEV(pPrevIns, 0);
            AssertLogRelMsgReturn(pOtherPciDev && pOtherPciDev->Int.s.fRegistered,
                                  ("'%s'/%d: Can't use PDMPCIDEVREG_DEV_NO_SAME_AS_PREV without a previously registered PCI device by the same or earlier PDM device instance!\n",
                                   pDevIns->pReg->szName, pDevIns->iInstance),
                                  VERR_WRONG_ORDER);
            for (uint32_t iPrevPciDev = 1; iPrevPciDev < pDevIns->cPciDevs; iPrevPciDev++)
            {
                PPDMPCIDEV pCur = PDMDEV_GET_PPCIDEV(pPrevIns, iPrevPciDev);
                AssertBreak(pCur);
                if (!pCur->Int.s.fRegistered)
                    break;
                pOtherPciDev = pCur;
            }

            uPciDevNo    = pOtherPciDev->uDevFn >> 3;
            uDefPciBusNo = pOtherPciDev->Int.s.idxPdmBus;
        }
    }

    /*
     * Choose the PCI bus for the device.
     *
     * This is simple. If the device was configured for a particular bus, the PCIBusNo
     * configuration value will be set. If not the default bus is 0.
     */
    /** @cfgm{/Devices/NAME/XX/[PciCfgYY/]PCIBusNo, uint8_t, 0, 7, 0}
     * Selects the PCI bus number of a device.  The default value isn't necessarily
     * zero if the device is registered using PDMPCIDEVREG_DEV_NO_SAME_AS_PREV, it
     * will then also inherit the bus number from the previously registered device.
     */
    uint8_t u8Bus;
    int rc = CFGMR3QueryU8Def(pCfg, "PCIBusNo", &u8Bus, (uint8_t)uDefPciBusNo);
    AssertLogRelMsgRCReturn(rc, ("Configuration error: PCIBusNo query failed with rc=%Rrc (%s/%d)\n",
                                 rc, pDevIns->pReg->szName, pDevIns->iInstance), rc);
    AssertLogRelMsgReturn(u8Bus < RT_ELEMENTS(pVM->pdm.s.aPciBuses),
                          ("Configuration error: PCIBusNo=%d, max is %d. (%s/%d)\n", u8Bus,
                           RT_ELEMENTS(pVM->pdm.s.aPciBuses), pDevIns->pReg->szName, pDevIns->iInstance),
                          VERR_PDM_NO_PCI_BUS);
    pPciDev->Int.s.idxPdmBus = u8Bus;
    PPDMPCIBUS pBus = &pVM->pdm.s.aPciBuses[u8Bus];
    if (pBus->pDevInsR3)
    {
        /*
         * Check the configuration for PCI device and function assignment.
         */
        /** @cfgm{/Devices/NAME/XX/[PciCfgYY/]PCIDeviceNo, uint8_t, 0, 31}
         * Overrides the default PCI device number of a device.
         */
        uint8_t uCfgDevice;
        rc = CFGMR3QueryU8(pCfg, "PCIDeviceNo", &uCfgDevice);
        if (RT_SUCCESS(rc))
        {
            AssertMsgReturn(uCfgDevice <= 31,
                            ("Configuration error: PCIDeviceNo=%d, max is 31. (%s/%d/%d)\n",
                             uCfgDevice, pDevIns->pReg->szName, pDevIns->iInstance, pPciDev->Int.s.idxSubDev),
                            VERR_PDM_BAD_PCI_CONFIG);
            uPciDevNo = uCfgDevice;
        }
        else
            AssertMsgReturn(rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT,
                            ("Configuration error: PCIDeviceNo query failed with rc=%Rrc (%s/%d/%d)\n",
                             rc, pDevIns->pReg->szName, pDevIns->iInstance, pPciDev->Int.s.idxSubDev),
                            rc);

        /** @cfgm{/Devices/NAME/XX/[PciCfgYY/]PCIFunctionNo, uint8_t, 0, 7}
         * Overrides the default PCI function number of a device.
         */
        uint8_t uCfgFunction;
        rc = CFGMR3QueryU8(pCfg, "PCIFunctionNo", &uCfgFunction);
        if (RT_SUCCESS(rc))
        {
            AssertMsgReturn(uCfgFunction <= 7,
                            ("Configuration error: PCIFunctionNo=%#x, max is 7. (%s/%d/%d)\n",
                             uCfgFunction, pDevIns->pReg->szName, pDevIns->iInstance, pPciDev->Int.s.idxSubDev),
                            VERR_PDM_BAD_PCI_CONFIG);
            uPciFunNo = uCfgFunction;
        }
        else
            AssertMsgReturn(rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT,
                            ("Configuration error: PCIFunctionNo query failed with rc=%Rrc (%s/%d/%d)\n",
                             rc, pDevIns->pReg->szName, pDevIns->iInstance, pPciDev->Int.s.idxSubDev),
                            rc);

#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
        PPDMIOMMUR3 pIommu       = &pVM->pdm.s.aIommus[0];
        PPDMDEVINS  pDevInsIommu = pIommu->CTX_SUFF(pDevIns);
        if (pDevInsIommu)
        {
            /*
             * If the PCI device/function number has been explicitly specified via CFGM,
             * ensure it's not the BDF reserved for the southbridge I/O APIC expected
             * by linux guests when using an AMD IOMMU, see @bugref{9654#c23}.
             *
             * In the Intel IOMMU case, we re-use the same I/O APIC address to reserve a
             * PCI slot so the same check below is sufficient, see @bugref{9967#c13}.
             */
            uint16_t const uDevFn    = VBOX_PCI_DEVFN_MAKE(uPciDevNo, uPciFunNo);
            uint16_t const uBusDevFn = PCIBDF_MAKE(u8Bus, uDevFn);
            if (uBusDevFn == VBOX_PCI_BDF_SB_IOAPIC)
            {
                LogRel(("Configuration error: PCI BDF (%u:%u:%u) conflicts with SB I/O APIC (%s/%d/%d)\n", u8Bus,
                        uCfgDevice, uCfgFunction, pDevIns->pReg->szName, pDevIns->iInstance, pPciDev->Int.s.idxSubDev));
                return VERR_NOT_AVAILABLE;
            }
        }
#endif

        /*
         * Initialize the internal data.  We only do the wipe and the members
         * owned by PDM, the PCI bus does the rest in the registration call.
         */
        RT_ZERO(pPciDev->Int);

        pPciDev->Int.s.idxDevCfg = pPciDev->Int.s.idxSubDev;
        pPciDev->Int.s.fReassignableDevNo = uPciDevNoRaw >= VBOX_PCI_MAX_DEVICES;
        pPciDev->Int.s.fReassignableFunNo = uPciFunNo >= VBOX_PCI_MAX_FUNCTIONS;
        pPciDev->Int.s.pDevInsR3 = pDevIns;
        pPciDev->Int.s.idxPdmBus = u8Bus;
        pPciDev->Int.s.fRegistered = true;

        /* Set some of the public members too. */
        pPciDev->pszNameR3 = pszName;

        /*
         * Call the pci bus device to do the actual registration.
         */
        pdmLock(pVM);
        rc = pBus->pfnRegister(pBus->pDevInsR3, pPciDev, fFlags, uPciDevNo, uPciFunNo, pszName);
        pdmUnlock(pVM);
        if (RT_SUCCESS(rc))
            Log(("PDM: Registered device '%s'/%d as PCI device %d on bus %d\n",
                 pDevIns->pReg->szName, pDevIns->iInstance, pPciDev->uDevFn, pBus->iBus));
        else
            pPciDev->Int.s.fRegistered = false;
    }
    else
    {
        AssertLogRelMsgFailed(("Configuration error: No PCI bus available. This could be related to init order too!\n"));
        rc = VERR_PDM_NO_PCI_BUS;
    }

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

    PVM             pVM    = pDevIns->Internal.s.pVMR3;
    size_t const    idxBus = pPciDev->Int.s.idxPdmBus;
    AssertReturn(idxBus < RT_ELEMENTS(pVM->pdm.s.aPciBuses), VERR_WRONG_ORDER);
    PPDMPCIBUS      pBus   = &pVM->pdm.s.aPciBuses[idxBus];

    pdmLock(pVM);
    int rc;
    if (pBus->pfnRegisterMsi)
        rc = pBus->pfnRegisterMsi(pBus->pDevInsR3, pPciDev, pMsiReg);
    else
        rc = VERR_NOT_IMPLEMENTED;
    pdmUnlock(pVM);

    LogFlow(("pdmR3DevHlp_PCIRegisterMsi: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIIORegionRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_PCIIORegionRegister(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                                         RTGCPHYS cbRegion, PCIADDRESSSPACE enmType, uint32_t fFlags,
                                                         uint64_t hHandle, PFNPCIIOREGIONMAP pfnMapUnmap)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    if (!pPciDev) /* NULL is an alias for the default PCI device. */
        pPciDev = pDevIns->apPciDevs[0];
    AssertReturn(pPciDev, VERR_PDM_NOT_PCI_DEVICE);
    LogFlow(("pdmR3DevHlp_PCIIORegionRegister: caller='%s'/%d: pPciDev=%p:{%#x} iRegion=%d cbRegion=%RGp enmType=%d fFlags=%#x, hHandle=%#RX64 pfnMapUnmap=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPciDev, pPciDev->uDevFn, iRegion, cbRegion, enmType, fFlags, hHandle, pfnMapUnmap));
    PDMPCIDEV_ASSERT_VALID_RET(pDevIns, pPciDev);

    /*
     * Validate input.
     */
    VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    AssertLogRelMsgReturn(VMR3GetState(pVM) == VMSTATE_CREATING,
                          ("caller='%s'/%d: %s\n", pDevIns->pReg->szName, pDevIns->iInstance, VMR3GetStateName(VMR3GetState(pVM))),
                          VERR_WRONG_ORDER);

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
    int rc;
    switch (fFlags & PDMPCIDEV_IORGN_F_HANDLE_MASK)
    {
        case PDMPCIDEV_IORGN_F_NO_HANDLE:
            break;
        case PDMPCIDEV_IORGN_F_IOPORT_HANDLE:
            AssertReturn(enmType == PCI_ADDRESS_SPACE_IO, VERR_INVALID_FLAGS);
            rc = IOMR3IoPortValidateHandle(pVM, pDevIns, (IOMIOPORTHANDLE)hHandle);
            AssertRCReturn(rc, rc);
            break;
        case PDMPCIDEV_IORGN_F_MMIO_HANDLE:
            AssertReturn(   (enmType & ~PCI_ADDRESS_SPACE_BAR64) == PCI_ADDRESS_SPACE_MEM
                         || (enmType & ~PCI_ADDRESS_SPACE_BAR64) == PCI_ADDRESS_SPACE_MEM_PREFETCH,
                         VERR_INVALID_FLAGS);
            rc = IOMR3MmioValidateHandle(pVM, pDevIns, (IOMMMIOHANDLE)hHandle);
            AssertRCReturn(rc, rc);
            break;
        case PDMPCIDEV_IORGN_F_MMIO2_HANDLE:
            AssertReturn(   (enmType & ~PCI_ADDRESS_SPACE_BAR64) == PCI_ADDRESS_SPACE_MEM
                         || (enmType & ~PCI_ADDRESS_SPACE_BAR64) == PCI_ADDRESS_SPACE_MEM_PREFETCH,
                         VERR_INVALID_FLAGS);
            rc = PGMR3PhysMmio2ValidateHandle(pVM, pDevIns, (PGMMMIO2HANDLE)hHandle);
            AssertRCReturn(rc, rc);
            break;
        default:
            AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE);
            break;
    }

    /* This flag is required now. */
    AssertLogRelMsgReturn(fFlags & PDMPCIDEV_IORGN_F_NEW_STYLE,
                          ("'%s'/%d: Invalid flags: %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, fFlags),
                          VERR_INVALID_FLAGS);

    /*
     * We're currently restricted to page aligned MMIO regions.
     */
    if (   ((enmType & ~(PCI_ADDRESS_SPACE_BAR64 | PCI_ADDRESS_SPACE_MEM_PREFETCH)) == PCI_ADDRESS_SPACE_MEM)
        && cbRegion != RT_ALIGN_64(cbRegion, GUEST_PAGE_SIZE))
    {
        Log(("pdmR3DevHlp_PCIIORegionRegister: caller='%s'/%d: aligning cbRegion %RGp -> %RGp\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cbRegion, RT_ALIGN_64(cbRegion, GUEST_PAGE_SIZE)));
        cbRegion = RT_ALIGN_64(cbRegion, GUEST_PAGE_SIZE);
    }

    /*
     * For registering PCI MMIO memory or PCI I/O memory, the size of the region must be a power of 2!
     */
    int iLastSet = ASMBitLastSetU64(cbRegion);
    Assert(iLastSet > 0);
    uint64_t cbRegionAligned = RT_BIT_64(iLastSet - 1);
    if (cbRegion > cbRegionAligned)
        cbRegion = cbRegionAligned * 2; /* round up */

    size_t const    idxBus = pPciDev->Int.s.idxPdmBus;
    AssertReturn(idxBus < RT_ELEMENTS(pVM->pdm.s.aPciBuses), VERR_WRONG_ORDER);
    PPDMPCIBUS      pBus   = &pVM->pdm.s.aPciBuses[idxBus];

    pdmLock(pVM);
    rc = pBus->pfnIORegionRegister(pBus->pDevInsR3, pPciDev, iRegion, cbRegion, enmType, fFlags, hHandle, pfnMapUnmap);
    pdmUnlock(pVM);

    LogFlow(("pdmR3DevHlp_PCIIORegionRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIInterceptConfigAccesses} */
static DECLCALLBACK(int) pdmR3DevHlp_PCIInterceptConfigAccesses(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                                PFNPCICONFIGREAD pfnRead, PFNPCICONFIGWRITE pfnWrite)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    if (!pPciDev) /* NULL is an alias for the default PCI device. */
        pPciDev = pDevIns->apPciDevs[0];
    AssertReturn(pPciDev, VERR_PDM_NOT_PCI_DEVICE);
    LogFlow(("pdmR3DevHlp_PCIInterceptConfigAccesses: caller='%s'/%d: pPciDev=%p pfnRead=%p pfnWrite=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPciDev, pfnRead, pfnWrite));
    PDMPCIDEV_ASSERT_VALID_RET(pDevIns, pPciDev);

    /*
     * Validate input.
     */
    AssertPtr(pfnRead);
    AssertPtr(pfnWrite);
    AssertPtr(pPciDev);

    size_t const    idxBus = pPciDev->Int.s.idxPdmBus;
    AssertReturn(idxBus < RT_ELEMENTS(pVM->pdm.s.aPciBuses), VERR_INTERNAL_ERROR_2);
    PPDMPCIBUS      pBus   = &pVM->pdm.s.aPciBuses[idxBus];
    AssertRelease(VMR3GetState(pVM) != VMSTATE_RUNNING);

    /*
     * Do the job.
     */
    pdmLock(pVM);
    pBus->pfnInterceptConfigAccesses(pBus->pDevInsR3, pPciDev, pfnRead, pfnWrite);
    pdmUnlock(pVM);

    LogFlow(("pdmR3DevHlp_PCIInterceptConfigAccesses: caller='%s'/%d: returns VINF_SUCCESS\n",
             pDevIns->pReg->szName, pDevIns->iInstance));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIConfigWrite} */
static DECLCALLBACK(VBOXSTRICTRC)
pdmR3DevHlp_PCIConfigWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t uAddress, unsigned cb, uint32_t u32Value)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    AssertPtrReturn(pPciDev, VERR_PDM_NOT_PCI_DEVICE);
    LogFlow(("pdmR3DevHlp_PCIConfigWrite: caller='%s'/%d: pPciDev=%p uAddress=%#x cd=%d u32Value=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPciDev, uAddress, cb, u32Value));

    /*
     * Resolve the bus.
     */
    size_t const    idxBus = pPciDev->Int.s.idxPdmBus;
    AssertReturn(idxBus < RT_ELEMENTS(pVM->pdm.s.aPciBuses), VERR_INTERNAL_ERROR_2);
    PPDMPCIBUS      pBus   = &pVM->pdm.s.aPciBuses[idxBus];

    /*
     * Do the job.
     */
    VBOXSTRICTRC rcStrict = pBus->pfnConfigWrite(pBus->pDevInsR3, pPciDev, uAddress, cb, u32Value);

    LogFlow(("pdmR3DevHlp_PCIConfigWrite: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIConfigRead} */
static DECLCALLBACK(VBOXSTRICTRC)
pdmR3DevHlp_PCIConfigRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t uAddress, unsigned cb, uint32_t *pu32Value)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    AssertPtrReturn(pPciDev, VERR_PDM_NOT_PCI_DEVICE);
    LogFlow(("pdmR3DevHlp_PCIConfigRead: caller='%s'/%d: pPciDev=%p uAddress=%#x cd=%d pu32Value=%p:{%#x}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPciDev, uAddress, cb, pu32Value, *pu32Value));

    /*
     * Resolve the bus.
     */
    size_t const    idxBus = pPciDev->Int.s.idxPdmBus;
    AssertReturn(idxBus < RT_ELEMENTS(pVM->pdm.s.aPciBuses), VERR_INTERNAL_ERROR_2);
    PPDMPCIBUS      pBus   = &pVM->pdm.s.aPciBuses[idxBus];

    /*
     * Do the job.
     */
    VBOXSTRICTRC rcStrict = pBus->pfnConfigRead(pBus->pDevInsR3, pPciDev, uAddress, cb, pu32Value);

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
    PDMPCIDEV_ASSERT_VALID_AND_REGISTERED(pDevIns, pPciDev);

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

#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
    int rc = pdmIommuMemAccessRead(pDevIns, pPciDev, GCPhys, pvBuf, cbRead, fFlags);
    if (   rc == VERR_IOMMU_NOT_PRESENT
        || rc == VERR_IOMMU_CANNOT_CALL_SELF)
    { /* likely - ASSUMING most VMs won't be configured with an IOMMU. */ }
    else
        return rc;
#endif

    return pDevIns->pHlpR3->pfnPhysRead(pDevIns, GCPhys, pvBuf, cbRead, fFlags);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIPhysWrite} */
static DECLCALLBACK(int)
pdmR3DevHlp_PCIPhysWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite, uint32_t fFlags)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    if (!pPciDev) /* NULL is an alias for the default PCI device. */
        pPciDev = pDevIns->apPciDevs[0];
    AssertReturn(pPciDev, VERR_PDM_NOT_PCI_DEVICE);
    PDMPCIDEV_ASSERT_VALID_AND_REGISTERED(pDevIns, pPciDev);

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

#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
    int rc = pdmIommuMemAccessWrite(pDevIns, pPciDev, GCPhys, pvBuf, cbWrite, fFlags);
    if (   rc == VERR_IOMMU_NOT_PRESENT
        || rc == VERR_IOMMU_CANNOT_CALL_SELF)
    { /* likely - ASSUMING most VMs won't be configured with an IOMMU. */ }
    else
        return rc;
#endif

    return pDevIns->pHlpR3->pfnPhysWrite(pDevIns, GCPhys, pvBuf, cbWrite, fFlags);
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

#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
    int rc = pdmR3IommuMemAccessWriteCCPtr(pDevIns, pPciDev, GCPhys, fFlags, ppv, pLock);
    if (   rc == VERR_IOMMU_NOT_PRESENT
        || rc == VERR_IOMMU_CANNOT_CALL_SELF)
    { /* likely - ASSUMING most VMs won't be configured with an IOMMU. */ }
    else
        return rc;
#endif

    return pDevIns->pHlpR3->pfnPhysGCPhys2CCPtr(pDevIns, GCPhys, fFlags, ppv, pLock);
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

#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
    int rc = pdmR3IommuMemAccessReadCCPtr(pDevIns, pPciDev, GCPhys, fFlags, ppv, pLock);
    if (   rc == VERR_IOMMU_NOT_PRESENT
        || rc == VERR_IOMMU_CANNOT_CALL_SELF)
    { /* likely - ASSUMING most VMs won't be configured with an IOMMU. */ }
    else
        return rc;
#endif

    return pDevIns->pHlpR3->pfnPhysGCPhys2CCPtrReadOnly(pDevIns, GCPhys, fFlags, ppv, pLock);
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

#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
    int rc = pdmR3IommuMemAccessBulkWriteCCPtr(pDevIns, pPciDev, cPages, paGCPhysPages, fFlags, papvPages, paLocks);
    if (   rc == VERR_IOMMU_NOT_PRESENT
        || rc == VERR_IOMMU_CANNOT_CALL_SELF)
    { /* likely - ASSUMING most VMs won't be configured with an IOMMU. */ }
    else
        return rc;
#endif

    return pDevIns->pHlpR3->pfnPhysBulkGCPhys2CCPtr(pDevIns, cPages, paGCPhysPages, fFlags, papvPages, paLocks);
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

#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
    int rc = pdmR3IommuMemAccessBulkReadCCPtr(pDevIns, pPciDev, cPages, paGCPhysPages, fFlags, papvPages, paLocks);
    if (   rc == VERR_IOMMU_NOT_PRESENT
        || rc == VERR_IOMMU_CANNOT_CALL_SELF)
    { /* likely - ASSUMING most VMs won't be configured with an IOMMU. */ }
    else
        return rc;
#endif

    return pDevIns->pHlpR3->pfnPhysBulkGCPhys2CCPtrReadOnly(pDevIns, cPages, paGCPhysPages, fFlags, papvPages, paLocks);
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
    PDMPCIDEV_ASSERT_VALID_AND_REGISTERED(pDevIns, pPciDev);

    /*
     * Validate input.
     */
    Assert(iIrq == 0);
    Assert((uint32_t)iLevel <= PDM_IRQ_LEVEL_FLIP_FLOP);

    /*
     * Must have a PCI device registered!
     */
    PVM             pVM    = pDevIns->Internal.s.pVMR3;
    size_t const    idxBus = pPciDev->Int.s.idxPdmBus;
    AssertReturnVoid(idxBus < RT_ELEMENTS(pVM->pdm.s.aPciBuses));
    PPDMPCIBUS      pBus   = &pVM->pdm.s.aPciBuses[idxBus];

    pdmLock(pVM);
    uint32_t uTagSrc;
    if (iLevel & PDM_IRQ_LEVEL_HIGH)
    {
        pDevIns->Internal.s.uLastIrqTag = uTagSrc = pdmCalcIrqTag(pVM, pDevIns->idTracing);
        if (iLevel == PDM_IRQ_LEVEL_HIGH)
            VBOXVMM_PDM_IRQ_HIGH(VMMGetCpu(pVM), RT_LOWORD(uTagSrc), RT_HIWORD(uTagSrc));
        else
            VBOXVMM_PDM_IRQ_HILO(VMMGetCpu(pVM), RT_LOWORD(uTagSrc), RT_HIWORD(uTagSrc));
    }
    else
        uTagSrc = pDevIns->Internal.s.uLastIrqTag;

    pBus->pfnSetIrqR3(pBus->pDevInsR3, pPciDev, iIrq, iLevel, uTagSrc);

    if (iLevel == PDM_IRQ_LEVEL_LOW)
        VBOXVMM_PDM_IRQ_LOW(VMMGetCpu(pVM), RT_LOWORD(uTagSrc), RT_HIWORD(uTagSrc));
    pdmUnlock(pVM);

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
#ifndef VBOX_VMM_TARGET_ARMV8
    Assert(iIrq < 16);
#endif
    Assert((uint32_t)iLevel <= PDM_IRQ_LEVEL_FLIP_FLOP);

    PVM pVM = pDevIns->Internal.s.pVMR3;

    /*
     * Do the job.
     */
    pdmLock(pVM);
    uint32_t uTagSrc;
    if (iLevel & PDM_IRQ_LEVEL_HIGH)
    {
        pDevIns->Internal.s.uLastIrqTag = uTagSrc = pdmCalcIrqTag(pVM, pDevIns->idTracing);
        if (iLevel == PDM_IRQ_LEVEL_HIGH)
            VBOXVMM_PDM_IRQ_HIGH(VMMGetCpu(pVM), RT_LOWORD(uTagSrc), RT_HIWORD(uTagSrc));
        else
            VBOXVMM_PDM_IRQ_HILO(VMMGetCpu(pVM), RT_LOWORD(uTagSrc), RT_HIWORD(uTagSrc));
    }
    else
        uTagSrc = pDevIns->Internal.s.uLastIrqTag;

    PDMIsaSetIrq(pVM, iIrq, iLevel, uTagSrc);  /* (The API takes the lock recursively.) */

    if (iLevel == PDM_IRQ_LEVEL_LOW)
        VBOXVMM_PDM_IRQ_LOW(VMMGetCpu(pVM), RT_LOWORD(uTagSrc), RT_HIWORD(uTagSrc));
    pdmUnlock(pVM);

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
    PVM const pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DevHlp_DriverAttach: caller='%s'/%d: iLun=%d pBaseInterface=%p ppBaseInterface=%p pszDesc=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, iLun, pBaseInterface, ppBaseInterface, pszDesc, pszDesc));

    /*
     * Lookup the LUN, it might already be registered.
     */
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    PPDMLUN pLunPrev = NULL;
    PPDMLUN pLun = pDevIns->Internal.s.pLunsR3;
    for (; pLun; pLunPrev = pLun, pLun = pLun->pNext)
        if (pLun->iLun == iLun)
            break;

    /*
     * Create the LUN if if wasn't found, else check if driver is already attached to it.
     */
    if (!pLun)
    {
        if (   !pBaseInterface
            || !pszDesc
            || !*pszDesc)
        {
            RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
            Assert(pBaseInterface);
            Assert(pszDesc || *pszDesc);
            return VERR_INVALID_PARAMETER;
        }

        pLun = (PPDMLUN)MMR3HeapAlloc(pVM, MM_TAG_PDM_LUN, sizeof(*pLun));
        if (!pLun)
        {
            RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
            return VERR_NO_MEMORY;
        }

        pLun->iLun      = iLun;
        pLun->pNext     = pLunPrev ? pLunPrev->pNext : NULL;
        pLun->pTop      = NULL;
        pLun->pBottom   = NULL;
        pLun->pDevIns   = pDevIns;
        pLun->pUsbIns   = NULL;
        pLun->pszDesc   = pszDesc;
        pLun->pBase     = pBaseInterface;
        if (!pLunPrev)
            pDevIns->Internal.s.pLunsR3 = pLun;
        else
            pLunPrev->pNext = pLun;
        Log(("pdmR3DevHlp_DriverAttach: Registered LUN#%d '%s' with device '%s'/%d.\n",
             iLun, pszDesc, pDevIns->pReg->szName, pDevIns->iInstance));
    }
    else if (pLun->pTop)
    {
        RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
        AssertMsgFailed(("Already attached! The device should keep track of such things!\n"));
        LogFlow(("pdmR3DevHlp_DriverAttach: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VERR_PDM_DRIVER_ALREADY_ATTACHED));
        return VERR_PDM_DRIVER_ALREADY_ATTACHED;
    }
    Assert(pLun->pBase == pBaseInterface);


    /*
     * Get the attached driver configuration.
     */
    int rc;
    PCFGMNODE const pNode = CFGMR3GetChildF(pDevIns->Internal.s.pCfgHandle, "LUN#%u", iLun);
    if (pNode)
        rc = pdmR3DrvInstantiate(pVM, pNode, pBaseInterface, NULL /*pDrvAbove*/, pLun, ppBaseInterface);
    else
        rc = VERR_PDM_NO_ATTACHED_DRIVER;

    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

    LogFlow(("pdmR3DevHlp_DriverAttach: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDriverDetach} */
static DECLCALLBACK(int) pdmR3DevHlp_DriverDetach(PPDMDEVINS pDevIns, PPDMDRVINS pDrvIns, uint32_t fFlags)
{
    PDMDEV_ASSERT_DEVINS(pDevIns); RT_NOREF_PV(pDevIns);
    LogFlow(("pdmR3DevHlp_DriverDetach: caller='%s'/%d: pDrvIns=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pDrvIns));

    PVM const pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);
    int rc = pdmR3DrvDetach(pVM, pDrvIns, fFlags);
    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

    LogFlow(("pdmR3DevHlp_DriverDetach: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDriverReconfigure} */
static DECLCALLBACK(int) pdmR3DevHlp_DriverReconfigure(PPDMDEVINS pDevIns, uint32_t iLun, uint32_t cDepth,
                                                       const char * const *papszDrivers, PCFGMNODE *papConfigs, uint32_t fFlags)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM const pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
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

    /*
     * Exclusively lock the core stuff (mind, pdmR3DrvDetach will leave + reenter).
     */
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    /*
     * Do we have to detach an existing driver first?
     */
    for (PPDMLUN pLun = pDevIns->Internal.s.pLunsR3; pLun; pLun = pLun->pNext)
        if (pLun->iLun == iLun)
        {
            if (pLun->pTop)
            {
                int rc = pdmR3DrvDetach(pVM, pLun->pTop, 0);
                AssertRCReturnStmt(rc, RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw), rc);
            }
            break;
        }

    /*
     * Remove the old tree.
     */
    PCFGMNODE pCfgDev = CFGMR3GetChildF(CFGMR3GetRoot(pVM), "Devices/%s/%u/", pDevIns->pReg->szName, pDevIns->iInstance);
    AssertReturn(pCfgDev, VERR_INTERNAL_ERROR_2);
    PCFGMNODE pCfgLun = CFGMR3GetChildF(pCfgDev, "LUN#%u", iLun);
    if (pCfgLun)
        CFGMR3RemoveNode(pCfgLun);

    /*
     * Construct a new tree.
     */
    int rc = CFGMR3InsertNodeF(pCfgDev, &pCfgLun, "LUN#%u", iLun);
    AssertRCReturn(rc, rc);
    PCFGMNODE pCfgDrv = pCfgLun;
    for (uint32_t i = 0; i < cDepth; i++)
    {
        rc = CFGMR3InsertString(pCfgDrv, "Driver", papszDrivers[i]);
        AssertRCReturn(rc, rc);
        if (papConfigs && papConfigs[i])
        {
            rc = CFGMR3InsertSubTree(pCfgDrv, "Config", papConfigs[i], NULL);
            AssertRCReturn(rc, rc);
            papConfigs[i] = NULL;
        }
        else
        {
            rc = CFGMR3InsertNode(pCfgDrv, "Config", NULL);
            AssertRCReturn(rc, rc);
        }

        if (i + 1 >= cDepth)
            break;
        rc = CFGMR3InsertNode(pCfgDrv, "AttachedDriver", &pCfgDrv);
        AssertRCReturn(rc, rc);
    }

    /* Keep the lock till the end to serialize the CFGM changes. */
    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
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

    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

    if (pDevIns->iInstance > 0)
    {
        pszName = MMR3HeapAPrintf(pVM, MM_TAG_PDM_DEVICE_DESC, "%s_%u", pszName, pDevIns->iInstance);
        AssertLogRelReturn(pszName, VERR_NO_MEMORY);
    }

    int rc = PDMR3QueueCreateDevice(pVM, pDevIns, cbItem, cItems, cMilliesInterval, pfnCallback, fRZEnabled, pszName, phQueue);

    LogFlow(("pdmR3DevHlp_QueueCreate: caller='%s'/%d: returns %Rrc *phQueue=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc, *phQueue));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnQueueAlloc} */
static DECLCALLBACK(PPDMQUEUEITEMCORE) pdmR3DevHlp_QueueAlloc(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMQueueAlloc(pDevIns->Internal.s.pVMR3, hQueue, pDevIns);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnQueueInsert} */
static DECLCALLBACK(int) pdmR3DevHlp_QueueInsert(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue, PPDMQUEUEITEMCORE pItem)
{
    return PDMQueueInsert(pDevIns->Internal.s.pVMR3, hQueue, pDevIns, pItem);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnQueueFlushIfNecessary} */
static DECLCALLBACK(bool) pdmR3DevHlp_QueueFlushIfNecessary(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue)
{
    return PDMQueueFlushIfNecessary(pDevIns->Internal.s.pVMR3, hQueue, pDevIns) == VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTaskCreate} */
static DECLCALLBACK(int) pdmR3DevHlp_TaskCreate(PPDMDEVINS pDevIns, uint32_t fFlags, const char *pszName,
                                                PFNPDMTASKDEV pfnCallback, void *pvUser, PDMTASKHANDLE *phTask)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_TaskTrigger: caller='%s'/%d: pfnCallback=%p fFlags=%#x pszName=%p:{%s} phTask=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pfnCallback, fFlags, pszName, pszName, phTask));
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

    int rc = PDMR3TaskCreate(pVM, fFlags, pszName, PDMTASKTYPE_DEV, pDevIns, (PFNRT)pfnCallback, pvUser, phTask);

    LogFlow(("pdmR3DevHlp_TaskTrigger: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnTaskTrigger} */
static DECLCALLBACK(int) pdmR3DevHlp_TaskTrigger(PPDMDEVINS pDevIns, PDMTASKHANDLE hTask)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_TaskTrigger: caller='%s'/%d: hTask=%RU64\n", pDevIns->pReg->szName, pDevIns->iInstance, hTask));

    int rc = PDMTaskTrigger(pDevIns->Internal.s.pVMR3, PDMTASKTYPE_DEV, pDevIns, hTask);

    LogFlow(("pdmR3DevHlp_TaskTrigger: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventCreate} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventCreate(PPDMDEVINS pDevIns, PSUPSEMEVENT phEvent)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventCreate: caller='%s'/%d: phEvent=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, phEvent));
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

    int rc = SUPSemEventCreate(pVM->pSession, phEvent);

    LogFlow(("pdmR3DevHlp_SUPSemEventCreate: caller='%s'/%d: returns %Rrc *phEvent=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, rc, *phEvent));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventClose} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventClose(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventClose: caller='%s'/%d: hEvent=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, hEvent));

    int rc = SUPSemEventClose(pDevIns->Internal.s.pVMR3->pSession, hEvent);

    LogFlow(("pdmR3DevHlp_SUPSemEventClose: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventSignal} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventSignal(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventSignal: caller='%s'/%d: hEvent=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, hEvent));

    int rc = SUPSemEventSignal(pDevIns->Internal.s.pVMR3->pSession, hEvent);

    LogFlow(("pdmR3DevHlp_SUPSemEventSignal: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventWaitNoResume} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventWaitNoResume(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint32_t cMillies)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventWaitNoResume: caller='%s'/%d: hEvent=%p cNsTimeout=%RU32\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEvent, cMillies));

    int rc = SUPSemEventWaitNoResume(pDevIns->Internal.s.pVMR3->pSession, hEvent, cMillies);

    LogFlow(("pdmR3DevHlp_SUPSemEventWaitNoResume: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventWaitNsAbsIntr} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventWaitNsAbsIntr(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint64_t uNsTimeout)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventWaitNsAbsIntr: caller='%s'/%d: hEvent=%p uNsTimeout=%RU64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEvent, uNsTimeout));

    int rc = SUPSemEventWaitNsAbsIntr(pDevIns->Internal.s.pVMR3->pSession, hEvent, uNsTimeout);

    LogFlow(("pdmR3DevHlp_SUPSemEventWaitNsAbsIntr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventWaitNsRelIntr} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventWaitNsRelIntr(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint64_t cNsTimeout)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventWaitNsRelIntr: caller='%s'/%d: hEvent=%p cNsTimeout=%RU64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEvent, cNsTimeout));

    int rc = SUPSemEventWaitNsRelIntr(pDevIns->Internal.s.pVMR3->pSession, hEvent, cNsTimeout);

    LogFlow(("pdmR3DevHlp_SUPSemEventWaitNsRelIntr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventGetResolution} */
static DECLCALLBACK(uint32_t) pdmR3DevHlp_SUPSemEventGetResolution(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventGetResolution: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    uint32_t cNsResolution = SUPSemEventGetResolution(pDevIns->Internal.s.pVMR3->pSession);

    LogFlow(("pdmR3DevHlp_SUPSemEventGetResolution: caller='%s'/%d: returns %u\n", pDevIns->pReg->szName, pDevIns->iInstance, cNsResolution));
    return cNsResolution;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventMultiCreate} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventMultiCreate(PPDMDEVINS pDevIns, PSUPSEMEVENTMULTI phEventMulti)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventMultiCreate: caller='%s'/%d: phEventMulti=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, phEventMulti));
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

    int rc = SUPSemEventMultiCreate(pVM->pSession, phEventMulti);

    LogFlow(("pdmR3DevHlp_SUPSemEventMultiCreate: caller='%s'/%d: returns %Rrc *phEventMulti=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, rc, *phEventMulti));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventMultiClose} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventMultiClose(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventMultiClose: caller='%s'/%d: hEventMulti=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, hEventMulti));

    int rc = SUPSemEventMultiClose(pDevIns->Internal.s.pVMR3->pSession, hEventMulti);

    LogFlow(("pdmR3DevHlp_SUPSemEventMultiClose: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventMultiSignal} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventMultiSignal(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventMultiSignal: caller='%s'/%d: hEventMulti=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, hEventMulti));

    int rc = SUPSemEventMultiSignal(pDevIns->Internal.s.pVMR3->pSession, hEventMulti);

    LogFlow(("pdmR3DevHlp_SUPSemEventMultiSignal: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventMultiReset} */
static DECLCALLBACK(int) pdmR3DevHlp_SUPSemEventMultiReset(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventMultiReset: caller='%s'/%d: hEventMulti=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, hEventMulti));

    int rc = SUPSemEventMultiReset(pDevIns->Internal.s.pVMR3->pSession, hEventMulti);

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

    int rc = SUPSemEventMultiWaitNoResume(pDevIns->Internal.s.pVMR3->pSession, hEventMulti, cMillies);

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

    int rc = SUPSemEventMultiWaitNsAbsIntr(pDevIns->Internal.s.pVMR3->pSession, hEventMulti, uNsTimeout);

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

    int rc = SUPSemEventMultiWaitNsRelIntr(pDevIns->Internal.s.pVMR3->pSession, hEventMulti, cNsTimeout);

    LogFlow(("pdmR3DevHlp_SUPSemEventMultiWaitNsRelIntr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSUPSemEventMultiGetResolution} */
static DECLCALLBACK(uint32_t) pdmR3DevHlp_SUPSemEventMultiGetResolution(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_SUPSemEventMultiGetResolution: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    uint32_t cNsResolution = SUPSemEventMultiGetResolution(pDevIns->Internal.s.pVMR3->pSession);

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

    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    int rc = pdmR3CritSectInitDevice(pVM, pDevIns, pCritSect, RT_SRC_POS_ARGS, pszNameFmt, va);

    LogFlow(("pdmR3DevHlp_CritSectInit: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectGetNop} */
static DECLCALLBACK(PPDMCRITSECT) pdmR3DevHlp_CritSectGetNop(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

    PPDMCRITSECT pCritSect = PDMR3CritSectGetNop(pVM);
    LogFlow(("pdmR3DevHlp_CritSectGetNop: caller='%s'/%d: return %p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pCritSect));
    return pCritSect;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSetDeviceCritSect} */
static DECLCALLBACK(int) pdmR3DevHlp_SetDeviceCritSect(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    /*
     * Validate input.
     *
     * Note! We only allow the automatically created default critical section
     *       to be replaced by this API.
     */
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertPtrReturn(pCritSect, VERR_INVALID_POINTER);
    LogFlow(("pdmR3DevHlp_SetDeviceCritSect: caller='%s'/%d: pCritSect=%p (%s)\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pCritSect, pCritSect->s.pszName));
    AssertReturn(PDMCritSectIsInitialized(pCritSect), VERR_INVALID_PARAMETER);
    PVM pVM = pDevIns->Internal.s.pVMR3;

    VM_ASSERT_EMT(pVM);
    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_CREATING, VERR_WRONG_ORDER);

    AssertReturn(pDevIns->pCritSectRoR3, VERR_PDM_DEV_IPE_1);
    AssertReturn(pDevIns->pCritSectRoR3->s.fAutomaticDefaultCritsect, VERR_WRONG_ORDER);
    AssertReturn(!pDevIns->pCritSectRoR3->s.fUsedByTimerOrSimilar, VERR_WRONG_ORDER);
    AssertReturn(pDevIns->pCritSectRoR3 != pCritSect, VERR_INVALID_PARAMETER);

    /*
     * Replace the critical section and destroy the automatic default section.
     */
    PPDMCRITSECT pOldCritSect = pDevIns->pCritSectRoR3;
    pDevIns->pCritSectRoR3 = pCritSect;
    pDevIns->Internal.s.fIntFlags |= PDMDEVINSINT_FLAGS_CHANGED_CRITSECT;

    Assert(RT_BOOL(pDevIns->Internal.s.fIntFlags & PDMDEVINSINT_FLAGS_R0_ENABLED) == pDevIns->fR0Enabled);
    if (   (pDevIns->Internal.s.fIntFlags & PDMDEVINSINT_FLAGS_R0_ENABLED)
        && !(pDevIns->Internal.s.pDevR3->pReg->fFlags & PDM_DEVREG_FLAGS_NEW_STYLE))
    {
        PDMDEVICECOMPATSETCRITSECTREQ Req;
        Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
        Req.Hdr.cbReq    = sizeof(Req);
        Req.idxR0Device  = pDevIns->Internal.s.idxR0Device;
        Req.pDevInsR3    = pDevIns;
        Req.pCritSectR3  = pCritSect;
        int rc = VMMR3CallR0(pVM, VMMR0_DO_PDM_DEVICE_COMPAT_SET_CRITSECT, 0, &Req.Hdr);
        AssertLogRelRCReturn(rc, rc);
    }

    PDMR3CritSectDelete(pVM, pOldCritSect);
    Assert((uintptr_t)pOldCritSect - (uintptr_t)pDevIns < pDevIns->cbRing3);

    LogFlow(("pdmR3DevHlp_SetDeviceCritSect: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectYield} */
static DECLCALLBACK(bool)     pdmR3DevHlp_CritSectYield(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMR3CritSectYield(pDevIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectEnter} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectEnter(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectEnter(pDevIns->Internal.s.pVMR3, pCritSect, rcBusy);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectEnterDebug} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectEnterDebug(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, int rcBusy, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectEnterDebug(pDevIns->Internal.s.pVMR3, pCritSect, rcBusy, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectTryEnter} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectTryEnter(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectTryEnter(pDevIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectTryEnterDebug} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectTryEnterDebug(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectTryEnterDebug(pDevIns->Internal.s.pVMR3, pCritSect, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectLeave} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectLeave(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectLeave(pDevIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectIsOwner} */
static DECLCALLBACK(bool)     pdmR3DevHlp_CritSectIsOwner(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectIsOwner(pDevIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectIsInitialized} */
static DECLCALLBACK(bool)     pdmR3DevHlp_CritSectIsInitialized(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    return PDMCritSectIsInitialized(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectHasWaiters} */
static DECLCALLBACK(bool)     pdmR3DevHlp_CritSectHasWaiters(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectHasWaiters(pDevIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectGetRecursion} */
static DECLCALLBACK(uint32_t) pdmR3DevHlp_CritSectGetRecursion(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    return PDMCritSectGetRecursion(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectScheduleExitEvent} */
static DECLCALLBACK(int) pdmR3DevHlp_CritSectScheduleExitEvent(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect,
                                                               SUPSEMEVENT hEventToSignal)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    return PDMHCCritSectScheduleExitEvent(pCritSect, hEventToSignal);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectDelete} */
static DECLCALLBACK(int) pdmR3DevHlp_CritSectDelete(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMR3CritSectDelete(pDevIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwInit} */
static DECLCALLBACK(int) pdmR3DevHlp_CritSectRwInit(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, RT_SRC_POS_DECL,
                                                    const char *pszNameFmt, va_list va)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_CritSectRwInit: caller='%s'/%d: pCritSect=%p pszNameFmt=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pCritSect, pszNameFmt, pszNameFmt));

    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    int rc = pdmR3CritSectRwInitDevice(pVM, pDevIns, pCritSect, RT_SRC_POS_ARGS, pszNameFmt, va);

    LogFlow(("pdmR3DevHlp_CritSectInit: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwDelete} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwDelete(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMR3CritSectRwDelete(pDevIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwEnterShared} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwEnterShared(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwEnterShared(pDevIns->Internal.s.pVMR3, pCritSect, rcBusy);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwEnterSharedDebug} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwEnterSharedDebug(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, int rcBusy,
                                                                     RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwEnterSharedDebug(pDevIns->Internal.s.pVMR3, pCritSect, rcBusy, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwTryEnterShared} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwTryEnterShared(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwTryEnterShared(pDevIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwTryEnterSharedDebug} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwTryEnterSharedDebug(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect,
                                                                        RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwTryEnterSharedDebug(pDevIns->Internal.s.pVMR3, pCritSect, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwLeaveShared} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwLeaveShared(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwLeaveShared(pDevIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwEnterExcl} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwEnterExcl(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwEnterExcl(pDevIns->Internal.s.pVMR3, pCritSect, rcBusy);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwEnterExclDebug} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwEnterExclDebug(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, int rcBusy,
                                                                   RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwEnterExclDebug(pDevIns->Internal.s.pVMR3, pCritSect, rcBusy, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwTryEnterExcl} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwTryEnterExcl(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwTryEnterExcl(pDevIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwTryEnterExclDebug} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwTryEnterExclDebug(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect,
                                                                      RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwTryEnterExclDebug(pDevIns->Internal.s.pVMR3, pCritSect, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwLeaveExcl} */
static DECLCALLBACK(int)      pdmR3DevHlp_CritSectRwLeaveExcl(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwLeaveExcl(pDevIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwIsWriteOwner} */
static DECLCALLBACK(bool)     pdmR3DevHlp_CritSectRwIsWriteOwner(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwIsWriteOwner(pDevIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwIsReadOwner} */
static DECLCALLBACK(bool)     pdmR3DevHlp_CritSectRwIsReadOwner(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, bool fWannaHear)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwIsReadOwner(pDevIns->Internal.s.pVMR3, pCritSect, fWannaHear);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwGetWriteRecursion} */
static DECLCALLBACK(uint32_t) pdmR3DevHlp_CritSectRwGetWriteRecursion(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    return PDMCritSectRwGetWriteRecursion(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwGetWriterReadRecursion} */
static DECLCALLBACK(uint32_t) pdmR3DevHlp_CritSectRwGetWriterReadRecursion(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    return PDMCritSectRwGetWriterReadRecursion(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwGetReadCount} */
static DECLCALLBACK(uint32_t) pdmR3DevHlp_CritSectRwGetReadCount(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    return PDMCritSectRwGetReadCount(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCritSectRwIsInitialized} */
static DECLCALLBACK(bool)     pdmR3DevHlp_CritSectRwIsInitialized(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    return PDMCritSectRwIsInitialized(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnThreadCreate} */
static DECLCALLBACK(int) pdmR3DevHlp_ThreadCreate(PPDMDEVINS pDevIns, PPPDMTHREAD ppThread, void *pvUser, PFNPDMTHREADDEV pfnThread,
                                                  PFNPDMTHREADWAKEUPDEV pfnWakeup, size_t cbStack, RTTHREADTYPE enmType, const char *pszName)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DevHlp_ThreadCreate: caller='%s'/%d: ppThread=%p pvUser=%p pfnThread=%p pfnWakeup=%p cbStack=%#zx enmType=%d pszName=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, ppThread, pvUser, pfnThread, pfnWakeup, cbStack, enmType, pszName, pszName));

    int rc = pdmR3ThreadCreateDevice(pDevIns->Internal.s.pVMR3, pDevIns, ppThread, pvUser, pfnThread, pfnWakeup, cbStack, enmType, pszName);

    LogFlow(("pdmR3DevHlp_ThreadCreate: caller='%s'/%d: returns %Rrc *ppThread=%RTthrd\n", pDevIns->pReg->szName, pDevIns->iInstance,
            rc, *ppThread));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSetAsyncNotification} */
static DECLCALLBACK(int) pdmR3DevHlp_SetAsyncNotification(PPDMDEVINS pDevIns, PFNPDMDEVASYNCNOTIFY pfnAsyncNotify)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT0(pDevIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DevHlp_SetAsyncNotification: caller='%s'/%d: pfnAsyncNotify=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pfnAsyncNotify));

    int rc = VINF_SUCCESS;
    AssertStmt(pfnAsyncNotify, rc = VERR_INVALID_PARAMETER);
    AssertStmt(!pDevIns->Internal.s.pfnAsyncNotify, rc = VERR_WRONG_ORDER);
    AssertStmt(pDevIns->Internal.s.fIntFlags & (PDMDEVINSINT_FLAGS_SUSPENDED | PDMDEVINSINT_FLAGS_RESET), rc = VERR_WRONG_ORDER);
    VMSTATE enmVMState = VMR3GetState(pDevIns->Internal.s.pVMR3);
    AssertStmt(   enmVMState == VMSTATE_SUSPENDING
               || enmVMState == VMSTATE_SUSPENDING_EXT_LS
               || enmVMState == VMSTATE_SUSPENDING_LS
               || enmVMState == VMSTATE_RESETTING
               || enmVMState == VMSTATE_RESETTING_LS
               || enmVMState == VMSTATE_POWERING_OFF
               || enmVMState == VMSTATE_POWERING_OFF_LS,
               rc = VERR_INVALID_STATE);

    if (RT_SUCCESS(rc))
        pDevIns->Internal.s.pfnAsyncNotify = pfnAsyncNotify;

    LogFlow(("pdmR3DevHlp_SetAsyncNotification: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnAsyncNotificationCompleted} */
static DECLCALLBACK(void) pdmR3DevHlp_AsyncNotificationCompleted(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;

    VMSTATE enmVMState = VMR3GetState(pVM);
    if (   enmVMState == VMSTATE_SUSPENDING
        || enmVMState == VMSTATE_SUSPENDING_EXT_LS
        || enmVMState == VMSTATE_SUSPENDING_LS
        || enmVMState == VMSTATE_RESETTING
        || enmVMState == VMSTATE_RESETTING_LS
        || enmVMState == VMSTATE_POWERING_OFF
        || enmVMState == VMSTATE_POWERING_OFF_LS)
    {
        LogFlow(("pdmR3DevHlp_AsyncNotificationCompleted: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));
        VMR3AsyncPdmNotificationWakeupU(pVM->pUVM);
    }
    else
        LogFlow(("pdmR3DevHlp_AsyncNotificationCompleted: caller='%s'/%d: enmVMState=%d\n", pDevIns->pReg->szName, pDevIns->iInstance, enmVMState));
}


/** @interface_method_impl{PDMDEVHLPR3,pfnRTCRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_RTCRegister(PPDMDEVINS pDevIns, PCPDMRTCREG pRtcReg, PCPDMRTCHLP *ppRtcHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
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

    /*
     * Only one DMA device.
     */
    PVM const pVM = pDevIns->Internal.s.pVMR3;
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    int rc = VINF_SUCCESS;
    if (!pVM->pdm.s.pRtc)
    {
        /*
         * Allocate and initialize pci bus structure.
         */
        PPDMRTC pRtc = (PPDMRTC)MMR3HeapAlloc(pDevIns->Internal.s.pVMR3, MM_TAG_PDM_DEVICE, sizeof(*pRtc));
        if (pRtc)
        {
            pRtc->pDevIns   = pDevIns;
            pRtc->Reg       = *pRtcReg;
            pVM->pdm.s.pRtc = pRtc;

            /* set the helper pointer. */
            *ppRtcHlp = &g_pdmR3DevRtcHlp;
            Log(("PDM: Registered RTC device '%s'/%d pDevIns=%p\n",
                 pDevIns->pReg->szName, pDevIns->iInstance, pDevIns));
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
    {
        AssertMsgFailed(("Only one RTC device is supported!\n"));
        rc = VERR_ALREADY_EXISTS;
    }

    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
    LogFlow(("pdmR3DevHlp_RTCRegister: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDMARegister} */
static DECLCALLBACK(int) pdmR3DevHlp_DMARegister(PPDMDEVINS pDevIns, unsigned uChannel, PFNDMATRANSFERHANDLER pfnTransferHandler, void *pvUser)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DevHlp_DMARegister: caller='%s'/%d: uChannel=%d pfnTransferHandler=%p pvUser=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, uChannel, pfnTransferHandler, pvUser));
    int            rc    = VINF_SUCCESS;
    PPDMDMAC const pDmac = pVM->pdm.s.pDmac;
    if (pDmac)
        pDmac->Reg.pfnRegister(pDmac->pDevIns, uChannel, pDevIns, pfnTransferHandler, pvUser);
    else
    {
        AssertMsgFailed(("Configuration error: No DMAC controller available. This could be related to init order too!\n"));
        rc = VERR_PDM_NO_DMAC_INSTANCE;
    }
    LogFlow(("pdmR3DevHlp_DMARegister: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDMAReadMemory} */
static DECLCALLBACK(int) pdmR3DevHlp_DMAReadMemory(PPDMDEVINS pDevIns, unsigned uChannel, void *pvBuffer, uint32_t off, uint32_t cbBlock, uint32_t *pcbRead)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DevHlp_DMAReadMemory: caller='%s'/%d: uChannel=%d pvBuffer=%p off=%#x cbBlock=%#x pcbRead=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, uChannel, pvBuffer, off, cbBlock, pcbRead));
    int            rc    = VINF_SUCCESS;
    PPDMDMAC const pDmac = pVM->pdm.s.pDmac;
    if (pDmac)
    {
        uint32_t cb = pDmac->Reg.pfnReadMemory(pDmac->pDevIns, uChannel, pvBuffer, off, cbBlock);
        if (pcbRead)
            *pcbRead = cb;
    }
    else
    {
        AssertMsgFailed(("Configuration error: No DMAC controller available. This could be related to init order too!\n"));
        rc = VERR_PDM_NO_DMAC_INSTANCE;
    }
    LogFlow(("pdmR3DevHlp_DMAReadMemory: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDMAWriteMemory} */
static DECLCALLBACK(int) pdmR3DevHlp_DMAWriteMemory(PPDMDEVINS pDevIns, unsigned uChannel, const void *pvBuffer, uint32_t off, uint32_t cbBlock, uint32_t *pcbWritten)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DevHlp_DMAWriteMemory: caller='%s'/%d: uChannel=%d pvBuffer=%p off=%#x cbBlock=%#x pcbWritten=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, uChannel, pvBuffer, off, cbBlock, pcbWritten));
    int            rc    = VINF_SUCCESS;
    PPDMDMAC const pDmac = pVM->pdm.s.pDmac;
    if (pDmac)
    {
        uint32_t cb = pDmac->Reg.pfnWriteMemory(pDmac->pDevIns, uChannel, pvBuffer, off, cbBlock);
        if (pcbWritten)
            *pcbWritten = cb;
    }
    else
    {
        AssertMsgFailed(("Configuration error: No DMAC controller available. This could be related to init order too!\n"));
        rc = VERR_PDM_NO_DMAC_INSTANCE;
    }
    LogFlow(("pdmR3DevHlp_DMAWriteMemory: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDMASetDREQ} */
static DECLCALLBACK(int) pdmR3DevHlp_DMASetDREQ(PPDMDEVINS pDevIns, unsigned uChannel, unsigned uLevel)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DevHlp_DMASetDREQ: caller='%s'/%d: uChannel=%d uLevel=%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance, uChannel, uLevel));
    int            rc    = VINF_SUCCESS;
    PPDMDMAC const pDmac = pVM->pdm.s.pDmac;
    if (pDmac)
        pDmac->Reg.pfnSetDREQ(pDmac->pDevIns, uChannel, uLevel);
    else
    {
        AssertMsgFailed(("Configuration error: No DMAC controller available. This could be related to init order too!\n"));
        rc = VERR_PDM_NO_DMAC_INSTANCE;
    }
    LogFlow(("pdmR3DevHlp_DMASetDREQ: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}

/** @interface_method_impl{PDMDEVHLPR3,pfnDMAGetChannelMode} */
static DECLCALLBACK(uint8_t) pdmR3DevHlp_DMAGetChannelMode(PPDMDEVINS pDevIns, unsigned uChannel)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DevHlp_DMAGetChannelMode: caller='%s'/%d: uChannel=%d\n",
             pDevIns->pReg->szName, pDevIns->iInstance, uChannel));
    uint8_t        u8Mode;
    PPDMDMAC const pDmac = pVM->pdm.s.pDmac;
    if (pDmac)
        u8Mode = pDmac->Reg.pfnGetChannelMode(pDmac->pDevIns, uChannel);
    else
    {
        AssertMsgFailed(("Configuration error: No DMAC controller available. This could be related to init order too!\n"));
        u8Mode = 3 << 2 /* illegal mode type */;
    }
    LogFlow(("pdmR3DevHlp_DMAGetChannelMode: caller='%s'/%d: returns %#04x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, u8Mode));
    return u8Mode;
}

/** @interface_method_impl{PDMDEVHLPR3,pfnDMASchedule} */
static DECLCALLBACK(void) pdmR3DevHlp_DMASchedule(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DevHlp_DMASchedule: caller='%s'/%d: VM_FF_PDM_DMA %d -> 1\n",
             pDevIns->pReg->szName, pDevIns->iInstance, VM_FF_IS_SET(pVM, VM_FF_PDM_DMA)));

    AssertMsg(pVM->pdm.s.pDmac, ("Configuration error: No DMAC controller available. This could be related to init order too!\n"));
    VM_FF_SET(pVM, VM_FF_PDM_DMA);
    VMR3NotifyGlobalFFU(pVM->pUVM, VMNOTIFYFF_FLAGS_DONE_REM);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCMOSWrite} */
static DECLCALLBACK(int) pdmR3DevHlp_CMOSWrite(PPDMDEVINS pDevIns, unsigned iReg, uint8_t u8Value)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

    LogFlow(("pdmR3DevHlp_CMOSWrite: caller='%s'/%d: iReg=%#04x u8Value=%#04x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, iReg, u8Value));
    int rc;
    PPDMRTC const pRtc = pVM->pdm.s.pRtc;
    if (pRtc)
    {
        PPDMDEVINS const pDevInsRtc = pRtc->pDevIns;
        rc = PDMCritSectEnter(pVM, pDevInsRtc->pCritSectRoR3, VERR_IGNORED);
        if (RT_SUCCESS(rc))
        {
            rc = pRtc->Reg.pfnWrite(pDevInsRtc, iReg, u8Value);
            PDMCritSectLeave(pVM, pDevInsRtc->pCritSectRoR3);
        }
    }
    else
        rc = VERR_PDM_NO_RTC_INSTANCE;

    LogFlow(("pdmR3DevHlp_CMOSWrite: caller='%s'/%d: return %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCMOSRead} */
static DECLCALLBACK(int) pdmR3DevHlp_CMOSRead(PPDMDEVINS pDevIns, unsigned iReg, uint8_t *pu8Value)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

    LogFlow(("pdmR3DevHlp_CMOSWrite: caller='%s'/%d: iReg=%#04x pu8Value=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, iReg, pu8Value));
    int rc;
    PPDMRTC const pRtc = pVM->pdm.s.pRtc;
    if (pRtc)
    {
        PPDMDEVINS const pDevInsRtc = pRtc->pDevIns;
        rc = PDMCritSectEnter(pVM, pDevInsRtc->pCritSectRoR3, VERR_IGNORED);
        if (RT_SUCCESS(rc))
        {
            rc = pRtc->Reg.pfnRead(pDevInsRtc, iReg, pu8Value);
            PDMCritSectLeave(pVM, pDevInsRtc->pCritSectRoR3);
        }
    }
    else
        rc = VERR_PDM_NO_RTC_INSTANCE;

    LogFlow(("pdmR3DevHlp_CMOSWrite: caller='%s'/%d: return %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnAssertEMT} */
static DECLCALLBACK(bool) pdmR3DevHlp_AssertEMT(PPDMDEVINS pDevIns, const char *pszFile, unsigned iLine, const char *pszFunction)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    if (VM_IS_EMT(pDevIns->Internal.s.pVMR3))
        return true;

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
    if (!VM_IS_EMT(pDevIns->Internal.s.pVMR3))
        return true;

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
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DevHlp_PDMLdrGetRCInterfaceSymbols: caller='%s'/%d: pvInterface=%p cbInterface=%zu pszSymPrefix=%p:{%s} pszSymList=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pvInterface, cbInterface, pszSymPrefix, pszSymPrefix, pszSymList, pszSymList));

    int rc;
    if (   strncmp(pszSymPrefix, "dev", 3) == 0
        && RTStrIStr(pszSymPrefix + 3, pDevIns->pReg->szName) != NULL)
    {
        if (pDevIns->pReg->fFlags & PDM_DEVREG_FLAGS_RC)
            rc = PDMR3LdrGetInterfaceSymbols(pDevIns->Internal.s.pVMR3,
                                             pvInterface, cbInterface,
                                             pDevIns->pReg->pszRCMod, pDevIns->Internal.s.pDevR3->pszRCSearchPath,
                                             pszSymPrefix, pszSymList,
                                             false /*fRing0OrRC*/);
        else
        {
            AssertMsgFailed(("Not a raw-mode enabled driver\n"));
            rc = VERR_PERMISSION_DENIED;
        }
    }
    else
    {
        AssertMsgFailed(("Invalid prefix '%s' for '%s'; must start with 'dev' and contain the driver name!\n",
                         pszSymPrefix, pDevIns->pReg->szName));
        rc = VERR_INVALID_NAME;
    }

    LogFlow(("pdmR3DevHlp_PDMLdrGetRCInterfaceSymbols: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName,
             pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnLdrGetR0InterfaceSymbols} */
static DECLCALLBACK(int) pdmR3DevHlp_LdrGetR0InterfaceSymbols(PPDMDEVINS pDevIns, void *pvInterface, size_t cbInterface,
                                                              const char *pszSymPrefix, const char *pszSymList)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DevHlp_PDMLdrGetR0InterfaceSymbols: caller='%s'/%d: pvInterface=%p cbInterface=%zu pszSymPrefix=%p:{%s} pszSymList=%p:{%s}\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pvInterface, cbInterface, pszSymPrefix, pszSymPrefix, pszSymList, pszSymList));

    int rc;
    if (   strncmp(pszSymPrefix, "dev", 3) == 0
        && RTStrIStr(pszSymPrefix + 3, pDevIns->pReg->szName) != NULL)
    {
        if (pDevIns->pReg->fFlags & PDM_DEVREG_FLAGS_R0)
            rc = PDMR3LdrGetInterfaceSymbols(pDevIns->Internal.s.pVMR3,
                                             pvInterface, cbInterface,
                                             pDevIns->pReg->pszR0Mod, pDevIns->Internal.s.pDevR3->pszR0SearchPath,
                                             pszSymPrefix, pszSymList,
                                             true /*fRing0OrRC*/);
        else
        {
            AssertMsgFailed(("Not a ring-0 enabled driver\n"));
            rc = VERR_PERMISSION_DENIED;
        }
    }
    else
    {
        AssertMsgFailed(("Invalid prefix '%s' for '%s'; must start with 'dev' and contain the driver name!\n",
                         pszSymPrefix, pDevIns->pReg->szName));
        rc = VERR_INVALID_NAME;
    }

    LogFlow(("pdmR3DevHlp_PDMLdrGetR0InterfaceSymbols: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName,
             pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnCallR0} */
static DECLCALLBACK(int) pdmR3DevHlp_CallR0(PPDMDEVINS pDevIns, uint32_t uOperation, uint64_t u64Arg)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM    pVM = pDevIns->Internal.s.pVMR3;
    PVMCPU pVCpu = VMMGetCpu(pVM);
    AssertReturn(pVCpu, VERR_VM_THREAD_IS_EMT);
    LogFlow(("pdmR3DevHlp_CallR0: caller='%s'/%d: uOperation=%#x u64Arg=%#RX64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, uOperation, u64Arg));

    /*
     * Resolve the ring-0 entry point.  There is not need to remember this like
     * we do for drivers since this is mainly for construction time hacks and
     * other things that aren't performance critical.
     */
    int rc;
    if (pDevIns->pReg->fFlags & PDM_DEVREG_FLAGS_R0)
    {
        /*
         * Make the ring-0 call.
         */
        PDMDEVICEGENCALLREQ Req;
        RT_ZERO(Req.Params);
        Req.Hdr.u32Magic    = SUPVMMR0REQHDR_MAGIC;
        Req.Hdr.cbReq       = sizeof(Req);
        Req.pDevInsR3       = pDevIns;
        Req.idxR0Device     = pDevIns->Internal.s.idxR0Device;
        Req.enmCall         = PDMDEVICEGENCALL_REQUEST;
        Req.Params.Req.uReq = uOperation;
        Req.Params.Req.uArg = u64Arg;
        rc = VMMR3CallR0Emt(pVM, pVCpu, VMMR0_DO_PDM_DEVICE_GEN_CALL, 0, &Req.Hdr);
    }
    else
        rc = VERR_ACCESS_DENIED;
    LogFlow(("pdmR3DevHlp_CallR0: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName,
             pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMGetSuspendReason} */
static DECLCALLBACK(VMSUSPENDREASON) pdmR3DevHlp_VMGetSuspendReason(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    VMSUSPENDREASON enmReason = VMR3GetSuspendReason(pVM->pUVM);
    LogFlow(("pdmR3DevHlp_VMGetSuspendReason: caller='%s'/%d: returns %d\n",
             pDevIns->pReg->szName, pDevIns->iInstance, enmReason));
    return enmReason;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMGetResumeReason} */
static DECLCALLBACK(VMRESUMEREASON) pdmR3DevHlp_VMGetResumeReason(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    VMRESUMEREASON enmReason = VMR3GetResumeReason(pVM->pUVM);
    LogFlow(("pdmR3DevHlp_VMGetResumeReason: caller='%s'/%d: returns %d\n",
             pDevIns->pReg->szName, pDevIns->iInstance, enmReason));
    return enmReason;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetUVM} */
static DECLCALLBACK(PUVM) pdmR3DevHlp_GetUVM(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_GetUVM: caller='%s'/%d: returns %p\n", pDevIns->pReg->szName, pDevIns->iInstance, pDevIns->Internal.s.pVMR3));
    return pDevIns->Internal.s.pVMR3->pUVM;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetVM} */
static DECLCALLBACK(PVM) pdmR3DevHlp_GetVM(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_GetVM: caller='%s'/%d: returns %p\n", pDevIns->pReg->szName, pDevIns->iInstance, pDevIns->Internal.s.pVMR3));
    return pDevIns->Internal.s.pVMR3;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetVMCPU} */
static DECLCALLBACK(PVMCPU) pdmR3DevHlp_GetVMCPU(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DevHlp_GetVMCPU: caller='%s'/%d for CPU %u\n", pDevIns->pReg->szName, pDevIns->iInstance, VMMGetCpuId(pDevIns->Internal.s.pVMR3)));
    return VMMGetCpu(pDevIns->Internal.s.pVMR3);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetCurrentCpuId} */
static DECLCALLBACK(VMCPUID) pdmR3DevHlp_GetCurrentCpuId(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VMCPUID idCpu = VMMGetCpuId(pDevIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DevHlp_GetCurrentCpuId: caller='%s'/%d for CPU %u\n", pDevIns->pReg->szName, pDevIns->iInstance, idCpu));
    return idCpu;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIBusRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_PCIBusRegister(PPDMDEVINS pDevIns, PPDMPCIBUSREGR3 pPciBusReg,
                                                    PCPDMPCIHLPR3 *ppPciHlp, uint32_t *piBus)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
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
    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_CREATING, VERR_WRONG_ORDER);

    /* Take the core lock exclusively just to be safe. */
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    /*
     * Find free PCI bus entry.
     */
    unsigned iBus = 0;
    for (iBus = 0; iBus < RT_ELEMENTS(pVM->pdm.s.aPciBuses); iBus++)
        if (!pVM->pdm.s.aPciBuses[iBus].pDevInsR3)
            break;
    AssertLogRelMsgReturnStmt(iBus < RT_ELEMENTS(pVM->pdm.s.aPciBuses),
                              ("Too many PCI buses. Max=%u\n", RT_ELEMENTS(pVM->pdm.s.aPciBuses)),
                              RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw),
                              VERR_OUT_OF_RESOURCES);
    PPDMPCIBUS pPciBus = &pVM->pdm.s.aPciBuses[iBus];

    /*
     * Init the R3 bits.
     */
    pPciBus->iBus                       = iBus;
    pPciBus->pDevInsR3                  = pDevIns;
    pPciBus->pfnRegister                = pPciBusReg->pfnRegisterR3;
    pPciBus->pfnRegisterMsi             = pPciBusReg->pfnRegisterMsiR3;
    pPciBus->pfnIORegionRegister        = pPciBusReg->pfnIORegionRegisterR3;
    pPciBus->pfnInterceptConfigAccesses = pPciBusReg->pfnInterceptConfigAccesses;
    pPciBus->pfnConfigRead              = pPciBusReg->pfnConfigRead;
    pPciBus->pfnConfigWrite             = pPciBusReg->pfnConfigWrite;
    pPciBus->pfnSetIrqR3                = pPciBusReg->pfnSetIrqR3;

    Log(("PDM: Registered PCI bus device '%s'/%d pDevIns=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pDevIns));

    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

    /* set the helper pointer and return. */
    *ppPciHlp = &g_pdmR3DevPciHlp;
    if (piBus)
        *piBus = iBus;
    LogFlow(("pdmR3DevHlp_PCIBusRegister: caller='%s'/%d: returns %Rrc *piBus=%u\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS, iBus));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnIommuRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_IommuRegister(PPDMDEVINS pDevIns, PPDMIOMMUREGR3 pIommuReg, PCPDMIOMMUHLPR3 *ppIommuHlp,
                                                   uint32_t *pidxIommu)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DevHlp_IommuRegister: caller='%s'/%d: pIommuReg=%p:{.u32Version=%#x, .u32TheEnd=%#x } ppIommuHlp=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pIommuReg, pIommuReg->u32Version, pIommuReg->u32TheEnd, ppIommuHlp));
    PVM pVM = pDevIns->Internal.s.pVMR3;

    /*
     * Validate input.
     */
    AssertMsgReturn(pIommuReg->u32Version == PDM_IOMMUREGR3_VERSION,
                    ("%s/%d: u32Version=%#x expected %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, pIommuReg->u32Version, PDM_IOMMUREGR3_VERSION),
                    VERR_INVALID_PARAMETER);
    AssertPtrReturn(pIommuReg->pfnMemAccess,     VERR_INVALID_POINTER);
    AssertPtrReturn(pIommuReg->pfnMemBulkAccess, VERR_INVALID_POINTER);
    AssertPtrReturn(pIommuReg->pfnMsiRemap,      VERR_INVALID_POINTER);
    AssertMsgReturn(pIommuReg->u32TheEnd == PDM_IOMMUREGR3_VERSION,
                    ("%s/%d: u32TheEnd=%#x expected %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, pIommuReg->u32TheEnd, PDM_IOMMUREGR3_VERSION),
                    VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppIommuHlp, VERR_INVALID_POINTER);

    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_CREATING, VERR_WRONG_ORDER);
    VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);

    /* Take the core lock exclusively just to be safe. */
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    /*
     * Find free IOMMU slot.
     * The IOMMU at the root complex is the one at 0.
     */
    unsigned idxIommu = 0;
#if 0
    for (idxIommu = 0; idxIommu < RT_ELEMENTS(pVM->pdm.s.aIommus); idxIommu++)
        if (!pVM->pdm.s.aIommus[idxIommu].pDevInsR3)
            break;
    AssertLogRelMsgReturnStmt(idxIommu < RT_ELEMENTS(pVM->pdm.s.aIommus),
                              ("Too many IOMMUs. Max=%u\n", RT_ELEMENTS(pVM->pdm.s.aIommus)),
                              RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw),
                              VERR_OUT_OF_RESOURCES);
#else
    /* Currently we support only a single IOMMU. */
    AssertMsgReturnStmt(!pVM->pdm.s.aIommus[idxIommu].pDevInsR3,
                        ("%s/%u: Only one IOMMU device is supported!\n", pDevIns->pReg->szName, pDevIns->iInstance),
                        RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw),
                        VERR_ALREADY_EXISTS);
#endif
    PPDMIOMMUR3 pIommu = &pVM->pdm.s.aIommus[idxIommu];

    /*
     * Init the R3 bits.
     */
    pIommu->idxIommu         = idxIommu;
    pIommu->pDevInsR3        = pDevIns;
    pIommu->pfnMemAccess     = pIommuReg->pfnMemAccess;
    pIommu->pfnMemBulkAccess = pIommuReg->pfnMemBulkAccess;
    pIommu->pfnMsiRemap      = pIommuReg->pfnMsiRemap;
    Log(("PDM: Registered IOMMU device '%s'/%d pDevIns=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pDevIns));

    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

    /* Set the helper pointer and return. */
    *ppIommuHlp = &g_pdmR3DevIommuHlp;
    if (pidxIommu)
        *pidxIommu = idxIommu;
    LogFlow(("pdmR3DevHlp_IommuRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}



/** @interface_method_impl{PDMDEVHLPR3,pfnPICRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_PICRegister(PPDMDEVINS pDevIns, PPDMPICREG pPicReg, PCPDMPICHLP *ppPicHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DevHlp_PICRegister: caller='%s'/%d: pPicReg=%p:{.u32Version=%#x, .pfnSetIrq=%p, .pfnGetInterrupt=%p, .u32TheEnd=%#x } ppPicHlp=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPicReg, pPicReg->u32Version, pPicReg->pfnSetIrq, pPicReg->pfnGetInterrupt, pPicReg->u32TheEnd, ppPicHlp));
    PVM pVM = pDevIns->Internal.s.pVMR3;

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

    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_CREATING, VERR_WRONG_ORDER);
    VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);

    /* Take the core lock exclusively just to be safe. */
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    /*
     * Only one PIC device.
     */
    AssertMsgReturnStmt(pVM->pdm.s.Pic.pDevInsR3 == NULL,
                        ("%s/%d: Only one PIC!\n", pDevIns->pReg->szName, pDevIns->iInstance),
                        RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw),
                        VERR_ALREADY_EXISTS);

    /*
     * Take down the callbacks and instance.
     */
    pVM->pdm.s.Pic.pDevInsR3 = pDevIns;
    pVM->pdm.s.Pic.pfnSetIrqR3 = pPicReg->pfnSetIrq;
    pVM->pdm.s.Pic.pfnGetInterruptR3 = pPicReg->pfnGetInterrupt;
    Log(("PDM: Registered PIC device '%s'/%d pDevIns=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pDevIns));

    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

    /* set the helper pointer and return. */
    *ppPicHlp = &g_pdmR3DevPicHlp;
    LogFlow(("pdmR3DevHlp_PICRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnApicRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_ApicRegister(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    /*
     * Validate caller context.
     */
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_CREATING, VERR_WRONG_ORDER);
    VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);

    /* Take the core lock exclusively just to be safe. */
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    /*
     * Only one APIC device. On SMP we have single logical device covering all LAPICs,
     * as they need to communicate and share state easily.
     */
    AssertMsgReturnStmt(pVM->pdm.s.Apic.pDevInsR3 == NULL,
                        ("%s/%u: Only one APIC device is supported!\n", pDevIns->pReg->szName, pDevIns->iInstance),
                        RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw),
                        VERR_ALREADY_EXISTS);

    /*
     * Set the ring-3 and raw-mode bits, leave the ring-0 to ring-0 setup.
     */
    pVM->pdm.s.Apic.pDevInsR3 = pDevIns;
#ifdef VBOX_WITH_RAW_MODE_KEEP
    pVM->pdm.s.Apic.pDevInsRC = PDMDEVINS_2_RCPTR(pDevIns);
    Assert(pVM->pdm.s.Apic.pDevInsRC || !VM_IS_RAW_MODE_ENABLED(pVM));
#endif

    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

    LogFlow(("pdmR3DevHlp_ApicRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnIoApicRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_IoApicRegister(PPDMDEVINS pDevIns, PPDMIOAPICREG pIoApicReg, PCPDMIOAPICHLP *ppIoApicHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_IoApicRegister: caller='%s'/%d: pIoApicReg=%p:{.u32Version=%#x, .pfnSetIrq=%p, .pfnSendMsi=%p, .pfnSetEoi=%p, .u32TheEnd=%#x } ppIoApicHlp=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pIoApicReg, pIoApicReg->u32Version, pIoApicReg->pfnSetIrq, pIoApicReg->pfnSendMsi, pIoApicReg->pfnSetEoi, pIoApicReg->u32TheEnd, ppIoApicHlp));
    PVM pVM = pDevIns->Internal.s.pVMR3;

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
    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_CREATING, VERR_WRONG_ORDER);
    VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);

    /* Take the core lock exclusively just to be safe. */
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    /*
     * The I/O APIC requires the APIC to be present (hacks++).
     * If the I/O APIC does GC stuff so must the APIC.
     */
    AssertMsgReturnStmt(pVM->pdm.s.Apic.pDevInsR3 != NULL,
                        ("Configuration error / Init order error! No APIC!\n"),
                        RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw),
                        VERR_WRONG_ORDER);

    /*
     * Only one I/O APIC device.
     */
    AssertMsgReturnStmt(pVM->pdm.s.IoApic.pDevInsR3 == NULL,
                        ("Only one IOAPIC device is supported! (caller %s/%d)\n", pDevIns->pReg->szName, pDevIns->iInstance),
                        RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw),
                        VERR_ALREADY_EXISTS);

    /*
     * Initialize the R3 bits.
     */
    pVM->pdm.s.IoApic.pDevInsR3    = pDevIns;
    pVM->pdm.s.IoApic.pfnSetIrqR3  = pIoApicReg->pfnSetIrq;
    pVM->pdm.s.IoApic.pfnSendMsiR3 = pIoApicReg->pfnSendMsi;
    pVM->pdm.s.IoApic.pfnSetEoiR3  = pIoApicReg->pfnSetEoi;
    Log(("PDM: Registered I/O APIC device '%s'/%d pDevIns=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pDevIns));

    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

    /* set the helper pointer and return. */
    *ppIoApicHlp = &g_pdmR3DevIoApicHlp;
    LogFlow(("pdmR3DevHlp_IoApicRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnHpetRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_HpetRegister(PPDMDEVINS pDevIns, PPDMHPETREG pHpetReg, PCPDMHPETHLPR3 *ppHpetHlpR3)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_HpetRegister: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));
    PVM pVM = pDevIns->Internal.s.pVMR3;

    /*
     * Validate input.
     */
    AssertMsgReturn(pHpetReg->u32Version == PDM_HPETREG_VERSION,
                    ("%s/%u: u32Version=%#x expected %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, pHpetReg->u32Version, PDM_HPETREG_VERSION),
                    VERR_VERSION_MISMATCH);
    AssertPtrReturn(ppHpetHlpR3, VERR_INVALID_POINTER);
    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_CREATING, VERR_WRONG_ORDER);
    VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);

    /*
     * Only one HPET device.
     */
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    AssertMsgReturnStmt(pVM->pdm.s.pHpet == NULL,
                        ("Only one HPET device is supported! (caller %s/%d)\n", pDevIns->pReg->szName, pDevIns->iInstance),
                        RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw),
                        VERR_ALREADY_EXISTS);

    /*
     * Do the job (what there is of it).
     */
    pVM->pdm.s.pHpet = pDevIns;

    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

    *ppHpetHlpR3 = &g_pdmR3DevHpetHlp;

    LogFlow(("pdmR3DevHlp_HpetRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPciRawRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_PciRawRegister(PPDMDEVINS pDevIns, PPDMPCIRAWREG pPciRawReg, PCPDMPCIRAWHLPR3 *ppPciRawHlpR3)
{
    PDMDEV_ASSERT_DEVINS(pDevIns); RT_NOREF_PV(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
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

    /* set the helper pointer and return. */
    *ppPciRawHlpR3 = &g_pdmR3DevPciRawHlp;
    LogFlow(("pdmR3DevHlp_PciRawRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnDMACRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_DMACRegister(PPDMDEVINS pDevIns, PPDMDMACREG pDmacReg, PCPDMDMACHLP *ppDmacHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
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

    /*
     * Only one DMA device.
     */
    PVM const pVM = pDevIns->Internal.s.pVMR3;
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    int rc = VINF_SUCCESS;
    if (!pVM->pdm.s.pDmac)
    {

        /*
         * Allocate and initialize pci bus structure.
         */
        PPDMDMAC pDmac = (PPDMDMAC)MMR3HeapAlloc(pDevIns->Internal.s.pVMR3, MM_TAG_PDM_DEVICE, sizeof(*pDmac));
        if (pDmac)
        {
            pDmac->pDevIns   = pDevIns;
            pDmac->Reg       = *pDmacReg;
            pVM->pdm.s.pDmac = pDmac;

            /* set the helper pointer. */
            *ppDmacHlp = &g_pdmR3DevDmacHlp;
            Log(("PDM: Registered DMAC device '%s'/%d pDevIns=%p\n",
                 pDevIns->pReg->szName, pDevIns->iInstance, pDevIns));
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
    {
        AssertMsgFailed(("Only one DMA device is supported!\n"));
        rc = VERR_ALREADY_EXISTS;
    }

    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
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
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DevHlp_RegisterVMMDevHeap: caller='%s'/%d: GCPhys=%RGp pvHeap=%p cbHeap=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPhys, pvHeap, cbHeap));

    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);
    if (pVM->pdm.s.pvVMMDevHeap == NULL)
    {
        pVM->pdm.s.pvVMMDevHeap     = pvHeap;
        pVM->pdm.s.GCPhysVMMDevHeap = GCPhys;
        pVM->pdm.s.cbVMMDevHeap     = cbHeap;
        pVM->pdm.s.cbVMMDevHeapLeft = cbHeap;
    }
    else
    {
        Assert(pVM->pdm.s.pvVMMDevHeap == pvHeap);
        Assert(pVM->pdm.s.cbVMMDevHeap == cbHeap);
        Assert(pVM->pdm.s.GCPhysVMMDevHeap != GCPhys || GCPhys == NIL_RTGCPHYS);
        if (pVM->pdm.s.GCPhysVMMDevHeap != GCPhys)
        {
            pVM->pdm.s.GCPhysVMMDevHeap = GCPhys;
            PFNPDMVMMDEVHEAPNOTIFY const pfnVMMDevHeapNotify = pVM->pdm.s.pfnVMMDevHeapNotify;
            if (pfnVMMDevHeapNotify)
                pfnVMMDevHeapNotify(pVM, pvHeap, GCPhys);
        }
    }
    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

    LogFlow(("pdmR3DevHlp_RegisterVMMDevHeap: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVHLPR3,pfnFirmwareRegister}
 */
static DECLCALLBACK(int) pdmR3DevHlp_FirmwareRegister(PPDMDEVINS pDevIns, PCPDMFWREG pFwReg, PCPDMFWHLPR3 *ppFwHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
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

    /*
     * Only one firmware registration.
     */
    PVM const pVM = pDevIns->Internal.s.pVMR3;
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    int rc = VINF_SUCCESS;
    if (!pVM->pdm.s.pFirmware)
    {
        /*
         * Allocate and initialize pci bus structure.
         */
        PPDMFW pFirmware = (PPDMFW)MMR3HeapAlloc(pDevIns->Internal.s.pVMR3, MM_TAG_PDM_DEVICE, sizeof(*pFirmware));
        if (pFirmware)
        {
            pFirmware->pDevIns   = pDevIns;
            pFirmware->Reg       = *pFwReg;
            pVM->pdm.s.pFirmware = pFirmware;

            /* set the helper pointer. */
            *ppFwHlp = &g_pdmR3DevFirmwareHlp;
            Log(("PDM: Registered firmware device '%s'/%d pDevIns=%p\n",
                 pDevIns->pReg->szName, pDevIns->iInstance, pDevIns));
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
    {
        AssertMsgFailed(("Only one firmware device is supported!\n"));
        rc = VERR_ALREADY_EXISTS;
    }

    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
    LogFlow(("pdmR3DevHlp_FirmwareRegister: caller='%s'/%d: returns %Rrc\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMReset} */
static DECLCALLBACK(int) pdmR3DevHlp_VMReset(PPDMDEVINS pDevIns, uint32_t fFlags)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DevHlp_VMReset: caller='%s'/%d: fFlags=%#x VM_FF_RESET %d -> 1\n",
             pDevIns->pReg->szName, pDevIns->iInstance, fFlags, VM_FF_IS_SET(pVM, VM_FF_RESET)));

    /*
     * We postpone this operation because we're likely to be inside a I/O instruction
     * and the EIP will be updated when we return.
     * We still return VINF_EM_RESET to break out of any execution loops and force FF evaluation.
     */
    bool fHaltOnReset;
    int rc = CFGMR3QueryBool(CFGMR3GetChild(CFGMR3GetRoot(pVM), "PDM"), "HaltOnReset", &fHaltOnReset);
    if (RT_SUCCESS(rc) && fHaltOnReset)
    {
        Log(("pdmR3DevHlp_VMReset: Halt On Reset!\n"));
        rc = VINF_EM_HALT;
    }
    else
    {
        pVM->pdm.s.fResetFlags = fFlags;
        VM_FF_SET(pVM, VM_FF_RESET);
        rc = VINF_EM_RESET;
    }

    LogFlow(("pdmR3DevHlp_VMReset: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMSuspend} */
static DECLCALLBACK(int) pdmR3DevHlp_VMSuspend(PPDMDEVINS pDevIns)
{
    int rc;
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DevHlp_VMSuspend: caller='%s'/%d:\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    /** @todo Always take the SMP path - fewer code paths. */
    if (pVM->cCpus > 1)
    {
        /* We own the IOM lock here and could cause a deadlock by waiting for a VCPU that is blocking on the IOM lock. */
        rc = VMR3ReqCallNoWait(pVM, VMCPUID_ANY_QUEUE, (PFNRT)VMR3Suspend, 2, pVM->pUVM, VMSUSPENDREASON_VM);
        AssertRC(rc);
        rc = VINF_EM_SUSPEND;
    }
    else
        rc = VMR3Suspend(pVM->pUVM, VMSUSPENDREASON_VM);

    LogFlow(("pdmR3DevHlp_VMSuspend: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/**
 * Worker for pdmR3DevHlp_VMSuspendSaveAndPowerOff that is invoked via a queued
 * EMT request to avoid deadlocks.
 *
 * @returns VBox status code fit for scheduling.
 * @param   pVM                 The cross context VM structure.
 * @param   pDevIns             The device that triggered this action.
 */
static DECLCALLBACK(int) pdmR3DevHlp_VMSuspendSaveAndPowerOffWorker(PVM pVM, PPDMDEVINS pDevIns)
{
    /*
     * Suspend the VM first then do the saving.
     */
    int rc = VMR3Suspend(pVM->pUVM, VMSUSPENDREASON_VM);
    if (RT_SUCCESS(rc))
    {
        PUVM pUVM = pVM->pUVM;
        rc = pUVM->pVmm2UserMethods->pfnSaveState(pVM->pUVM->pVmm2UserMethods, pUVM);

        /*
         * On success, power off the VM, on failure we'll leave it suspended.
         */
        if (RT_SUCCESS(rc))
        {
            rc = VMR3PowerOff(pVM->pUVM);
            if (RT_FAILURE(rc))
                LogRel(("%s/SSP: VMR3PowerOff failed: %Rrc\n", pDevIns->pReg->szName, rc));
        }
        else
            LogRel(("%s/SSP: pfnSaveState failed: %Rrc\n", pDevIns->pReg->szName, rc));
    }
    else
        LogRel(("%s/SSP: Suspend failed: %Rrc\n", pDevIns->pReg->szName, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMSuspendSaveAndPowerOff} */
static DECLCALLBACK(int) pdmR3DevHlp_VMSuspendSaveAndPowerOff(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DevHlp_VMSuspendSaveAndPowerOff: caller='%s'/%d:\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    int rc;
    if (   pVM->pUVM->pVmm2UserMethods
        && pVM->pUVM->pVmm2UserMethods->pfnSaveState)
    {
        rc = VMR3ReqCallNoWait(pVM, VMCPUID_ANY_QUEUE, (PFNRT)pdmR3DevHlp_VMSuspendSaveAndPowerOffWorker, 2, pVM, pDevIns);
        if (RT_SUCCESS(rc))
        {
            LogRel(("%s: Suspending, Saving and Powering Off the VM\n", pDevIns->pReg->szName));
            rc = VINF_EM_SUSPEND;
        }
    }
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlow(("pdmR3DevHlp_VMSuspendSaveAndPowerOff: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMPowerOff} */
static DECLCALLBACK(int) pdmR3DevHlp_VMPowerOff(PPDMDEVINS pDevIns)
{
    int rc;
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DevHlp_VMPowerOff: caller='%s'/%d:\n",
             pDevIns->pReg->szName, pDevIns->iInstance));

    /** @todo Always take the SMP path - fewer code paths. */
    if (pVM->cCpus > 1)
    {
        /* We might be holding locks here and could cause a deadlock since
           VMR3PowerOff rendezvous with the other CPUs. */
        rc = VMR3ReqCallNoWait(pVM, VMCPUID_ANY_QUEUE, (PFNRT)VMR3PowerOff, 1, pVM->pUVM);
        AssertRC(rc);
        /* Set the VCPU state to stopped here as well to make sure no
           inconsistency with the EM state occurs. */
        VMCPU_SET_STATE(VMMGetCpu(pVM), VMCPUSTATE_STOPPED);
        rc = VINF_EM_OFF;
    }
    else
        rc = VMR3PowerOff(pVM->pUVM);

    LogFlow(("pdmR3DevHlp_VMPowerOff: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnA20IsEnabled} */
static DECLCALLBACK(bool) pdmR3DevHlp_A20IsEnabled(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);

    bool fRc = PGMPhysIsA20Enabled(VMMGetCpu(pDevIns->Internal.s.pVMR3));

    LogFlow(("pdmR3DevHlp_A20IsEnabled: caller='%s'/%d: returns %d\n", pDevIns->pReg->szName, pDevIns->iInstance, fRc));
    return fRc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnA20Set} */
static DECLCALLBACK(void) pdmR3DevHlp_A20Set(PPDMDEVINS pDevIns, bool fEnable)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DevHlp_A20Set: caller='%s'/%d: fEnable=%d\n", pDevIns->pReg->szName, pDevIns->iInstance, fEnable));
#ifdef VBOX_VMM_TARGET_ARMV8
    AssertReleaseFailed();
    RT_NOREF(pDevIns, fEnable);
#else
    PGMR3PhysSetA20(VMMGetCpu(pDevIns->Internal.s.pVMR3), fEnable);
#endif
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetCpuId} */
static DECLCALLBACK(void) pdmR3DevHlp_GetCpuId(PPDMDEVINS pDevIns, uint32_t iLeaf,
                                               uint32_t *pEax, uint32_t *pEbx, uint32_t *pEcx, uint32_t *pEdx)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);

    LogFlow(("pdmR3DevHlp_GetCpuId: caller='%s'/%d: iLeaf=%d pEax=%p pEbx=%p pEcx=%p pEdx=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, iLeaf, pEax, pEbx, pEcx, pEdx));
    AssertPtr(pEax); AssertPtr(pEbx); AssertPtr(pEcx); AssertPtr(pEdx);

#ifdef VBOX_VMM_TARGET_ARMV8
    RT_NOREF(pDevIns, iLeaf, pEax, pEbx, pEcx, pEdx);
    AssertReleaseFailed();
#else
    CPUMGetGuestCpuId(VMMGetCpu(pDevIns->Internal.s.pVMR3), iLeaf, 0 /*iSubLeaf*/, -1 /*f64BitMode*/, pEax, pEbx, pEcx, pEdx);
#endif

    LogFlow(("pdmR3DevHlp_GetCpuId: caller='%s'/%d: returns void - *pEax=%#x *pEbx=%#x *pEcx=%#x *pEdx=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, *pEax, *pEbx, *pEcx, *pEdx));
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetMainExecutionEngine} */
static DECLCALLBACK(uint8_t) pdmR3DevHlp_GetMainExecutionEngine(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VM_ASSERT_EMT(pDevIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DevHlp_GetMainExecutionEngine: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return pDevIns->Internal.s.pVMR3->bMainExecutionEngine;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMMRegisterPatchMemory} */
static DECLCALLBACK(int) pdmR3DevHlp_VMMRegisterPatchMemory(PPDMDEVINS pDevIns, RTGCPTR GCPtrPatchMem, uint32_t cbPatchMem)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    LogFlow(("pdmR3DevHlp_VMMRegisterPatchMemory: caller='%s'/%d: GCPtrPatchMem=%RGv cbPatchMem=%RU32\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPtrPatchMem, cbPatchMem));

    int rc = VMMR3RegisterPatchMemory(pDevIns->Internal.s.pVMR3, GCPtrPatchMem, cbPatchMem);

    LogFlow(("pdmR3DevHlp_VMMRegisterPatchMemory: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMMDeregisterPatchMemory} */
static DECLCALLBACK(int) pdmR3DevHlp_VMMDeregisterPatchMemory(PPDMDEVINS pDevIns, RTGCPTR GCPtrPatchMem, uint32_t cbPatchMem)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    LogFlow(("pdmR3DevHlp_VMMDeregisterPatchMemory: caller='%s'/%d: GCPtrPatchMem=%RGv cbPatchMem=%RU32\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPtrPatchMem, cbPatchMem));

    int rc = VMMR3DeregisterPatchMemory(pDevIns->Internal.s.pVMR3, GCPtrPatchMem, cbPatchMem);

    LogFlow(("pdmR3DevHlp_VMMDeregisterPatchMemory: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSharedModuleRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_SharedModuleRegister(PPDMDEVINS pDevIns, VBOXOSFAMILY enmGuestOS, char *pszModuleName, char *pszVersion,
                                                          RTGCPTR GCBaseAddr, uint32_t cbModule,
                                                          uint32_t cRegions, VMMDEVSHAREDREGIONDESC const *paRegions)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    LogFlow(("pdmR3DevHlp_SharedModuleRegister: caller='%s'/%d: enmGuestOS=%u pszModuleName=%p:{%s} pszVersion=%p:{%s} GCBaseAddr=%RGv cbModule=%#x cRegions=%u paRegions=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, enmGuestOS, pszModuleName, pszModuleName, pszVersion, pszVersion, GCBaseAddr, cbModule, cRegions, paRegions));

#ifdef VBOX_WITH_PAGE_SHARING
    int rc = PGMR3SharedModuleRegister(pDevIns->Internal.s.pVMR3, enmGuestOS, pszModuleName, pszVersion,
                                       GCBaseAddr, cbModule, cRegions, paRegions);
#else
    RT_NOREF(pDevIns, enmGuestOS, pszModuleName, pszVersion, GCBaseAddr, cbModule, cRegions, paRegions);
    int rc = VERR_NOT_SUPPORTED;
#endif

    LogFlow(("pdmR3DevHlp_SharedModuleRegister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSharedModuleUnregister} */
static DECLCALLBACK(int) pdmR3DevHlp_SharedModuleUnregister(PPDMDEVINS pDevIns, char *pszModuleName, char *pszVersion,
                                                            RTGCPTR GCBaseAddr, uint32_t cbModule)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    LogFlow(("pdmR3DevHlp_SharedModuleUnregister: caller='%s'/%d: pszModuleName=%p:{%s} pszVersion=%p:{%s} GCBaseAddr=%RGv cbModule=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pszModuleName, pszModuleName, pszVersion, pszVersion, GCBaseAddr, cbModule));

#ifdef VBOX_WITH_PAGE_SHARING
    int rc = PGMR3SharedModuleUnregister(pDevIns->Internal.s.pVMR3, pszModuleName, pszVersion, GCBaseAddr, cbModule);
#else
    RT_NOREF(pDevIns, pszModuleName, pszVersion, GCBaseAddr, cbModule);
    int rc = VERR_NOT_SUPPORTED;
#endif

    LogFlow(("pdmR3DevHlp_SharedModuleUnregister: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSharedModuleGetPageState} */
static DECLCALLBACK(int) pdmR3DevHlp_SharedModuleGetPageState(PPDMDEVINS pDevIns, RTGCPTR GCPtrPage, bool *pfShared, uint64_t *pfPageFlags)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    LogFlow(("pdmR3DevHlp_SharedModuleGetPageState: caller='%s'/%d: GCPtrPage=%RGv pfShared=%p pfPageFlags=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, GCPtrPage, pfShared, pfPageFlags));

#if defined(VBOX_WITH_PAGE_SHARING) && defined(DEBUG)
    int rc = PGMR3SharedModuleGetPageState(pDevIns->Internal.s.pVMR3, GCPtrPage, pfShared, pfPageFlags);
#else
    RT_NOREF(pDevIns, GCPtrPage, pfShared, pfPageFlags);
    int rc = VERR_NOT_IMPLEMENTED;
#endif

    LogFlow(("pdmR3DevHlp_SharedModuleGetPageState: caller='%s'/%d: returns %Rrc *pfShared=%RTbool *pfPageFlags=%#RX64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc, *pfShared, *pfPageFlags));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSharedModuleCheckAll} */
static DECLCALLBACK(int) pdmR3DevHlp_SharedModuleCheckAll(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    LogFlow(("pdmR3DevHlp_SharedModuleCheckAll: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

#ifdef VBOX_WITH_PAGE_SHARING
    int rc = PGMR3SharedModuleCheckAll(pDevIns->Internal.s.pVMR3);
#else
    RT_NOREF(pDevIns);
    int rc = VERR_NOT_SUPPORTED;
#endif

    LogFlow(("pdmR3DevHlp_SharedModuleCheckAll: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnQueryLun} */
static DECLCALLBACK(int) pdmR3DevHlp_QueryLun(PPDMDEVINS pDevIns, const char *pszDevice,
                                              unsigned iInstance, unsigned iLun, PPDMIBASE *ppBase)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    LogFlow(("pdmR3DevHlp_QueryLun: caller='%s'/%d: pszDevice=%p:{%s} iInstance=%u iLun=%u ppBase=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pszDevice, pszDevice, iInstance, iLun, ppBase));

    int rc = PDMR3QueryLun(pDevIns->Internal.s.pVMR3->pUVM, pszDevice, iInstance, iLun, ppBase);

    LogFlow(("pdmR3DevHlp_QueryLun: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGIMDeviceRegister} */
static DECLCALLBACK(void) pdmR3DevHlp_GIMDeviceRegister(PPDMDEVINS pDevIns, PGIMDEBUG pDbg)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    LogFlow(("pdmR3DevHlp_GIMDeviceRegister: caller='%s'/%d: pDbg=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pDbg));

    GIMR3GimDeviceRegister(pDevIns->Internal.s.pVMR3, pDevIns, pDbg);

    LogFlow(("pdmR3DevHlp_GIMDeviceRegister: caller='%s'/%d: returns\n", pDevIns->pReg->szName, pDevIns->iInstance));
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGIMGetDebugSetup} */
static DECLCALLBACK(int) pdmR3DevHlp_GIMGetDebugSetup(PPDMDEVINS pDevIns, PGIMDEBUGSETUP pDbgSetup)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    LogFlow(("pdmR3DevHlp_GIMGetDebugSetup: caller='%s'/%d: pDbgSetup=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pDbgSetup));

    int rc = GIMR3GetDebugSetup(pDevIns->Internal.s.pVMR3, pDbgSetup);

    LogFlow(("pdmR3DevHlp_GIMGetDebugSetup: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGIMGetMmio2Regions} */
static DECLCALLBACK(PGIMMMIO2REGION) pdmR3DevHlp_GIMGetMmio2Regions(PPDMDEVINS pDevIns, uint32_t *pcRegions)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    LogFlow(("pdmR3DevHlp_GIMGetMmio2Regions: caller='%s'/%d: pcRegions=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pcRegions));

    PGIMMMIO2REGION pRegion = GIMGetMmio2Regions(pDevIns->Internal.s.pVMR3, pcRegions);

    LogFlow(("pdmR3DevHlp_GIMGetMmio2Regions: caller='%s'/%d: returns %p\n", pDevIns->pReg->szName, pDevIns->iInstance, pRegion));
    return pRegion;
}


/**
 * The device helper structure for trusted devices.
 */
const PDMDEVHLPR3 g_pdmR3DevHlpTrusted =
{
    PDM_DEVHLPR3_VERSION,
    pdmR3DevHlp_IoPortCreateEx,
    pdmR3DevHlp_IoPortMap,
    pdmR3DevHlp_IoPortUnmap,
    pdmR3DevHlp_IoPortGetMappingAddress,
    pdmR3DevHlp_IoPortRead,
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
    SSMR3PutStruct,
    SSMR3PutStructEx,
    SSMR3PutBool,
    SSMR3PutU8,
    SSMR3PutS8,
    SSMR3PutU16,
    SSMR3PutS16,
    SSMR3PutU32,
    SSMR3PutS32,
    SSMR3PutU64,
    SSMR3PutS64,
    SSMR3PutU128,
    SSMR3PutS128,
    SSMR3PutUInt,
    SSMR3PutSInt,
    SSMR3PutGCUInt,
    SSMR3PutGCUIntReg,
    SSMR3PutGCPhys32,
    SSMR3PutGCPhys64,
    SSMR3PutGCPhys,
    SSMR3PutGCPtr,
    SSMR3PutGCUIntPtr,
    SSMR3PutRCPtr,
    SSMR3PutIOPort,
    SSMR3PutSel,
    SSMR3PutMem,
    SSMR3PutStrZ,
    SSMR3GetStruct,
    SSMR3GetStructEx,
    SSMR3GetBool,
    SSMR3GetBoolV,
    SSMR3GetU8,
    SSMR3GetU8V,
    SSMR3GetS8,
    SSMR3GetS8V,
    SSMR3GetU16,
    SSMR3GetU16V,
    SSMR3GetS16,
    SSMR3GetS16V,
    SSMR3GetU32,
    SSMR3GetU32V,
    SSMR3GetS32,
    SSMR3GetS32V,
    SSMR3GetU64,
    SSMR3GetU64V,
    SSMR3GetS64,
    SSMR3GetS64V,
    SSMR3GetU128,
    SSMR3GetU128V,
    SSMR3GetS128,
    SSMR3GetS128V,
    SSMR3GetGCPhys32,
    SSMR3GetGCPhys32V,
    SSMR3GetGCPhys64,
    SSMR3GetGCPhys64V,
    SSMR3GetGCPhys,
    SSMR3GetGCPhysV,
    SSMR3GetUInt,
    SSMR3GetSInt,
    SSMR3GetGCUInt,
    SSMR3GetGCUIntReg,
    SSMR3GetGCPtr,
    SSMR3GetGCUIntPtr,
    SSMR3GetRCPtr,
    SSMR3GetIOPort,
    SSMR3GetSel,
    SSMR3GetMem,
    SSMR3GetStrZ,
    SSMR3GetStrZEx,
    SSMR3Skip,
    SSMR3SkipToEndOfUnit,
    SSMR3SetLoadError,
    SSMR3SetLoadErrorV,
    SSMR3SetCfgError,
    SSMR3SetCfgErrorV,
    SSMR3HandleGetStatus,
    SSMR3HandleGetAfter,
    SSMR3HandleIsLiveSave,
    SSMR3HandleMaxDowntime,
    SSMR3HandleHostBits,
    SSMR3HandleRevision,
    SSMR3HandleVersion,
    SSMR3HandleHostOSAndArch,
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
    TMR3TimerSkip,
    pdmR3DevHlp_TMUtcNow,
    CFGMR3Exists,
    CFGMR3QueryType,
    CFGMR3QuerySize,
    CFGMR3QueryInteger,
    CFGMR3QueryIntegerDef,
    CFGMR3QueryString,
    CFGMR3QueryStringDef,
    CFGMR3QueryPassword,
    CFGMR3QueryPasswordDef,
    CFGMR3QueryBytes,
    CFGMR3QueryU64,
    CFGMR3QueryU64Def,
    CFGMR3QueryS64,
    CFGMR3QueryS64Def,
    CFGMR3QueryU32,
    CFGMR3QueryU32Def,
    CFGMR3QueryS32,
    CFGMR3QueryS32Def,
    CFGMR3QueryU16,
    CFGMR3QueryU16Def,
    CFGMR3QueryS16,
    CFGMR3QueryS16Def,
    CFGMR3QueryU8,
    CFGMR3QueryU8Def,
    CFGMR3QueryS8,
    CFGMR3QueryS8Def,
    CFGMR3QueryBool,
    CFGMR3QueryBoolDef,
    CFGMR3QueryPort,
    CFGMR3QueryPortDef,
    CFGMR3QueryUInt,
    CFGMR3QueryUIntDef,
    CFGMR3QuerySInt,
    CFGMR3QuerySIntDef,
    CFGMR3QueryGCPtr,
    CFGMR3QueryGCPtrDef,
    CFGMR3QueryGCPtrU,
    CFGMR3QueryGCPtrUDef,
    CFGMR3QueryGCPtrS,
    CFGMR3QueryGCPtrSDef,
    CFGMR3QueryStringAlloc,
    CFGMR3QueryStringAllocDef,
    CFGMR3GetParent,
    CFGMR3GetChild,
    CFGMR3GetChildF,
    CFGMR3GetChildFV,
    CFGMR3GetFirstChild,
    CFGMR3GetNextChild,
    CFGMR3GetName,
    CFGMR3GetNameLen,
    CFGMR3AreChildrenValid,
    CFGMR3GetFirstValue,
    CFGMR3GetNextValue,
    CFGMR3GetValueName,
    CFGMR3GetValueNameLen,
    CFGMR3GetValueType,
    CFGMR3AreValuesValid,
    CFGMR3ValidateConfig,
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
    PDMR3ThreadDestroy,
    PDMR3ThreadIAmSuspending,
    PDMR3ThreadIAmRunning,
    PDMR3ThreadSleep,
    PDMR3ThreadSuspend,
    PDMR3ThreadResume,
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
    pdmR3DevHlp_CpuGetGuestArch,
    pdmR3DevHlp_CpuGetGuestMicroarch,
    pdmR3DevHlp_CpuGetGuestAddrWidths,
    pdmR3DevHlp_CpuGetGuestScalableBusFrequency,
    pdmR3DevHlp_STAMDeregisterByPrefix,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
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


#ifdef VBOX_WITH_DBGF_TRACING
/**
 * The device helper structure for trusted devices - tracing variant.
 */
const PDMDEVHLPR3 g_pdmR3DevHlpTracing =
{
    PDM_DEVHLPR3_VERSION,
    pdmR3DevHlpTracing_IoPortCreateEx,
    pdmR3DevHlpTracing_IoPortMap,
    pdmR3DevHlpTracing_IoPortUnmap,
    pdmR3DevHlp_IoPortGetMappingAddress,
    pdmR3DevHlp_IoPortRead,  /** @todo Needs tracing variants for ARM now. */
    pdmR3DevHlp_IoPortWrite, /** @todo Needs tracing variants for ARM now. */
    pdmR3DevHlpTracing_MmioCreateEx,
    pdmR3DevHlpTracing_MmioMap,
    pdmR3DevHlpTracing_MmioUnmap,
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
    SSMR3PutStruct,
    SSMR3PutStructEx,
    SSMR3PutBool,
    SSMR3PutU8,
    SSMR3PutS8,
    SSMR3PutU16,
    SSMR3PutS16,
    SSMR3PutU32,
    SSMR3PutS32,
    SSMR3PutU64,
    SSMR3PutS64,
    SSMR3PutU128,
    SSMR3PutS128,
    SSMR3PutUInt,
    SSMR3PutSInt,
    SSMR3PutGCUInt,
    SSMR3PutGCUIntReg,
    SSMR3PutGCPhys32,
    SSMR3PutGCPhys64,
    SSMR3PutGCPhys,
    SSMR3PutGCPtr,
    SSMR3PutGCUIntPtr,
    SSMR3PutRCPtr,
    SSMR3PutIOPort,
    SSMR3PutSel,
    SSMR3PutMem,
    SSMR3PutStrZ,
    SSMR3GetStruct,
    SSMR3GetStructEx,
    SSMR3GetBool,
    SSMR3GetBoolV,
    SSMR3GetU8,
    SSMR3GetU8V,
    SSMR3GetS8,
    SSMR3GetS8V,
    SSMR3GetU16,
    SSMR3GetU16V,
    SSMR3GetS16,
    SSMR3GetS16V,
    SSMR3GetU32,
    SSMR3GetU32V,
    SSMR3GetS32,
    SSMR3GetS32V,
    SSMR3GetU64,
    SSMR3GetU64V,
    SSMR3GetS64,
    SSMR3GetS64V,
    SSMR3GetU128,
    SSMR3GetU128V,
    SSMR3GetS128,
    SSMR3GetS128V,
    SSMR3GetGCPhys32,
    SSMR3GetGCPhys32V,
    SSMR3GetGCPhys64,
    SSMR3GetGCPhys64V,
    SSMR3GetGCPhys,
    SSMR3GetGCPhysV,
    SSMR3GetUInt,
    SSMR3GetSInt,
    SSMR3GetGCUInt,
    SSMR3GetGCUIntReg,
    SSMR3GetGCPtr,
    SSMR3GetGCUIntPtr,
    SSMR3GetRCPtr,
    SSMR3GetIOPort,
    SSMR3GetSel,
    SSMR3GetMem,
    SSMR3GetStrZ,
    SSMR3GetStrZEx,
    SSMR3Skip,
    SSMR3SkipToEndOfUnit,
    SSMR3SetLoadError,
    SSMR3SetLoadErrorV,
    SSMR3SetCfgError,
    SSMR3SetCfgErrorV,
    SSMR3HandleGetStatus,
    SSMR3HandleGetAfter,
    SSMR3HandleIsLiveSave,
    SSMR3HandleMaxDowntime,
    SSMR3HandleHostBits,
    SSMR3HandleRevision,
    SSMR3HandleVersion,
    SSMR3HandleHostOSAndArch,
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
    TMR3TimerSkip,
    pdmR3DevHlp_TMUtcNow,
    CFGMR3Exists,
    CFGMR3QueryType,
    CFGMR3QuerySize,
    CFGMR3QueryInteger,
    CFGMR3QueryIntegerDef,
    CFGMR3QueryString,
    CFGMR3QueryStringDef,
    CFGMR3QueryPassword,
    CFGMR3QueryPasswordDef,
    CFGMR3QueryBytes,
    CFGMR3QueryU64,
    CFGMR3QueryU64Def,
    CFGMR3QueryS64,
    CFGMR3QueryS64Def,
    CFGMR3QueryU32,
    CFGMR3QueryU32Def,
    CFGMR3QueryS32,
    CFGMR3QueryS32Def,
    CFGMR3QueryU16,
    CFGMR3QueryU16Def,
    CFGMR3QueryS16,
    CFGMR3QueryS16Def,
    CFGMR3QueryU8,
    CFGMR3QueryU8Def,
    CFGMR3QueryS8,
    CFGMR3QueryS8Def,
    CFGMR3QueryBool,
    CFGMR3QueryBoolDef,
    CFGMR3QueryPort,
    CFGMR3QueryPortDef,
    CFGMR3QueryUInt,
    CFGMR3QueryUIntDef,
    CFGMR3QuerySInt,
    CFGMR3QuerySIntDef,
    CFGMR3QueryGCPtr,
    CFGMR3QueryGCPtrDef,
    CFGMR3QueryGCPtrU,
    CFGMR3QueryGCPtrUDef,
    CFGMR3QueryGCPtrS,
    CFGMR3QueryGCPtrSDef,
    CFGMR3QueryStringAlloc,
    CFGMR3QueryStringAllocDef,
    CFGMR3GetParent,
    CFGMR3GetChild,
    CFGMR3GetChildF,
    CFGMR3GetChildFV,
    CFGMR3GetFirstChild,
    CFGMR3GetNextChild,
    CFGMR3GetName,
    CFGMR3GetNameLen,
    CFGMR3AreChildrenValid,
    CFGMR3GetFirstValue,
    CFGMR3GetNextValue,
    CFGMR3GetValueName,
    CFGMR3GetValueNameLen,
    CFGMR3GetValueType,
    CFGMR3AreValuesValid,
    CFGMR3ValidateConfig,
    pdmR3DevHlpTracing_PhysRead,
    pdmR3DevHlpTracing_PhysWrite,
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
    pdmR3DevHlpTracing_PCIPhysRead,
    pdmR3DevHlpTracing_PCIPhysWrite,
    pdmR3DevHlp_PCIPhysGCPhys2CCPtr,
    pdmR3DevHlp_PCIPhysGCPhys2CCPtrReadOnly,
    pdmR3DevHlp_PCIPhysBulkGCPhys2CCPtr,
    pdmR3DevHlp_PCIPhysBulkGCPhys2CCPtrReadOnly,
    pdmR3DevHlpTracing_PCISetIrq,
    pdmR3DevHlpTracing_PCISetIrqNoWait,
    pdmR3DevHlpTracing_ISASetIrq,
    pdmR3DevHlpTracing_ISASetIrqNoWait,
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
    PDMR3ThreadDestroy,
    PDMR3ThreadIAmSuspending,
    PDMR3ThreadIAmRunning,
    PDMR3ThreadSleep,
    PDMR3ThreadSuspend,
    PDMR3ThreadResume,
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
    pdmR3DevHlp_CpuGetGuestArch,
    pdmR3DevHlp_CpuGetGuestMicroarch,
    pdmR3DevHlp_CpuGetGuestAddrWidths,
    pdmR3DevHlp_CpuGetGuestScalableBusFrequency,
    pdmR3DevHlp_STAMDeregisterByPrefix,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
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
#endif /* VBOX_WITH_DBGF_TRACING */




/** @interface_method_impl{PDMDEVHLPR3,pfnGetUVM} */
static DECLCALLBACK(PUVM) pdmR3DevHlp_Untrusted_GetUVM(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return NULL;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetVM} */
static DECLCALLBACK(PVM) pdmR3DevHlp_Untrusted_GetVM(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return NULL;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetVMCPU} */
static DECLCALLBACK(PVMCPU) pdmR3DevHlp_Untrusted_GetVMCPU(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return NULL;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetCurrentCpuId} */
static DECLCALLBACK(VMCPUID) pdmR3DevHlp_Untrusted_GetCurrentCpuId(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return NIL_VMCPUID;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnRegisterVMMDevHeap} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_RegisterVMMDevHeap(PPDMDEVINS pDevIns, RTGCPHYS GCPhys,
                                                                  RTR3PTR pvHeap, unsigned cbHeap)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    NOREF(GCPhys); NOREF(pvHeap); NOREF(cbHeap);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnFirmwareRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_FirmwareRegister(PPDMDEVINS pDevIns, PCPDMFWREG pFwReg, PCPDMFWHLPR3 *ppFwHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    NOREF(pFwReg); NOREF(ppFwHlp);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMReset} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_VMReset(PPDMDEVINS pDevIns, uint32_t fFlags)
{
    PDMDEV_ASSERT_DEVINS(pDevIns); NOREF(fFlags);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMSuspend} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_VMSuspend(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMSuspendSaveAndPowerOff} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_VMSuspendSaveAndPowerOff(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMPowerOff} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_VMPowerOff(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnA20IsEnabled} */
static DECLCALLBACK(bool) pdmR3DevHlp_Untrusted_A20IsEnabled(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return false;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnA20Set} */
static DECLCALLBACK(void) pdmR3DevHlp_Untrusted_A20Set(PPDMDEVINS pDevIns, bool fEnable)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    NOREF(fEnable);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetCpuId} */
static DECLCALLBACK(void) pdmR3DevHlp_Untrusted_GetCpuId(PPDMDEVINS pDevIns, uint32_t iLeaf,
                                                         uint32_t *pEax, uint32_t *pEbx, uint32_t *pEcx, uint32_t *pEdx)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    NOREF(iLeaf); NOREF(pEax); NOREF(pEbx); NOREF(pEcx); NOREF(pEdx);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetMainExecutionEngine} */
static DECLCALLBACK(uint8_t) pdmR3DevHlp_Untrusted_GetMainExecutionEngine(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return VM_EXEC_ENGINE_NOT_SET;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGetSupDrvSession} */
static DECLCALLBACK(PSUPDRVSESSION) pdmR3DevHlp_Untrusted_GetSupDrvSession(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return (PSUPDRVSESSION)0;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnQueryGenericUserObject} */
static DECLCALLBACK(void *) pdmR3DevHlp_Untrusted_QueryGenericUserObject(PPDMDEVINS pDevIns, PCRTUUID pUuid)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d %RTuuid\n",
                            pDevIns->pReg->szName, pDevIns->iInstance, pUuid));
    return NULL;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPGMHandlerPhysicalTypeRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_PGMHandlerPhysicalTypeRegister(PPDMDEVINS pDevIns, PGMPHYSHANDLERKIND enmKind,
                                                                              PFNPGMPHYSHANDLER pfnHandler,
                                                                              const char *pszDesc, PPGMPHYSHANDLERTYPE phType)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns, enmKind, pfnHandler, pszDesc);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    *phType = NIL_PGMPHYSHANDLERTYPE;
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPGMHandlerPhysicalRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_PGMHandlerPhysicalRegister(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, RTGCPHYS GCPhysLast,
                                                                          PGMPHYSHANDLERTYPE hType, R3PTRTYPE(const char *) pszDesc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(GCPhys, GCPhysLast, hType, pszDesc);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPGMHandlerPhysicalDeregister} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_PGMHandlerPhysicalDeregister(PPDMDEVINS pDevIns, RTGCPHYS GCPhys)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(GCPhys);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPGMHandlerPhysicalPageTempOff} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_PGMHandlerPhysicalPageTempOff(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, RTGCPHYS GCPhysPage)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(GCPhys, GCPhysPage);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPGMHandlerPhysicalReset} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_PGMHandlerPhysicalReset(PPDMDEVINS pDevIns, RTGCPHYS GCPhys)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(GCPhys);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMMRegisterPatchMemory} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_VMMRegisterPatchMemory(PPDMDEVINS pDevIns, RTGCPTR GCPtrPatchMem, uint32_t cbPatchMem)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(GCPtrPatchMem, cbPatchMem);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnVMMDeregisterPatchMemory} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_VMMDeregisterPatchMemory(PPDMDEVINS pDevIns, RTGCPTR GCPtrPatchMem, uint32_t cbPatchMem)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(GCPtrPatchMem, cbPatchMem);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSharedModuleRegister} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_SharedModuleRegister(PPDMDEVINS pDevIns, VBOXOSFAMILY enmGuestOS, char *pszModuleName, char *pszVersion,
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
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_SharedModuleUnregister(PPDMDEVINS pDevIns, char *pszModuleName, char *pszVersion,
                                                                      RTGCPTR GCBaseAddr, uint32_t cbModule)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pszModuleName, pszVersion, GCBaseAddr, cbModule);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSharedModuleGetPageState} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_SharedModuleGetPageState(PPDMDEVINS pDevIns, RTGCPTR GCPtrPage, bool *pfShared, uint64_t *pfPageFlags)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(GCPtrPage, pfShared, pfPageFlags);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnSharedModuleCheckAll} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_SharedModuleCheckAll(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnQueryLun} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_QueryLun(PPDMDEVINS pDevIns, const char *pszDevice, unsigned iInstance, unsigned iLun, PPDMIBASE *ppBase)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pszDevice, iInstance, iLun, ppBase);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGIMDeviceRegister} */
static DECLCALLBACK(void) pdmR3DevHlp_Untrusted_GIMDeviceRegister(PPDMDEVINS pDevIns, PGIMDEBUG pDbg)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDbg);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGIMGetDebugSetup} */
static DECLCALLBACK(int) pdmR3DevHlp_Untrusted_GIMGetDebugSetup(PPDMDEVINS pDevIns, PGIMDEBUGSETUP pDbgSetup)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDbgSetup);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return VERR_ACCESS_DENIED;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnGIMGetMmio2Regions} */
static DECLCALLBACK(PGIMMMIO2REGION) pdmR3DevHlp_Untrusted_GIMGetMmio2Regions(PPDMDEVINS pDevIns, uint32_t *pcRegions)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pcRegions);
    AssertReleaseMsgFailed(("Untrusted device called trusted helper! '%s'/%d\n",
                            pDevIns->pReg->szName, pDevIns->iInstance));
    return NULL;
}


/**
 * The device helper structure for non-trusted devices.
 */
const PDMDEVHLPR3 g_pdmR3DevHlpUnTrusted =
{
    PDM_DEVHLPR3_VERSION,
    pdmR3DevHlp_IoPortCreateEx,
    pdmR3DevHlp_IoPortMap,
    pdmR3DevHlp_IoPortUnmap,
    pdmR3DevHlp_IoPortGetMappingAddress,
    pdmR3DevHlp_IoPortRead,
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
    SSMR3PutStruct,
    SSMR3PutStructEx,
    SSMR3PutBool,
    SSMR3PutU8,
    SSMR3PutS8,
    SSMR3PutU16,
    SSMR3PutS16,
    SSMR3PutU32,
    SSMR3PutS32,
    SSMR3PutU64,
    SSMR3PutS64,
    SSMR3PutU128,
    SSMR3PutS128,
    SSMR3PutUInt,
    SSMR3PutSInt,
    SSMR3PutGCUInt,
    SSMR3PutGCUIntReg,
    SSMR3PutGCPhys32,
    SSMR3PutGCPhys64,
    SSMR3PutGCPhys,
    SSMR3PutGCPtr,
    SSMR3PutGCUIntPtr,
    SSMR3PutRCPtr,
    SSMR3PutIOPort,
    SSMR3PutSel,
    SSMR3PutMem,
    SSMR3PutStrZ,
    SSMR3GetStruct,
    SSMR3GetStructEx,
    SSMR3GetBool,
    SSMR3GetBoolV,
    SSMR3GetU8,
    SSMR3GetU8V,
    SSMR3GetS8,
    SSMR3GetS8V,
    SSMR3GetU16,
    SSMR3GetU16V,
    SSMR3GetS16,
    SSMR3GetS16V,
    SSMR3GetU32,
    SSMR3GetU32V,
    SSMR3GetS32,
    SSMR3GetS32V,
    SSMR3GetU64,
    SSMR3GetU64V,
    SSMR3GetS64,
    SSMR3GetS64V,
    SSMR3GetU128,
    SSMR3GetU128V,
    SSMR3GetS128,
    SSMR3GetS128V,
    SSMR3GetGCPhys32,
    SSMR3GetGCPhys32V,
    SSMR3GetGCPhys64,
    SSMR3GetGCPhys64V,
    SSMR3GetGCPhys,
    SSMR3GetGCPhysV,
    SSMR3GetUInt,
    SSMR3GetSInt,
    SSMR3GetGCUInt,
    SSMR3GetGCUIntReg,
    SSMR3GetGCPtr,
    SSMR3GetGCUIntPtr,
    SSMR3GetRCPtr,
    SSMR3GetIOPort,
    SSMR3GetSel,
    SSMR3GetMem,
    SSMR3GetStrZ,
    SSMR3GetStrZEx,
    SSMR3Skip,
    SSMR3SkipToEndOfUnit,
    SSMR3SetLoadError,
    SSMR3SetLoadErrorV,
    SSMR3SetCfgError,
    SSMR3SetCfgErrorV,
    SSMR3HandleGetStatus,
    SSMR3HandleGetAfter,
    SSMR3HandleIsLiveSave,
    SSMR3HandleMaxDowntime,
    SSMR3HandleHostBits,
    SSMR3HandleRevision,
    SSMR3HandleVersion,
    SSMR3HandleHostOSAndArch,
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
    TMR3TimerSkip,
    pdmR3DevHlp_TMUtcNow,
    CFGMR3Exists,
    CFGMR3QueryType,
    CFGMR3QuerySize,
    CFGMR3QueryInteger,
    CFGMR3QueryIntegerDef,
    CFGMR3QueryString,
    CFGMR3QueryStringDef,
    CFGMR3QueryPassword,
    CFGMR3QueryPasswordDef,
    CFGMR3QueryBytes,
    CFGMR3QueryU64,
    CFGMR3QueryU64Def,
    CFGMR3QueryS64,
    CFGMR3QueryS64Def,
    CFGMR3QueryU32,
    CFGMR3QueryU32Def,
    CFGMR3QueryS32,
    CFGMR3QueryS32Def,
    CFGMR3QueryU16,
    CFGMR3QueryU16Def,
    CFGMR3QueryS16,
    CFGMR3QueryS16Def,
    CFGMR3QueryU8,
    CFGMR3QueryU8Def,
    CFGMR3QueryS8,
    CFGMR3QueryS8Def,
    CFGMR3QueryBool,
    CFGMR3QueryBoolDef,
    CFGMR3QueryPort,
    CFGMR3QueryPortDef,
    CFGMR3QueryUInt,
    CFGMR3QueryUIntDef,
    CFGMR3QuerySInt,
    CFGMR3QuerySIntDef,
    CFGMR3QueryGCPtr,
    CFGMR3QueryGCPtrDef,
    CFGMR3QueryGCPtrU,
    CFGMR3QueryGCPtrUDef,
    CFGMR3QueryGCPtrS,
    CFGMR3QueryGCPtrSDef,
    CFGMR3QueryStringAlloc,
    CFGMR3QueryStringAllocDef,
    CFGMR3GetParent,
    CFGMR3GetChild,
    CFGMR3GetChildF,
    CFGMR3GetChildFV,
    CFGMR3GetFirstChild,
    CFGMR3GetNextChild,
    CFGMR3GetName,
    CFGMR3GetNameLen,
    CFGMR3AreChildrenValid,
    CFGMR3GetFirstValue,
    CFGMR3GetNextValue,
    CFGMR3GetValueName,
    CFGMR3GetValueNameLen,
    CFGMR3GetValueType,
    CFGMR3AreValuesValid,
    CFGMR3ValidateConfig,
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
    PDMR3ThreadDestroy,
    PDMR3ThreadIAmSuspending,
    PDMR3ThreadIAmRunning,
    PDMR3ThreadSleep,
    PDMR3ThreadSuspend,
    PDMR3ThreadResume,
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
    pdmR3DevHlp_CpuGetGuestArch,
    pdmR3DevHlp_CpuGetGuestMicroarch,
    pdmR3DevHlp_CpuGetGuestAddrWidths,
    pdmR3DevHlp_CpuGetGuestScalableBusFrequency,
    pdmR3DevHlp_STAMDeregisterByPrefix,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    pdmR3DevHlp_Untrusted_GetUVM,
    pdmR3DevHlp_Untrusted_GetVM,
    pdmR3DevHlp_Untrusted_GetVMCPU,
    pdmR3DevHlp_Untrusted_GetCurrentCpuId,
    pdmR3DevHlp_Untrusted_RegisterVMMDevHeap,
    pdmR3DevHlp_Untrusted_FirmwareRegister,
    pdmR3DevHlp_Untrusted_VMReset,
    pdmR3DevHlp_Untrusted_VMSuspend,
    pdmR3DevHlp_Untrusted_VMSuspendSaveAndPowerOff,
    pdmR3DevHlp_Untrusted_VMPowerOff,
    pdmR3DevHlp_Untrusted_A20IsEnabled,
    pdmR3DevHlp_Untrusted_A20Set,
    pdmR3DevHlp_Untrusted_GetCpuId,
    pdmR3DevHlp_Untrusted_GetMainExecutionEngine,
    pdmR3DevHlp_TMTimeVirtGet,
    pdmR3DevHlp_TMTimeVirtGetFreq,
    pdmR3DevHlp_TMTimeVirtGetNano,
    pdmR3DevHlp_TMCpuTicksPerSecond,
    pdmR3DevHlp_Untrusted_GetSupDrvSession,
    pdmR3DevHlp_Untrusted_QueryGenericUserObject,
    pdmR3DevHlp_Untrusted_PGMHandlerPhysicalTypeRegister,
    pdmR3DevHlp_Untrusted_PGMHandlerPhysicalRegister,
    pdmR3DevHlp_Untrusted_PGMHandlerPhysicalDeregister,
    pdmR3DevHlp_Untrusted_PGMHandlerPhysicalPageTempOff,
    pdmR3DevHlp_Untrusted_PGMHandlerPhysicalReset,
    pdmR3DevHlp_Untrusted_VMMRegisterPatchMemory,
    pdmR3DevHlp_Untrusted_VMMDeregisterPatchMemory,
    pdmR3DevHlp_Untrusted_SharedModuleRegister,
    pdmR3DevHlp_Untrusted_SharedModuleUnregister,
    pdmR3DevHlp_Untrusted_SharedModuleGetPageState,
    pdmR3DevHlp_Untrusted_SharedModuleCheckAll,
    pdmR3DevHlp_Untrusted_QueryLun,
    pdmR3DevHlp_Untrusted_GIMDeviceRegister,
    pdmR3DevHlp_Untrusted_GIMGetDebugSetup,
    pdmR3DevHlp_Untrusted_GIMGetMmio2Regions,
    PDM_DEVHLPR3_VERSION /* the end */
};



/**
 * Queue consumer callback for internal component.
 *
 * @returns Success indicator.
 *          If false the item will not be removed and the flushing will stop.
 * @param   pVM         The cross context VM structure.
 * @param   pItem       The item to consume. Upon return this item will be freed.
 */
DECLCALLBACK(bool) pdmR3DevHlpQueueConsumer(PVM pVM, PPDMQUEUEITEMCORE pItem)
{
    PPDMDEVHLPTASK pTask = (PPDMDEVHLPTASK)pItem;
    LogFlow(("pdmR3DevHlpQueueConsumer: enmOp=%d pDevIns=%p\n", pTask->enmOp, pTask->pDevInsR3));
    switch (pTask->enmOp)
    {
        case PDMDEVHLPTASKOP_ISA_SET_IRQ:
            PDMIsaSetIrq(pVM, pTask->u.IsaSetIrq.iIrq, pTask->u.IsaSetIrq.iLevel, pTask->u.IsaSetIrq.uTagSrc);
            break;

        case PDMDEVHLPTASKOP_PCI_SET_IRQ:
        {
            /* Same as pdmR3DevHlp_PCISetIrq, except we've got a tag already. */
            PPDMDEVINSR3 pDevIns = pTask->pDevInsR3;
            PPDMPCIDEV   pPciDev = pTask->u.PciSetIrq.idxPciDev < RT_ELEMENTS(pDevIns->apPciDevs)
                                 ? pDevIns->apPciDevs[pTask->u.PciSetIrq.idxPciDev] : NULL;
            if (pPciDev)
            {
                size_t const idxBus = pPciDev->Int.s.idxPdmBus;
                AssertBreak(idxBus < RT_ELEMENTS(pVM->pdm.s.aPciBuses));
                PPDMPCIBUS   pBus   = &pVM->pdm.s.aPciBuses[idxBus];

                pdmLock(pVM);
                pBus->pfnSetIrqR3(pBus->pDevInsR3, pPciDev, pTask->u.PciSetIrq.iIrq,
                                  pTask->u.PciSetIrq.iLevel, pTask->u.PciSetIrq.uTagSrc);
                pdmUnlock(pVM);
            }
            else
                AssertReleaseMsgFailed(("No PCI device given! (%#x)\n", pPciDev->Int.s.idxSubDev));
            break;
        }

        case PDMDEVHLPTASKOP_IOAPIC_SET_IRQ:
        {
            PDMIoApicSetIrq(pVM, pTask->u.IoApicSetIrq.uBusDevFn, pTask->u.IoApicSetIrq.iIrq, pTask->u.IoApicSetIrq.iLevel,
                            pTask->u.IoApicSetIrq.uTagSrc);
            break;
        }

        case PDMDEVHLPTASKOP_IOAPIC_SEND_MSI:
        {
            PDMIoApicSendMsi(pVM, pTask->u.IoApicSendMsi.uBusDevFn, &pTask->u.IoApicSendMsi.Msi, pTask->u.IoApicSendMsi.uTagSrc);
            break;
        }

        case PDMDEVHLPTASKOP_IOAPIC_SET_EOI:
        {
            PDMIoApicBroadcastEoi(pVM, pTask->u.IoApicSetEoi.uVector);
            break;
        }

        default:
            AssertReleaseMsgFailed(("Invalid operation %d\n", pTask->enmOp));
            break;
    }
    return true;
}

/** @} */

