/* $Id: PDMR0DevHlpTracing.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM - Pluggable Device and Driver Manager, Device Helper variants when tracing is enabled.
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
#include <VBox/err.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include "dtrace/VBoxVMM.h"
#include "PDMInline.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @name Ring-0 Device Helpers
 * @{
 */


static DECLCALLBACK(VBOXSTRICTRC) pdmR0DevHlpTracing_IoPortNewIn(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PCPDMDEVINSDBGFTRACK pTrack = (PCPDMDEVINSDBGFTRACK)pvUser;

    Assert(!pTrack->fMmio);
    PGVM pGVM = pDevIns->Internal.s.pGVM;
    VBOXSTRICTRC rcStrict = pTrack->u.IoPort.pfnIn(pDevIns, pTrack->pvUser, offPort, pu32, cb);
    if (RT_SUCCESS(rcStrict))
        DBGFTracerEvtIoPortRead(pGVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, pTrack->u.IoPort.hIoPorts, offPort, pu32, cb);

    return rcStrict;
}


static DECLCALLBACK(VBOXSTRICTRC) pdmR0DevHlpTracing_IoPortNewInStr(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint8_t *pbDst,
                                                                    uint32_t *pcTransfers, unsigned cb)
{
    PCPDMDEVINSDBGFTRACK pTrack = (PCPDMDEVINSDBGFTRACK)pvUser;

    Assert(!pTrack->fMmio);
    PGVM pGVM = pDevIns->Internal.s.pGVM;
    uint32_t cTransfersReq = *pcTransfers;
    VBOXSTRICTRC rcStrict = pTrack->u.IoPort.pfnInStr(pDevIns, pTrack->pvUser, offPort, pbDst, pcTransfers, cb);
    if (RT_SUCCESS(rcStrict))
        DBGFTracerEvtIoPortReadStr(pGVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, pTrack->u.IoPort.hIoPorts, offPort, pbDst, cb,
                                   cTransfersReq, cTransfersReq - *pcTransfers);

    return rcStrict;
}


static DECLCALLBACK(VBOXSTRICTRC) pdmR0DevHlpTracing_IoPortNewOut(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PCPDMDEVINSDBGFTRACK pTrack = (PCPDMDEVINSDBGFTRACK)pvUser;

    Assert(!pTrack->fMmio);
    PGVM pGVM = pDevIns->Internal.s.pGVM;
    VBOXSTRICTRC rcStrict = pTrack->u.IoPort.pfnOut(pDevIns, pTrack->pvUser, offPort, u32, cb);
    if (RT_SUCCESS(rcStrict))
        DBGFTracerEvtIoPortWrite(pGVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, pTrack->u.IoPort.hIoPorts, offPort, &u32, cb);

    return rcStrict;
}


static DECLCALLBACK(VBOXSTRICTRC) pdmR0DevHlpTracing_IoPortNewOutStr(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, const uint8_t *pbSrc,
                                                                     uint32_t *pcTransfers, unsigned cb)
{
    PCPDMDEVINSDBGFTRACK pTrack = (PCPDMDEVINSDBGFTRACK)pvUser;

    Assert(!pTrack->fMmio);
    PGVM pGVM = pDevIns->Internal.s.pGVM;
    uint32_t cTransfersReq = *pcTransfers;
    VBOXSTRICTRC rcStrict = pTrack->u.IoPort.pfnOutStr(pDevIns, pTrack->pvUser, offPort, pbSrc, pcTransfers, cb);
    if (RT_SUCCESS(rcStrict))
        DBGFTracerEvtIoPortWriteStr(pGVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, pTrack->u.IoPort.hIoPorts, offPort, pbSrc, cb,
                                    cTransfersReq, cTransfersReq - *pcTransfers);

    return rcStrict;
}


