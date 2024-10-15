/* $Id: PDMDevHlpTracing.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
/** @name R3 DevHlp
 * @{
 */


static DECLCALLBACK(VBOXSTRICTRC) pdmR3DevHlpTracing_IoPortNewIn(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PCPDMDEVINSDBGFTRACK pTrack = (PCPDMDEVINSDBGFTRACK)pvUser;

    Assert(!pTrack->fMmio);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VBOXSTRICTRC rcStrict = pTrack->u.IoPort.pfnIn(pDevIns, pTrack->pvUser, offPort, pu32, cb);
    if (RT_SUCCESS(rcStrict))
        DBGFTracerEvtIoPortRead(pVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, pTrack->u.IoPort.hIoPorts, offPort, pu32, cb);

    return rcStrict;
}


static DECLCALLBACK(VBOXSTRICTRC) pdmR3DevHlpTracing_IoPortNewInStr(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint8_t *pbDst,
                                                                    uint32_t *pcTransfers, unsigned cb)
{
    PCPDMDEVINSDBGFTRACK pTrack = (PCPDMDEVINSDBGFTRACK)pvUser;

    Assert(!pTrack->fMmio);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    uint32_t cTransfersReq = *pcTransfers;
    VBOXSTRICTRC rcStrict = pTrack->u.IoPort.pfnInStr(pDevIns, pTrack->pvUser, offPort, pbDst, pcTransfers, cb);
    if (RT_SUCCESS(rcStrict))
        DBGFTracerEvtIoPortReadStr(pVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, pTrack->u.IoPort.hIoPorts, offPort, pbDst, cb,
                                   cTransfersReq, cTransfersReq - *pcTransfers);

    return rcStrict;
}


static DECLCALLBACK(VBOXSTRICTRC) pdmR3DevHlpTracing_IoPortNewOut(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PCPDMDEVINSDBGFTRACK pTrack = (PCPDMDEVINSDBGFTRACK)pvUser;

    Assert(!pTrack->fMmio);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VBOXSTRICTRC rcStrict = pTrack->u.IoPort.pfnOut(pDevIns, pTrack->pvUser, offPort, u32, cb);
    if (RT_SUCCESS(rcStrict))
        DBGFTracerEvtIoPortWrite(pVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, pTrack->u.IoPort.hIoPorts, offPort, &u32, cb);

    return rcStrict;
}


static DECLCALLBACK(VBOXSTRICTRC) pdmR3DevHlpTracing_IoPortNewOutStr(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, const uint8_t *pbSrc,
                                                                     uint32_t *pcTransfers, unsigned cb)
{
    PCPDMDEVINSDBGFTRACK pTrack = (PCPDMDEVINSDBGFTRACK)pvUser;

    Assert(!pTrack->fMmio);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    uint32_t cTransfersReq = *pcTransfers;
    VBOXSTRICTRC rcStrict = pTrack->u.IoPort.pfnOutStr(pDevIns, pTrack->pvUser, offPort, pbSrc, pcTransfers, cb);
    if (RT_SUCCESS(rcStrict))
        DBGFTracerEvtIoPortWriteStr(pVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, pTrack->u.IoPort.hIoPorts, offPort, pbSrc, cb,
                                    cTransfersReq, cTransfersReq - *pcTransfers);

    return rcStrict;
}


static DECLCALLBACK(VBOXSTRICTRC) pdmR3DevHlpTracing_MmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, uint32_t cb)
{
    PCPDMDEVINSDBGFTRACK pTrack = (PCPDMDEVINSDBGFTRACK)pvUser;

    Assert(pTrack->fMmio);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VBOXSTRICTRC rcStrict = pTrack->u.Mmio.pfnRead(pDevIns, pTrack->pvUser, off, pv, cb);
    if (RT_SUCCESS(rcStrict))
        DBGFTracerEvtMmioRead(pVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, pTrack->u.Mmio.hMmioRegion, off, pv, cb);

    return rcStrict;
}


static DECLCALLBACK(VBOXSTRICTRC) pdmR3DevHlpTracing_MmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, uint32_t cb)
{
    PCPDMDEVINSDBGFTRACK pTrack = (PCPDMDEVINSDBGFTRACK)pvUser;

    Assert(pTrack->fMmio);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VBOXSTRICTRC rcStrict = pTrack->u.Mmio.pfnWrite(pDevIns, pTrack->pvUser, off, pv, cb);
    if (RT_SUCCESS(rcStrict))
        DBGFTracerEvtMmioWrite(pVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, pTrack->u.Mmio.hMmioRegion, off, pv, cb);

    return rcStrict;
}