static DECLCALLBACK(VBOXSTRICTRC) pdmR0DevHlpTracing_MmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, uint32_t cb)
{
    PCPDMDEVINSDBGFTRACK pTrack = (PCPDMDEVINSDBGFTRACK)pvUser;

    Assert(pTrack->fMmio);
    PGVM pGVM = pDevIns->Internal.s.pGVM;
    VBOXSTRICTRC rcStrict = pTrack->u.Mmio.pfnRead(pDevIns, pTrack->pvUser, off, pv, cb);
    if (RT_SUCCESS(rcStrict))
        DBGFTracerEvtMmioRead(pGVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, pTrack->u.Mmio.hMmioRegion, off, pv, cb);

    return rcStrict;
}


static DECLCALLBACK(VBOXSTRICTRC) pdmR0DevHlpTracing_MmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, uint32_t cb)
{
    PCPDMDEVINSDBGFTRACK pTrack = (PCPDMDEVINSDBGFTRACK)pvUser;

    Assert(pTrack->fMmio);
    PGVM pGVM = pDevIns->Internal.s.pGVM;
    VBOXSTRICTRC rcStrict = pTrack->u.Mmio.pfnWrite(pDevIns, pTrack->pvUser, off, pv, cb);
    if (RT_SUCCESS(rcStrict))
        DBGFTracerEvtMmioWrite(pGVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, pTrack->u.Mmio.hMmioRegion, off, pv, cb);

    return rcStrict;
}


static DECLCALLBACK(VBOXSTRICTRC) pdmR0DevHlpTracing_MmioFill(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off,
                                                              uint32_t u32Item, uint32_t cbItem, uint32_t cItems)
{
    PCPDMDEVINSDBGFTRACK pTrack = (PCPDMDEVINSDBGFTRACK)pvUser;

    Assert(pTrack->fMmio);
    PGVM pGVM = pDevIns->Internal.s.pGVM;
    VBOXSTRICTRC rcStrict = pTrack->u.Mmio.pfnFill(pDevIns, pTrack->pvUser, off, u32Item, cbItem, cItems);
    if (RT_SUCCESS(rcStrict))
        DBGFTracerEvtMmioFill(pGVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, pTrack->u.Mmio.hMmioRegion, off,
                              u32Item, cbItem, cItems);

    return rcStrict;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnIoPortSetUpContextEx} */
DECL_HIDDEN_CALLBACK(int)
pdmR0DevHlpTracing_IoPortSetUpContextEx(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts,
                                        PFNIOMIOPORTNEWOUT pfnOut, PFNIOMIOPORTNEWIN pfnIn,
                                        PFNIOMIOPORTNEWOUTSTRING pfnOutStr, PFNIOMIOPORTNEWINSTRING pfnInStr, void *pvUser)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_IoPortSetUpContextEx: caller='%s'/%d: hIoPorts=%#x pfnOut=%p pfnIn=%p pfnOutStr=%p pfnInStr=%p pvUser=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hIoPorts, pfnOut, pfnIn, pfnOutStr, pfnInStr, pvUser));
    PGVM pGVM = pDevIns->Internal.s.pGVM;
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);

    int rc = VINF_SUCCESS;
    if (pDevIns->Internal.s.idxDbgfTraceTrackNext < pDevIns->Internal.s.cDbgfTraceTrackMax)
    {
        PPDMDEVINSDBGFTRACK pTrack = &pDevIns->Internal.s.paDbgfTraceTrack[pDevIns->Internal.s.idxDbgfTraceTrackNext];
        rc = IOMR0IoPortSetUpContext(pGVM, pDevIns, hIoPorts,
                                     pfnOut    ? pdmR0DevHlpTracing_IoPortNewOut    : NULL,
                                     pfnIn     ? pdmR0DevHlpTracing_IoPortNewIn     : NULL,
                                     pfnOutStr ? pdmR0DevHlpTracing_IoPortNewOutStr : NULL,
                                     pfnInStr  ? pdmR0DevHlpTracing_IoPortNewInStr  : NULL,
                                     pTrack);
        if (RT_SUCCESS(rc))
        {
            pTrack->fMmio              = false;
            pTrack->pvUser             = pvUser;
            pTrack->u.IoPort.hIoPorts  = hIoPorts;
            pTrack->u.IoPort.pfnOut    = pfnOut;
            pTrack->u.IoPort.pfnIn     = pfnIn;
            pTrack->u.IoPort.pfnOutStr = pfnOutStr;
            pTrack->u.IoPort.pfnInStr  = pfnInStr;
            pDevIns->Internal.s.idxDbgfTraceTrackNext++;
        }
    }
    else
        rc = VERR_OUT_OF_RESOURCES;

    LogFlow(("pdmR0DevHlp_IoPortSetUpContextEx: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnMmioSetUpContextEx} */
DECL_HIDDEN_CALLBACK(int)
pdmR0DevHlpTracing_MmioSetUpContextEx(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, PFNIOMMMIONEWWRITE pfnWrite,
                                      PFNIOMMMIONEWREAD pfnRead, PFNIOMMMIONEWFILL pfnFill, void *pvUser)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_MmioSetUpContextEx: caller='%s'/%d: hRegion=%#x pfnWrite=%p pfnRead=%p pfnFill=%p pvUser=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hRegion, pfnWrite, pfnRead, pfnFill, pvUser));
    PGVM pGVM = pDevIns->Internal.s.pGVM;
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);

    int rc = VINF_SUCCESS;
    if (pDevIns->Internal.s.idxDbgfTraceTrackNext < pDevIns->Internal.s.cDbgfTraceTrackMax)
    {
        PPDMDEVINSDBGFTRACK pTrack = &pDevIns->Internal.s.paDbgfTraceTrack[pDevIns->Internal.s.idxDbgfTraceTrackNext];

        rc = IOMR0MmioSetUpContext(pGVM, pDevIns, hRegion,
                                   pfnWrite ? pdmR0DevHlpTracing_MmioWrite : NULL,
                                   pfnRead  ? pdmR0DevHlpTracing_MmioRead  : NULL,
                                   pfnFill  ? pdmR0DevHlpTracing_MmioFill  : NULL,
                                   pTrack);
        if (RT_SUCCESS(rc))
        {
            pTrack->fMmio              = true;
            pTrack->pvUser             = pvUser;
            pTrack->u.Mmio.hMmioRegion = hRegion;
            pTrack->u.Mmio.pfnWrite    = pfnWrite;
            pTrack->u.Mmio.pfnRead     = pfnRead;
            pTrack->u.Mmio.pfnFill     = pfnFill;
            pDevIns->Internal.s.idxDbgfTraceTrackNext++;
        }
    }
    else
        rc = VERR_OUT_OF_RESOURCES;

    LogFlow(("pdmR0DevHlp_MmioSetUpContextEx: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnPhysRead} */
DECL_HIDDEN_CALLBACK(int)
pdmR0DevHlpTracing_PhysRead(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead, uint32_t fFlags)
{
    RT_NOREF(fFlags);

    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_PhysRead: caller=%p/%d: GCPhys=%RGp pvBuf=%p cbRead=%#x\n",
             pDevIns, pDevIns->iInstance, GCPhys, pvBuf, cbRead));

    VBOXSTRICTRC rcStrict = PGMPhysRead(pDevIns->Internal.s.pGVM, GCPhys, pvBuf, cbRead, PGMACCESSORIGIN_DEVICE);
    AssertMsg(rcStrict == VINF_SUCCESS, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict))); /** @todo track down the users for this bugger. */

    if (!(fFlags & PDM_DEVHLP_PHYS_RW_F_DATA_USER))
        DBGFTracerEvtGCPhysRead(pDevIns->Internal.s.pGVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, GCPhys, pvBuf, cbRead);

    Log(("pdmR0DevHlp_PhysRead: caller=%p/%d: returns %Rrc\n", pDevIns, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict) ));
    return VBOXSTRICTRC_VAL(rcStrict);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnPhysWrite} */