static DECLCALLBACK(VBOXSTRICTRC) pdmR3DevHlpTracing_MmioFill(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off,
                                                              uint32_t u32Item, uint32_t cbItem, uint32_t cItems)
{
    PCPDMDEVINSDBGFTRACK pTrack = (PCPDMDEVINSDBGFTRACK)pvUser;

    Assert(pTrack->fMmio);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VBOXSTRICTRC rcStrict = pTrack->u.Mmio.pfnFill(pDevIns, pTrack->pvUser, off, u32Item, cbItem, cItems);
    if (RT_SUCCESS(rcStrict))
        DBGFTracerEvtMmioFill(pVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, pTrack->u.Mmio.hMmioRegion, off,
                              u32Item, cbItem, cItems);

    return rcStrict;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnIoPortCreateEx} */
DECL_HIDDEN_CALLBACK(int)
pdmR3DevHlpTracing_IoPortCreateEx(PPDMDEVINS pDevIns, RTIOPORT cPorts, uint32_t fFlags, PPDMPCIDEV pPciDev,
                                  uint32_t iPciRegion, PFNIOMIOPORTNEWOUT pfnOut, PFNIOMIOPORTNEWIN pfnIn,
                                  PFNIOMIOPORTNEWOUTSTRING pfnOutStr, PFNIOMIOPORTNEWINSTRING pfnInStr, RTR3PTR pvUser,
                                  const char *pszDesc, PCIOMIOPORTDESC paExtDescs, PIOMIOPORTHANDLE phIoPorts)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlpTracing_IoPortCreateEx: caller='%s'/%d: cPorts=%#x fFlags=%#x pPciDev=%p iPciRegion=%#x pfnOut=%p pfnIn=%p pfnOutStr=%p pfnInStr=%p pvUser=%p pszDesc=%p:{%s} paExtDescs=%p phIoPorts=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cPorts, fFlags, pPciDev, iPciRegion, pfnOut, pfnIn, pfnOutStr, pfnInStr,
             pvUser, pszDesc, pszDesc, paExtDescs, phIoPorts));
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);

    int rc = VINF_SUCCESS;
    if (pDevIns->Internal.s.idxDbgfTraceTrackNext < pDevIns->Internal.s.cDbgfTraceTrackMax)
    {
        PPDMDEVINSDBGFTRACK pTrack = &pDevIns->Internal.s.paDbgfTraceTrack[pDevIns->Internal.s.idxDbgfTraceTrackNext];

        rc = IOMR3IoPortCreate(pVM, pDevIns, cPorts, fFlags, pPciDev, iPciRegion,
                               pfnOut    ? pdmR3DevHlpTracing_IoPortNewOut    : NULL,
                               pfnIn     ? pdmR3DevHlpTracing_IoPortNewIn     : NULL,
                               pfnOutStr ? pdmR3DevHlpTracing_IoPortNewOutStr : NULL,
                               pfnInStr  ? pdmR3DevHlpTracing_IoPortNewInStr  : NULL,
                               pTrack, pszDesc, paExtDescs, phIoPorts);
        if (RT_SUCCESS(rc))
        {
            pTrack->fMmio              = false;
            pTrack->pvUser             = pvUser;
            pTrack->u.IoPort.hIoPorts  = *phIoPorts;
            pTrack->u.IoPort.pfnOut    = pfnOut;
            pTrack->u.IoPort.pfnIn     = pfnIn;
            pTrack->u.IoPort.pfnOutStr = pfnOutStr;
            pTrack->u.IoPort.pfnInStr  = pfnInStr;
            pDevIns->Internal.s.idxDbgfTraceTrackNext++;
            DBGFR3TracerEvtIoPortCreate(pVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, *phIoPorts, cPorts, fFlags, iPciRegion);
        }
    }
    else
        rc = VERR_OUT_OF_RESOURCES;

    LogFlow(("pdmR3DevHlpTracing_IoPortCreateEx: caller='%s'/%d: returns %Rrc (*phIoPorts=%#x)\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc, *phIoPorts));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnIoPortMap} */
DECL_HIDDEN_CALLBACK(int) pdmR3DevHlpTracing_IoPortMap(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts, RTIOPORT Port)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_IoPortMap: caller='%s'/%d: hIoPorts=%#x Port=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, hIoPorts, Port));
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);

    int rc = IOMR3IoPortMap(pVM, pDevIns, hIoPorts, Port);
    DBGFTracerEvtIoPortMap(pVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, hIoPorts, Port);

    LogFlow(("pdmR3DevHlp_IoPortMap: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnIoPortUnmap} */
DECL_HIDDEN_CALLBACK(int) pdmR3DevHlpTracing_IoPortUnmap(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_IoPortMap: caller='%s'/%d: hIoPorts=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, hIoPorts));
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);

    int rc = IOMR3IoPortUnmap(pVM, pDevIns, hIoPorts);
    DBGFTracerEvtIoPortUnmap(pVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, hIoPorts);

    LogFlow(("pdmR3DevHlp_IoPortMap: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmioCreateEx} */
DECL_HIDDEN_CALLBACK(int)
pdmR3DevHlpTracing_MmioCreateEx(PPDMDEVINS pDevIns, RTGCPHYS cbRegion,
                                uint32_t fFlags, PPDMPCIDEV pPciDev, uint32_t iPciRegion,
                                PFNIOMMMIONEWWRITE pfnWrite, PFNIOMMMIONEWREAD pfnRead, PFNIOMMMIONEWFILL pfnFill,
                                void *pvUser, const char *pszDesc, PIOMMMIOHANDLE phRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MmioCreateEx: caller='%s'/%d: cbRegion=%#RGp fFlags=%#x pPciDev=%p iPciRegion=%#x pfnWrite=%p pfnRead=%p pfnFill=%p pvUser=%p pszDesc=%p:{%s} phRegion=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, cbRegion, fFlags, pPciDev, iPciRegion, pfnWrite, pfnRead, pfnFill, pvUser, pszDesc, pszDesc, phRegion));
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);

    /* HACK ALERT! Round the size up to page size.  The PCI bus should do something similar before mapping it. */
    /** @todo It's possible we need to do dummy MMIO fill-in of the PCI bus or
     *        guest adds more alignment to an region. */
    cbRegion = RT_ALIGN_T(cbRegion, GUEST_PAGE_SIZE, RTGCPHYS);

    int rc = VINF_SUCCESS;
    if (pDevIns->Internal.s.idxDbgfTraceTrackNext < pDevIns->Internal.s.cDbgfTraceTrackMax)
    {
        PPDMDEVINSDBGFTRACK pTrack = &pDevIns->Internal.s.paDbgfTraceTrack[pDevIns->Internal.s.idxDbgfTraceTrackNext];

        rc = IOMR3MmioCreate(pVM, pDevIns, cbRegion, fFlags, pPciDev, iPciRegion,
                             pfnWrite ? pdmR3DevHlpTracing_MmioWrite : NULL,
                             pfnRead  ? pdmR3DevHlpTracing_MmioRead  : NULL,
                             pfnFill  ? pdmR3DevHlpTracing_MmioFill  : NULL,
                             pTrack, pszDesc, phRegion);
        if (RT_SUCCESS(rc))
        {
            pTrack->fMmio              = true;
            pTrack->pvUser             = pvUser;
            pTrack->u.Mmio.hMmioRegion = *phRegion;
            pTrack->u.Mmio.pfnWrite    = pfnWrite;
            pTrack->u.Mmio.pfnRead     = pfnRead;
            pTrack->u.Mmio.pfnFill     = pfnFill;
            pDevIns->Internal.s.idxDbgfTraceTrackNext++;
            DBGFR3TracerEvtMmioCreate(pVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, *phRegion, cbRegion, fFlags, iPciRegion);
        }
    }
    else
        rc = VERR_OUT_OF_RESOURCES;

    LogFlow(("pdmR3DevHlp_MmioCreateEx: caller='%s'/%d: returns %Rrc (*phRegion=%#x)\n",
             pDevIns->pReg->szName, pDevIns->iInstance, rc, *phRegion));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmioMap} */
DECL_HIDDEN_CALLBACK(int) pdmR3DevHlpTracing_MmioMap(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, RTGCPHYS GCPhys)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MmioMap: caller='%s'/%d: hRegion=%#x GCPhys=%#RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion, GCPhys));

    PVM    const pVM   = pDevIns->Internal.s.pVMR3;
    PVMCPU const pVCpu = VMMGetCpu(pVM);
    AssertReturn(pVCpu, VERR_VM_THREAD_NOT_EMT);

    int rc = IOMR3MmioMap(pVM, pVCpu, pDevIns, hRegion, GCPhys);
    DBGFTracerEvtMmioMap(pVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, hRegion, GCPhys);

    LogFlow(("pdmR3DevHlp_MmioMap: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnMmioUnmap} */
DECL_HIDDEN_CALLBACK(int) pdmR3DevHlpTracing_MmioUnmap(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_MmioUnmap: caller='%s'/%d: hRegion=%#x\n", pDevIns->pReg->szName, pDevIns->iInstance, hRegion));

    PVM    const pVM   = pDevIns->Internal.s.pVMR3;
    PVMCPU const pVCpu = VMMGetCpu(pVM);
    AssertReturn(pVCpu, VERR_VM_THREAD_NOT_EMT);

    int rc = IOMR3MmioUnmap(pVM, pVCpu, pDevIns, hRegion);
    DBGFTracerEvtMmioUnmap(pVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, hRegion);

    LogFlow(("pdmR3DevHlp_MmioUnmap: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysRead} */
DECL_HIDDEN_CALLBACK(int)
pdmR3DevHlpTracing_PhysRead(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead, uint32_t fFlags)
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

    if (!(fFlags & PDM_DEVHLP_PHYS_RW_F_DATA_USER))
        DBGFTracerEvtGCPhysRead(pVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, GCPhys, pvBuf, cbRead);

    Log(("pdmR3DevHlp_PhysRead: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict) ));
    return VBOXSTRICTRC_VAL(rcStrict);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPhysWrite} */
DECL_HIDDEN_CALLBACK(int)
pdmR3DevHlpTracing_PhysWrite(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite, uint32_t fFlags)
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

    if (!(fFlags & PDM_DEVHLP_PHYS_RW_F_DATA_USER))
        DBGFTracerEvtGCPhysWrite(pVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, GCPhys, pvBuf, cbWrite);

    Log(("pdmR3DevHlp_PhysWrite: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict) ));
    return VBOXSTRICTRC_VAL(rcStrict);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnPCIPhysRead} */
DECL_HIDDEN_CALLBACK(int)
pdmR3DevHlpTracing_PCIPhysRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead, uint32_t fFlags)
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
        LogFunc(("caller='%s'/%d: returns %Rrc - Not bus master! GCPhys=%RGp cbRead=%#zx\n", pDevIns->pReg->szName,
                 pDevIns->iInstance, VERR_PDM_NOT_PCI_BUS_MASTER, GCPhys, cbRead));
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
DECL_HIDDEN_CALLBACK(int)
pdmR3DevHlpTracing_PCIPhysWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite, uint32_t fFlags)
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


/** @interface_method_impl{PDMDEVHLPR3,pfnPCISetIrq} */
DECL_HIDDEN_CALLBACK(void) pdmR3DevHlpTracing_PCISetIrq(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq, int iLevel)
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

    DBGFTracerEvtIrq(pVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, iIrq, iLevel);

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
DECL_HIDDEN_CALLBACK(void) pdmR3DevHlpTracing_PCISetIrqNoWait(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq, int iLevel)
{
    pdmR3DevHlpTracing_PCISetIrq(pDevIns, pPciDev, iIrq, iLevel);
}


/** @interface_method_impl{PDMDEVHLPR3,pfnISASetIrq} */
DECL_HIDDEN_CALLBACK(void) pdmR3DevHlpTracing_ISASetIrq(PPDMDEVINS pDevIns, int iIrq, int iLevel)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3DevHlp_ISASetIrq: caller='%s'/%d: iIrq=%d iLevel=%d\n", pDevIns->pReg->szName, pDevIns->iInstance, iIrq, iLevel));

    /*
     * Validate input.
     */
    Assert(iIrq < 16);
    Assert((uint32_t)iLevel <= PDM_IRQ_LEVEL_FLIP_FLOP);

    PVM pVM = pDevIns->Internal.s.pVMR3;

    DBGFTracerEvtIrq(pVM, pDevIns->Internal.s.hDbgfTraceEvtSrc, iIrq, iLevel);

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
DECL_HIDDEN_CALLBACK(void) pdmR3DevHlpTracing_ISASetIrqNoWait(PPDMDEVINS pDevIns, int iIrq, int iLevel)
{
    pdmR3DevHlpTracing_ISASetIrq(pDevIns, iIrq, iLevel);
}


/** @} */