DECL_HIDDEN_CALLBACK(int)
pdmR0DevHlpTracing_PhysWrite(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite, uint32_t fFlags)
{
    RT_NOREF(fFlags);

    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_PhysWrite: caller=%p/%d: GCPhys=%RGp pvBuf=%p cbWrite=%#x\n",
             pDevIns, pDevIns->iInstance, GCPhys, pvBuf, cbWrite));

    VBOXSTRICTRC rcStrict = PGMPhysWrite(pDevIns->Internal.s.pGVM, GCPhys, pvBuf, cbWrite, PGMACCESSORIGIN_DEVICE);
    AssertMsg(rcStrict == VINF_SUCCESS, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict))); /** @todo track down the users for this bugger. */

    if (!(fFlags & PDM_DEVHLP_PHYS_RW_F_DATA_USER))
        DBGFTracerEvtGCPhysWrite(pDevIns->Internal.s.pGVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, GCPhys, pvBuf, cbWrite);

    Log(("pdmR0DevHlp_PhysWrite: caller=%p/%d: returns %Rrc\n", pDevIns, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict) ));
    return VBOXSTRICTRC_VAL(rcStrict);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnPCIPhysRead} */
DECL_HIDDEN_CALLBACK(int)
pdmR0DevHlpTracing_PCIPhysRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead, uint32_t fFlags)
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
        Log(("pdmRCDevHlp_PCIPhysRead: caller=%p/%d: returns %Rrc - Not bus master! GCPhys=%RGp cbRead=%#zx\n",
             pDevIns, pDevIns->iInstance, VERR_PDM_NOT_PCI_BUS_MASTER, GCPhys, cbRead));
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

    return pDevIns->pHlpR0->pfnPhysRead(pDevIns, GCPhys, pvBuf, cbRead, fFlags);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnPCIPhysWrite} */
DECL_HIDDEN_CALLBACK(int)
pdmR0DevHlpTracing_PCIPhysWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite, uint32_t fFlags)
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
        LogFunc(("caller=%p/%d: returns %Rrc - Not bus master! GCPhys=%RGp cbWrite=%#zx\n", pDevIns, pDevIns->iInstance,
                 VERR_PDM_NOT_PCI_BUS_MASTER, GCPhys, cbWrite));
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

    return pDevIns->pHlpR0->pfnPhysWrite(pDevIns, GCPhys, pvBuf, cbWrite, fFlags);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnPCISetIrq} */
DECL_HIDDEN_CALLBACK(void) pdmR0DevHlpTracing_PCISetIrq(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq, int iLevel)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    if (!pPciDev) /* NULL is an alias for the default PCI device. */
        pPciDev = pDevIns->apPciDevs[0];
    AssertReturnVoid(pPciDev);
    LogFlow(("pdmR0DevHlpTracing_PCISetIrq: caller=%p/%d: pPciDev=%p:{%#x} iIrq=%d iLevel=%d\n",
             pDevIns, pDevIns->iInstance, pPciDev, pPciDev->uDevFn, iIrq, iLevel));
    PDMPCIDEV_ASSERT_VALID_AND_REGISTERED(pDevIns, pPciDev);

    PGVM         pGVM      = pDevIns->Internal.s.pGVM;
    size_t const idxBus    = pPciDev->Int.s.idxPdmBus;
    AssertReturnVoid(idxBus < RT_ELEMENTS(pGVM->pdmr0.s.aPciBuses));
    PPDMPCIBUSR0 pPciBusR0 = &pGVM->pdmr0.s.aPciBuses[idxBus];

    DBGFTracerEvtIrq(pGVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, iIrq, iLevel);

    pdmLock(pGVM);

    uint32_t uTagSrc;
    if (iLevel & PDM_IRQ_LEVEL_HIGH)
    {
        pDevIns->Internal.s.pIntR3R0->uLastIrqTag = uTagSrc = pdmCalcIrqTag(pGVM, pDevIns->Internal.s.pInsR3R0->idTracing);
        if (iLevel == PDM_IRQ_LEVEL_HIGH)
            VBOXVMM_PDM_IRQ_HIGH(VMMGetCpu(pGVM), RT_LOWORD(uTagSrc), RT_HIWORD(uTagSrc));
        else
            VBOXVMM_PDM_IRQ_HILO(VMMGetCpu(pGVM), RT_LOWORD(uTagSrc), RT_HIWORD(uTagSrc));
    }
    else
        uTagSrc = pDevIns->Internal.s.pIntR3R0->uLastIrqTag;

    if (pPciBusR0->pDevInsR0)
    {
        pPciBusR0->pfnSetIrqR0(pPciBusR0->pDevInsR0, pPciDev, iIrq, iLevel, uTagSrc);

        pdmUnlock(pGVM);

        if (iLevel == PDM_IRQ_LEVEL_LOW)
            VBOXVMM_PDM_IRQ_LOW(VMMGetCpu(pGVM), RT_LOWORD(uTagSrc), RT_HIWORD(uTagSrc));
    }
    else
    {
        pdmUnlock(pGVM);

        /* queue for ring-3 execution. */
        PPDMDEVHLPTASK pTask = (PPDMDEVHLPTASK)PDMQueueAlloc(pGVM, pGVM->pdm.s.hDevHlpQueue, pGVM);
        AssertReturnVoid(pTask);

        pTask->enmOp = PDMDEVHLPTASKOP_PCI_SET_IRQ;
        pTask->pDevInsR3 = PDMDEVINS_2_R3PTR(pDevIns);
        pTask->u.PciSetIrq.iIrq = iIrq;
        pTask->u.PciSetIrq.iLevel = iLevel;
        pTask->u.PciSetIrq.uTagSrc = uTagSrc;
        pTask->u.PciSetIrq.idxPciDev = pPciDev->Int.s.idxSubDev;

        PDMQueueInsert(pGVM, pGVM->pdm.s.hDevHlpQueue, pGVM, &pTask->Core);
    }

    LogFlow(("pdmR0DevHlpTracing_PCISetIrq: caller=%p/%d: returns void; uTagSrc=%#x\n", pDevIns, pDevIns->iInstance, uTagSrc));
}


/** @interface_method_impl{PDMDEVHLPR0,pfnISASetIrq} */
DECL_HIDDEN_CALLBACK(void) pdmR0DevHlpTracing_ISASetIrq(PPDMDEVINS pDevIns, int iIrq, int iLevel)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlpTracing_ISASetIrq: caller=%p/%d: iIrq=%d iLevel=%d\n", pDevIns, pDevIns->iInstance, iIrq, iLevel));
    PGVM pGVM = pDevIns->Internal.s.pGVM;

    DBGFTracerEvtIrq(pGVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, iIrq, iLevel);

    pdmLock(pGVM);
    uint32_t uTagSrc;
    if (iLevel & PDM_IRQ_LEVEL_HIGH)
    {
        pDevIns->Internal.s.pIntR3R0->uLastIrqTag = uTagSrc = pdmCalcIrqTag(pGVM, pDevIns->Internal.s.pInsR3R0->idTracing);
        if (iLevel == PDM_IRQ_LEVEL_HIGH)
            VBOXVMM_PDM_IRQ_HIGH(VMMGetCpu(pGVM), RT_LOWORD(uTagSrc), RT_HIWORD(uTagSrc));
        else
            VBOXVMM_PDM_IRQ_HILO(VMMGetCpu(pGVM), RT_LOWORD(uTagSrc), RT_HIWORD(uTagSrc));
    }
    else
        uTagSrc = pDevIns->Internal.s.pIntR3R0->uLastIrqTag;

    bool fRc = pdmR0IsaSetIrq(pGVM, iIrq, iLevel, uTagSrc);

    if (iLevel == PDM_IRQ_LEVEL_LOW && fRc)
        VBOXVMM_PDM_IRQ_LOW(VMMGetCpu(pGVM), RT_LOWORD(uTagSrc), RT_HIWORD(uTagSrc));
    pdmUnlock(pGVM);
    LogFlow(("pdmR0DevHlpTracing_ISASetIrq: caller=%p/%d: returns void; uTagSrc=%#x\n", pDevIns, pDevIns->iInstance, uTagSrc));
}


/** @} */

