/* $Id: PDMR0DevHlp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM - Pluggable Device and Driver Manager, R0 Device Helper parts.
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
#include <VBox/vmm/apic.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/gvm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/gvmm.h>

#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/sup.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/string.h>

#include "dtrace/VBoxVMM.h"
#include "PDMInline.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
extern DECLEXPORT(const PDMDEVHLPR0)    g_pdmR0DevHlp;
extern DECLEXPORT(const PDMDEVHLPR0)    g_pdmR0DevHlpTracing;
extern DECLEXPORT(const PDMPICHLP)      g_pdmR0PicHlp;
extern DECLEXPORT(const PDMIOAPICHLP)   g_pdmR0IoApicHlp;
extern DECLEXPORT(const PDMPCIHLPR0)    g_pdmR0PciHlp;
extern DECLEXPORT(const PDMIOMMUHLPR0)  g_pdmR0IommuHlp;
extern DECLEXPORT(const PDMHPETHLPR0)   g_pdmR0HpetHlp;
extern DECLEXPORT(const PDMPCIRAWHLPR0) g_pdmR0PciRawHlp;
RT_C_DECLS_END


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/** @name Ring-0 Device Helpers
 * @{
 */

/** @interface_method_impl{PDMDEVHLPR0,pfnIoPortSetUpContextEx} */
static DECLCALLBACK(int) pdmR0DevHlp_IoPortSetUpContextEx(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts,
                                                          PFNIOMIOPORTNEWOUT pfnOut, PFNIOMIOPORTNEWIN pfnIn,
                                                          PFNIOMIOPORTNEWOUTSTRING pfnOutStr, PFNIOMIOPORTNEWINSTRING pfnInStr,
                                                          void *pvUser)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_IoPortSetUpContextEx: caller='%s'/%d: hIoPorts=%#x pfnOut=%p pfnIn=%p pfnOutStr=%p pfnInStr=%p pvUser=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hIoPorts, pfnOut, pfnIn, pfnOutStr, pfnInStr, pvUser));
    PGVM pGVM = pDevIns->Internal.s.pGVM;
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);

    int rc = IOMR0IoPortSetUpContext(pGVM, pDevIns, hIoPorts, pfnOut, pfnIn, pfnOutStr, pfnInStr, pvUser);

    LogFlow(("pdmR0DevHlp_IoPortSetUpContextEx: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnMmioSetUpContextEx} */
static DECLCALLBACK(int) pdmR0DevHlp_MmioSetUpContextEx(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, PFNIOMMMIONEWWRITE pfnWrite,
                                                        PFNIOMMMIONEWREAD pfnRead, PFNIOMMMIONEWFILL pfnFill, void *pvUser)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_MmioSetUpContextEx: caller='%s'/%d: hRegion=%#x pfnWrite=%p pfnRead=%p pfnFill=%p pvUser=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hRegion, pfnWrite, pfnRead, pfnFill, pvUser));
    PGVM pGVM = pDevIns->Internal.s.pGVM;
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);

    int rc = IOMR0MmioSetUpContext(pGVM, pDevIns, hRegion, pfnWrite, pfnRead, pfnFill, pvUser);

    LogFlow(("pdmR0DevHlp_MmioSetUpContextEx: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnMmio2SetUpContext} */
static DECLCALLBACK(int) pdmR0DevHlp_Mmio2SetUpContext(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion,
                                                       size_t offSub, size_t cbSub, void **ppvMapping)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_Mmio2SetUpContext: caller='%s'/%d: hRegion=%#x offSub=%#zx cbSub=%#zx ppvMapping=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hRegion, offSub, cbSub, ppvMapping));
    *ppvMapping = NULL;

    PGVM pGVM = pDevIns->Internal.s.pGVM;
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);

    int rc = PGMR0PhysMMIO2MapKernel(pGVM, pDevIns, hRegion, offSub, cbSub, ppvMapping);

    LogFlow(("pdmR0DevHlp_Mmio2SetUpContext: caller='%s'/%d: returns %Rrc (%p)\n", pDevIns->pReg->szName, pDevIns->iInstance, rc, *ppvMapping));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnPCIPhysRead} */
static DECLCALLBACK(int) pdmR0DevHlp_PCIPhysRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys,
                                                 void *pvBuf, size_t cbRead, uint32_t fFlags)
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
        LogFunc(("caller=%p/%d: returns %Rrc - Not bus master! GCPhys=%RGp cbRead=%#zx\n", pDevIns, pDevIns->iInstance,
                 VERR_PDM_NOT_PCI_BUS_MASTER, GCPhys, cbRead));
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
static DECLCALLBACK(int) pdmR0DevHlp_PCIPhysWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys,
                                                  const void *pvBuf, size_t cbWrite, uint32_t fFlags)
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
static DECLCALLBACK(void) pdmR0DevHlp_PCISetIrq(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq, int iLevel)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    if (!pPciDev) /* NULL is an alias for the default PCI device. */
        pPciDev = pDevIns->apPciDevs[0];
    AssertReturnVoid(pPciDev);
    LogFlow(("pdmR0DevHlp_PCISetIrq: caller=%p/%d: pPciDev=%p:{%#x} iIrq=%d iLevel=%d\n",
             pDevIns, pDevIns->iInstance, pPciDev, pPciDev->uDevFn, iIrq, iLevel));
    PDMPCIDEV_ASSERT_VALID_AND_REGISTERED(pDevIns, pPciDev);

    PGVM         pGVM      = pDevIns->Internal.s.pGVM;
    size_t const idxBus    = pPciDev->Int.s.idxPdmBus;
    AssertReturnVoid(idxBus < RT_ELEMENTS(pGVM->pdmr0.s.aPciBuses));
    PPDMPCIBUSR0 pPciBusR0 = &pGVM->pdmr0.s.aPciBuses[idxBus];

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

    LogFlow(("pdmR0DevHlp_PCISetIrq: caller=%p/%d: returns void; uTagSrc=%#x\n", pDevIns, pDevIns->iInstance, uTagSrc));
}


/** @interface_method_impl{PDMDEVHLPR0,pfnISASetIrq} */
static DECLCALLBACK(void) pdmR0DevHlp_ISASetIrq(PPDMDEVINS pDevIns, int iIrq, int iLevel)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_ISASetIrq: caller=%p/%d: iIrq=%d iLevel=%d\n", pDevIns, pDevIns->iInstance, iIrq, iLevel));
    PGVM pGVM = pDevIns->Internal.s.pGVM;

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
    LogFlow(("pdmR0DevHlp_ISASetIrq: caller=%p/%d: returns void; uTagSrc=%#x\n", pDevIns, pDevIns->iInstance, uTagSrc));
}


/** @interface_method_impl{PDMDEVHLPR0,pfnPhysRead} */
static DECLCALLBACK(int) pdmR0DevHlp_PhysRead(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead, uint32_t fFlags)
{
    RT_NOREF(fFlags);

    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_PhysRead: caller=%p/%d: GCPhys=%RGp pvBuf=%p cbRead=%#x\n",
             pDevIns, pDevIns->iInstance, GCPhys, pvBuf, cbRead));

    VBOXSTRICTRC rcStrict = PGMPhysRead(pDevIns->Internal.s.pGVM, GCPhys, pvBuf, cbRead, PGMACCESSORIGIN_DEVICE);
    AssertMsg(rcStrict == VINF_SUCCESS, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict))); /** @todo track down the users for this bugger. */

    Log(("pdmR0DevHlp_PhysRead: caller=%p/%d: returns %Rrc\n", pDevIns, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict) ));
    return VBOXSTRICTRC_VAL(rcStrict);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnPhysWrite} */
static DECLCALLBACK(int) pdmR0DevHlp_PhysWrite(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite, uint32_t fFlags)
{
    RT_NOREF(fFlags);

    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_PhysWrite: caller=%p/%d: GCPhys=%RGp pvBuf=%p cbWrite=%#x\n",
             pDevIns, pDevIns->iInstance, GCPhys, pvBuf, cbWrite));

    VBOXSTRICTRC rcStrict = PGMPhysWrite(pDevIns->Internal.s.pGVM, GCPhys, pvBuf, cbWrite, PGMACCESSORIGIN_DEVICE);
    AssertMsg(rcStrict == VINF_SUCCESS, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict))); /** @todo track down the users for this bugger. */

    Log(("pdmR0DevHlp_PhysWrite: caller=%p/%d: returns %Rrc\n", pDevIns, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict) ));
    return VBOXSTRICTRC_VAL(rcStrict);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnA20IsEnabled} */
static DECLCALLBACK(bool) pdmR0DevHlp_A20IsEnabled(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_A20IsEnabled: caller=%p/%d:\n", pDevIns, pDevIns->iInstance));

    bool fEnabled = PGMPhysIsA20Enabled(VMMGetCpu(pDevIns->Internal.s.pGVM));

    Log(("pdmR0DevHlp_A20IsEnabled: caller=%p/%d: returns %RTbool\n", pDevIns, pDevIns->iInstance, fEnabled));
    return fEnabled;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnVMState} */
static DECLCALLBACK(VMSTATE) pdmR0DevHlp_VMState(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    VMSTATE enmVMState = pDevIns->Internal.s.pGVM->enmVMState;

    LogFlow(("pdmR0DevHlp_VMState: caller=%p/%d: returns %d\n", pDevIns, pDevIns->iInstance, enmVMState));
    return enmVMState;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnGetVM} */
static DECLCALLBACK(PVMCC)  pdmR0DevHlp_GetVM(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_GetVM: caller='%p'/%d\n", pDevIns, pDevIns->iInstance));
    return pDevIns->Internal.s.pGVM;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnGetVMCPU} */
static DECLCALLBACK(PVMCPUCC) pdmR0DevHlp_GetVMCPU(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_GetVMCPU: caller='%p'/%d\n", pDevIns, pDevIns->iInstance));
    return VMMGetCpu(pDevIns->Internal.s.pGVM);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnGetCurrentCpuId} */
static DECLCALLBACK(VMCPUID) pdmR0DevHlp_GetCurrentCpuId(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    VMCPUID idCpu = VMMGetCpuId(pDevIns->Internal.s.pGVM);
    LogFlow(("pdmR0DevHlp_GetCurrentCpuId: caller='%p'/%d for CPU %u\n", pDevIns, pDevIns->iInstance, idCpu));
    return idCpu;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnGetMainExecutionEngine} */
static DECLCALLBACK(uint8_t) pdmR0DevHlp_GetMainExecutionEngine(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_GetMainExecutionEngine: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return pDevIns->Internal.s.pGVM->bMainExecutionEngine;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerFromMicro} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TimerFromMicro(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMicroSecs)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerFromMicro(pDevIns->Internal.s.pGVM, hTimer, cMicroSecs);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerFromMilli} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TimerFromMilli(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMilliSecs)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerFromMilli(pDevIns->Internal.s.pGVM, hTimer, cMilliSecs);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerFromNano} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TimerFromNano(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cNanoSecs)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerFromNano(pDevIns->Internal.s.pGVM, hTimer, cNanoSecs);
}

/** @interface_method_impl{PDMDEVHLPR0,pfnTimerGet} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TimerGet(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerGet(pDevIns->Internal.s.pGVM, hTimer);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerGetFreq} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TimerGetFreq(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerGetFreq(pDevIns->Internal.s.pGVM, hTimer);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerGetNano} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TimerGetNano(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerGetNano(pDevIns->Internal.s.pGVM, hTimer);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerIsActive} */
static DECLCALLBACK(bool) pdmR0DevHlp_TimerIsActive(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerIsActive(pDevIns->Internal.s.pGVM, hTimer);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerIsLockOwner} */
static DECLCALLBACK(bool) pdmR0DevHlp_TimerIsLockOwner(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerIsLockOwner(pDevIns->Internal.s.pGVM, hTimer);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerLockClock} */
static DECLCALLBACK(VBOXSTRICTRC) pdmR0DevHlp_TimerLockClock(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerLock(pDevIns->Internal.s.pGVM, hTimer, rcBusy);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerLockClock2} */
static DECLCALLBACK(VBOXSTRICTRC) pdmR0DevHlp_TimerLockClock2(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer,
                                                              PPDMCRITSECT pCritSect, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PGVM const pGVM = pDevIns->Internal.s.pGVM;
    VBOXSTRICTRC rc = TMTimerLock(pGVM, hTimer, rcBusy);
    if (rc == VINF_SUCCESS)
    {
        rc = PDMCritSectEnter(pGVM, pCritSect, rcBusy);
        if (rc == VINF_SUCCESS)
            return rc;
        AssertRC(VBOXSTRICTRC_VAL(rc));
        TMTimerUnlock(pGVM, hTimer);
    }
    else
        AssertRC(VBOXSTRICTRC_VAL(rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerSet} */
static DECLCALLBACK(int) pdmR0DevHlp_TimerSet(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t uExpire)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerSet(pDevIns->Internal.s.pGVM, hTimer, uExpire);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerSetFrequencyHint} */
static DECLCALLBACK(int) pdmR0DevHlp_TimerSetFrequencyHint(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint32_t uHz)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerSetFrequencyHint(pDevIns->Internal.s.pGVM, hTimer, uHz);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerSetMicro} */
static DECLCALLBACK(int) pdmR0DevHlp_TimerSetMicro(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMicrosToNext)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerSetMicro(pDevIns->Internal.s.pGVM, hTimer, cMicrosToNext);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerSetMillies} */
static DECLCALLBACK(int) pdmR0DevHlp_TimerSetMillies(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMilliesToNext)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerSetMillies(pDevIns->Internal.s.pGVM, hTimer, cMilliesToNext);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerSetNano} */
static DECLCALLBACK(int) pdmR0DevHlp_TimerSetNano(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cNanosToNext)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerSetNano(pDevIns->Internal.s.pGVM, hTimer, cNanosToNext);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerSetRelative} */
static DECLCALLBACK(int) pdmR0DevHlp_TimerSetRelative(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cTicksToNext, uint64_t *pu64Now)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerSetRelative(pDevIns->Internal.s.pGVM, hTimer, cTicksToNext, pu64Now);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerStop} */
static DECLCALLBACK(int) pdmR0DevHlp_TimerStop(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return TMTimerStop(pDevIns->Internal.s.pGVM, hTimer);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerUnlockClock} */
static DECLCALLBACK(void) pdmR0DevHlp_TimerUnlockClock(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    TMTimerUnlock(pDevIns->Internal.s.pGVM, hTimer);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerUnlockClock2} */
static DECLCALLBACK(void) pdmR0DevHlp_TimerUnlockClock2(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PGVM const pGVM = pDevIns->Internal.s.pGVM;
    TMTimerUnlock(pGVM, hTimer);
    int rc = PDMCritSectLeave(pGVM, pCritSect);
    AssertRC(rc);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTMTimeVirtGet} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TMTimeVirtGet(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_TMTimeVirtGet: caller='%p'/%d\n", pDevIns, pDevIns->iInstance));
    return TMVirtualGet(pDevIns->Internal.s.pGVM);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTMTimeVirtGetFreq} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TMTimeVirtGetFreq(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_TMTimeVirtGetFreq: caller='%p'/%d\n", pDevIns, pDevIns->iInstance));
    return TMVirtualGetFreq(pDevIns->Internal.s.pGVM);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTMTimeVirtGetNano} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TMTimeVirtGetNano(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_TMTimeVirtGetNano: caller='%p'/%d\n", pDevIns, pDevIns->iInstance));
    return TMVirtualToNano(pDevIns->Internal.s.pGVM, TMVirtualGet(pDevIns->Internal.s.pGVM));
}


/** @interface_method_impl{PDMDEVHLPR0,pfnQueueAlloc} */
static DECLCALLBACK(PPDMQUEUEITEMCORE) pdmR0DevHlp_QueueAlloc(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMQueueAlloc(pDevIns->Internal.s.pGVM, hQueue, pDevIns);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnQueueInsert} */
static DECLCALLBACK(int) pdmR0DevHlp_QueueInsert(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue, PPDMQUEUEITEMCORE pItem)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMQueueInsert(pDevIns->Internal.s.pGVM, hQueue, pDevIns, pItem);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnQueueFlushIfNecessary} */
static DECLCALLBACK(bool) pdmR0DevHlp_QueueFlushIfNecessary(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMQueueFlushIfNecessary(pDevIns->Internal.s.pGVM, hQueue, pDevIns) == VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTaskTrigger} */
static DECLCALLBACK(int) pdmR0DevHlp_TaskTrigger(PPDMDEVINS pDevIns, PDMTASKHANDLE hTask)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_TaskTrigger: caller='%s'/%d: hTask=%RU64\n", pDevIns->pReg->szName, pDevIns->iInstance, hTask));

    int rc = PDMTaskTrigger(pDevIns->Internal.s.pGVM, PDMTASKTYPE_DEV, pDevIns->pDevInsForR3, hTask);

    LogFlow(("pdmR0DevHlp_TaskTrigger: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventSignal} */
static DECLCALLBACK(int) pdmR0DevHlp_SUPSemEventSignal(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventSignal: caller='%s'/%d: hEvent=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, hEvent));

    int rc = SUPSemEventSignal(pDevIns->Internal.s.pGVM->pSession, hEvent);

    LogFlow(("pdmR0DevHlp_SUPSemEventSignal: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventWaitNoResume} */
static DECLCALLBACK(int) pdmR0DevHlp_SUPSemEventWaitNoResume(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint32_t cMillies)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventWaitNoResume: caller='%s'/%d: hEvent=%p cNsTimeout=%RU32\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEvent, cMillies));

    int rc = SUPSemEventWaitNoResume(pDevIns->Internal.s.pGVM->pSession, hEvent, cMillies);

    LogFlow(("pdmR0DevHlp_SUPSemEventWaitNoResume: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventWaitNsAbsIntr} */
static DECLCALLBACK(int) pdmR0DevHlp_SUPSemEventWaitNsAbsIntr(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint64_t uNsTimeout)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventWaitNsAbsIntr: caller='%s'/%d: hEvent=%p uNsTimeout=%RU64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEvent, uNsTimeout));

    int rc = SUPSemEventWaitNsAbsIntr(pDevIns->Internal.s.pGVM->pSession, hEvent, uNsTimeout);

    LogFlow(("pdmR0DevHlp_SUPSemEventWaitNsAbsIntr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventWaitNsRelIntr} */
static DECLCALLBACK(int) pdmR0DevHlp_SUPSemEventWaitNsRelIntr(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint64_t cNsTimeout)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventWaitNsRelIntr: caller='%s'/%d: hEvent=%p cNsTimeout=%RU64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEvent, cNsTimeout));

    int rc = SUPSemEventWaitNsRelIntr(pDevIns->Internal.s.pGVM->pSession, hEvent, cNsTimeout);

    LogFlow(("pdmR0DevHlp_SUPSemEventWaitNsRelIntr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventGetResolution} */
static DECLCALLBACK(uint32_t) pdmR0DevHlp_SUPSemEventGetResolution(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventGetResolution: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    uint32_t cNsResolution = SUPSemEventGetResolution(pDevIns->Internal.s.pGVM->pSession);

    LogFlow(("pdmR0DevHlp_SUPSemEventGetResolution: caller='%s'/%d: returns %u\n", pDevIns->pReg->szName, pDevIns->iInstance, cNsResolution));
    return cNsResolution;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventMultiSignal} */
static DECLCALLBACK(int) pdmR0DevHlp_SUPSemEventMultiSignal(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventMultiSignal: caller='%s'/%d: hEventMulti=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, hEventMulti));

    int rc = SUPSemEventMultiSignal(pDevIns->Internal.s.pGVM->pSession, hEventMulti);

    LogFlow(("pdmR0DevHlp_SUPSemEventMultiSignal: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventMultiReset} */
static DECLCALLBACK(int) pdmR0DevHlp_SUPSemEventMultiReset(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventMultiReset: caller='%s'/%d: hEventMulti=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, hEventMulti));

    int rc = SUPSemEventMultiReset(pDevIns->Internal.s.pGVM->pSession, hEventMulti);

    LogFlow(("pdmR0DevHlp_SUPSemEventMultiReset: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventMultiWaitNoResume} */
static DECLCALLBACK(int) pdmR0DevHlp_SUPSemEventMultiWaitNoResume(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti,
                                                                  uint32_t cMillies)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventMultiWaitNoResume: caller='%s'/%d: hEventMulti=%p cMillies=%RU32\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEventMulti, cMillies));

    int rc = SUPSemEventMultiWaitNoResume(pDevIns->Internal.s.pGVM->pSession, hEventMulti, cMillies);

    LogFlow(("pdmR0DevHlp_SUPSemEventMultiWaitNoResume: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventMultiWaitNsAbsIntr} */
static DECLCALLBACK(int) pdmR0DevHlp_SUPSemEventMultiWaitNsAbsIntr(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti,
                                                                   uint64_t uNsTimeout)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventMultiWaitNsAbsIntr: caller='%s'/%d: hEventMulti=%p uNsTimeout=%RU64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEventMulti, uNsTimeout));

    int rc = SUPSemEventMultiWaitNsAbsIntr(pDevIns->Internal.s.pGVM->pSession, hEventMulti, uNsTimeout);

    LogFlow(("pdmR0DevHlp_SUPSemEventMultiWaitNsAbsIntr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventMultiWaitNsRelIntr} */
static DECLCALLBACK(int) pdmR0DevHlp_SUPSemEventMultiWaitNsRelIntr(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti,
                                                                   uint64_t cNsTimeout)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventMultiWaitNsRelIntr: caller='%s'/%d: hEventMulti=%p cNsTimeout=%RU64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEventMulti, cNsTimeout));

    int rc = SUPSemEventMultiWaitNsRelIntr(pDevIns->Internal.s.pGVM->pSession, hEventMulti, cNsTimeout);

    LogFlow(("pdmR0DevHlp_SUPSemEventMultiWaitNsRelIntr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventMultiGetResolution} */
static DECLCALLBACK(uint32_t) pdmR0DevHlp_SUPSemEventMultiGetResolution(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventMultiGetResolution: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    uint32_t cNsResolution = SUPSemEventMultiGetResolution(pDevIns->Internal.s.pGVM->pSession);

    LogFlow(("pdmR0DevHlp_SUPSemEventMultiGetResolution: caller='%s'/%d: returns %u\n", pDevIns->pReg->szName, pDevIns->iInstance, cNsResolution));
    return cNsResolution;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectGetNop} */
static DECLCALLBACK(PPDMCRITSECT) pdmR0DevHlp_CritSectGetNop(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PGVM pGVM = pDevIns->Internal.s.pGVM;

    PPDMCRITSECT pCritSect = &pGVM->pdm.s.NopCritSect;
    LogFlow(("pdmR0DevHlp_CritSectGetNop: caller='%s'/%d: return %p\n", pDevIns->pReg->szName, pDevIns->iInstance, pCritSect));
    return pCritSect;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSetDeviceCritSect} */
static DECLCALLBACK(int) pdmR0DevHlp_SetDeviceCritSect(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    /*
     * Validate input.
     *
     * Note! We only allow the automatically created default critical section
     *       to be replaced by this API.
     */
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertPtrReturn(pCritSect, VERR_INVALID_POINTER);
    LogFlow(("pdmR0DevHlp_SetDeviceCritSect: caller='%s'/%d: pCritSect=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pCritSect));
    AssertReturn(PDMCritSectIsInitialized(pCritSect), VERR_INVALID_PARAMETER);
    PGVM pGVM = pDevIns->Internal.s.pGVM;

    VM_ASSERT_EMT(pGVM);
    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_WRONG_ORDER);

    /*
     * Check that ring-3 has already done this, then effect the change.
     */
    AssertReturn(pDevIns->pDevInsForR3R0->Internal.s.fIntFlags & PDMDEVINSINT_FLAGS_CHANGED_CRITSECT, VERR_WRONG_ORDER);
    pDevIns->pCritSectRoR0 = pCritSect;

    LogFlow(("pdmR0DevHlp_SetDeviceCritSect: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectEnter} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectEnter(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectEnter(pDevIns->Internal.s.pGVM, pCritSect, rcBusy);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectEnterDebug} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectEnterDebug(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, int rcBusy, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectEnterDebug(pDevIns->Internal.s.pGVM, pCritSect, rcBusy, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectTryEnter} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectTryEnter(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectTryEnter(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectTryEnterDebug} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectTryEnterDebug(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectTryEnterDebug(pDevIns->Internal.s.pGVM, pCritSect, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectLeave} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectLeave(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectLeave(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectIsOwner} */
static DECLCALLBACK(bool)     pdmR0DevHlp_CritSectIsOwner(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectIsOwner(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectIsInitialized} */
static DECLCALLBACK(bool)     pdmR0DevHlp_CritSectIsInitialized(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    return PDMCritSectIsInitialized(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectHasWaiters} */
static DECLCALLBACK(bool)     pdmR0DevHlp_CritSectHasWaiters(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectHasWaiters(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectGetRecursion} */
static DECLCALLBACK(uint32_t) pdmR0DevHlp_CritSectGetRecursion(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    return PDMCritSectGetRecursion(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectScheduleExitEvent} */
static DECLCALLBACK(int) pdmR0DevHlp_CritSectScheduleExitEvent(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect,
                                                               SUPSEMEVENT hEventToSignal)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    return PDMHCCritSectScheduleExitEvent(pCritSect, hEventToSignal);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwEnterShared} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwEnterShared(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwEnterShared(pDevIns->Internal.s.pGVM, pCritSect, rcBusy);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwEnterSharedDebug} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwEnterSharedDebug(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, int rcBusy,
                                                                     RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwEnterSharedDebug(pDevIns->Internal.s.pGVM, pCritSect, rcBusy, uId, RT_SRC_POS_ARGS);
}



/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwTryEnterShared} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwTryEnterShared(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwTryEnterShared(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwTryEnterSharedDebug} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwTryEnterSharedDebug(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect,
                                                                        RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwTryEnterSharedDebug(pDevIns->Internal.s.pGVM, pCritSect, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwLeaveShared} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwLeaveShared(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwLeaveShared(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwEnterExcl} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwEnterExcl(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwEnterExcl(pDevIns->Internal.s.pGVM, pCritSect, rcBusy);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwEnterExclDebug} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwEnterExclDebug(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, int rcBusy,
                                                                   RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwEnterExclDebug(pDevIns->Internal.s.pGVM, pCritSect, rcBusy, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwTryEnterExcl} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwTryEnterExcl(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwTryEnterExcl(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwTryEnterExclDebug} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwTryEnterExclDebug(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect,
                                                                      RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwTryEnterExclDebug(pDevIns->Internal.s.pGVM, pCritSect, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwLeaveExcl} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwLeaveExcl(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwLeaveExcl(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwIsWriteOwner} */
static DECLCALLBACK(bool)     pdmR0DevHlp_CritSectRwIsWriteOwner(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwIsWriteOwner(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwIsReadOwner} */
static DECLCALLBACK(bool)     pdmR0DevHlp_CritSectRwIsReadOwner(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, bool fWannaHear)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return PDMCritSectRwIsReadOwner(pDevIns->Internal.s.pGVM, pCritSect, fWannaHear);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwGetWriteRecursion} */
static DECLCALLBACK(uint32_t) pdmR0DevHlp_CritSectRwGetWriteRecursion(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    return PDMCritSectRwGetWriteRecursion(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwGetWriterReadRecursion} */
static DECLCALLBACK(uint32_t) pdmR0DevHlp_CritSectRwGetWriterReadRecursion(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    return PDMCritSectRwGetWriterReadRecursion(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwGetReadCount} */
static DECLCALLBACK(uint32_t) pdmR0DevHlp_CritSectRwGetReadCount(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    return PDMCritSectRwGetReadCount(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwIsInitialized} */
static DECLCALLBACK(bool)     pdmR0DevHlp_CritSectRwIsInitialized(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    return PDMCritSectRwIsInitialized(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnDBGFTraceBuf} */
static DECLCALLBACK(RTTRACEBUF) pdmR0DevHlp_DBGFTraceBuf(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RTTRACEBUF hTraceBuf = pDevIns->Internal.s.pGVM->hTraceBufR0;
    LogFlow(("pdmR0DevHlp_DBGFTraceBuf: caller='%p'/%d: returns %p\n", pDevIns, pDevIns->iInstance, hTraceBuf));
    return hTraceBuf;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnPCIBusSetUpContext} */
static DECLCALLBACK(int) pdmR0DevHlp_PCIBusSetUpContext(PPDMDEVINS pDevIns, PPDMPCIBUSREGR0 pPciBusReg, PCPDMPCIHLPR0 *ppPciHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_PCIBusSetUpContext: caller='%p'/%d: pPciBusReg=%p{.u32Version=%#x, .iBus=%#u, .pfnSetIrq=%p, u32EnvVersion=%#x} ppPciHlp=%p\n",
             pDevIns, pDevIns->iInstance, pPciBusReg, pPciBusReg->u32Version, pPciBusReg->iBus, pPciBusReg->pfnSetIrq,
             pPciBusReg->u32EndVersion, ppPciHlp));
    PGVM pGVM = pDevIns->Internal.s.pGVM;

    /*
     * Validate input.
     */
    AssertPtrReturn(pPciBusReg, VERR_INVALID_POINTER);
    AssertLogRelMsgReturn(pPciBusReg->u32Version == PDM_PCIBUSREGCC_VERSION,
                          ("%#x vs %#x\n", pPciBusReg->u32Version, PDM_PCIBUSREGCC_VERSION), VERR_VERSION_MISMATCH);
    AssertPtrReturn(pPciBusReg->pfnSetIrq, VERR_INVALID_POINTER);
    AssertLogRelMsgReturn(pPciBusReg->u32EndVersion == PDM_PCIBUSREGCC_VERSION,
                          ("%#x vs %#x\n", pPciBusReg->u32EndVersion, PDM_PCIBUSREGCC_VERSION), VERR_VERSION_MISMATCH);

    AssertPtrReturn(ppPciHlp, VERR_INVALID_POINTER);

    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_WRONG_ORDER);
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);

    /* Check the shared bus data (registered earlier from ring-3): */
    uint32_t iBus = pPciBusReg->iBus;
    ASMCompilerBarrier();
    AssertLogRelMsgReturn(iBus < RT_ELEMENTS(pGVM->pdm.s.aPciBuses), ("iBus=%#x\n", iBus), VERR_OUT_OF_RANGE);
    PPDMPCIBUS pPciBusShared = &pGVM->pdm.s.aPciBuses[iBus];
    AssertLogRelMsgReturn(pPciBusShared->iBus == iBus, ("%u vs %u\n", pPciBusShared->iBus, iBus), VERR_INVALID_PARAMETER);
    AssertLogRelMsgReturn(pPciBusShared->pDevInsR3 == pDevIns->pDevInsForR3,
                          ("%p vs %p (iBus=%u)\n", pPciBusShared->pDevInsR3, pDevIns->pDevInsForR3, iBus), VERR_NOT_OWNER);

    /* Check that the bus isn't already registered in ring-0: */
    AssertCompile(RT_ELEMENTS(pGVM->pdm.s.aPciBuses) == RT_ELEMENTS(pGVM->pdmr0.s.aPciBuses));
    PPDMPCIBUSR0 pPciBusR0 = &pGVM->pdmr0.s.aPciBuses[iBus];
    AssertLogRelMsgReturn(pPciBusR0->pDevInsR0 == NULL,
                          ("%p (caller pDevIns=%p, iBus=%u)\n", pPciBusR0->pDevInsR0, pDevIns, iBus),
                          VERR_ALREADY_EXISTS);

    /*
     * Do the registering.
     */
    pPciBusR0->iBus        = iBus;
    pPciBusR0->uPadding0   = 0xbeefbeef;
    pPciBusR0->pfnSetIrqR0 = pPciBusReg->pfnSetIrq;
    pPciBusR0->pDevInsR0   = pDevIns;

    *ppPciHlp = &g_pdmR0PciHlp;

    LogFlow(("pdmR0DevHlp_PCIBusSetUpContext: caller='%p'/%d: returns VINF_SUCCESS\n", pDevIns, pDevIns->iInstance));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnIommuSetUpContext} */
static DECLCALLBACK(int) pdmR0DevHlp_IommuSetUpContext(PPDMDEVINS pDevIns, PPDMIOMMUREGR0 pIommuReg, PCPDMIOMMUHLPR0 *ppIommuHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_IommuSetUpContext: caller='%p'/%d: pIommuReg=%p{.u32Version=%#x, u32TheEnd=%#x} ppIommuHlp=%p\n",
             pDevIns, pDevIns->iInstance, pIommuReg, pIommuReg->u32Version, pIommuReg->u32TheEnd, ppIommuHlp));
    PGVM pGVM = pDevIns->Internal.s.pGVM;

    /*
     * Validate input.
     */
    AssertPtrReturn(pIommuReg, VERR_INVALID_POINTER);
    AssertLogRelMsgReturn(pIommuReg->u32Version == PDM_IOMMUREGCC_VERSION,
                          ("%#x vs %#x\n", pIommuReg->u32Version, PDM_IOMMUREGCC_VERSION), VERR_VERSION_MISMATCH);
    AssertPtrReturn(pIommuReg->pfnMemAccess,     VERR_INVALID_POINTER);
    AssertPtrReturn(pIommuReg->pfnMemBulkAccess, VERR_INVALID_POINTER);
    AssertPtrReturn(pIommuReg->pfnMsiRemap,      VERR_INVALID_POINTER);
    AssertLogRelMsgReturn(pIommuReg->u32TheEnd == PDM_IOMMUREGCC_VERSION,
                          ("%#x vs %#x\n", pIommuReg->u32TheEnd, PDM_IOMMUREGCC_VERSION), VERR_VERSION_MISMATCH);

    AssertPtrReturn(ppIommuHlp, VERR_INVALID_POINTER);

    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_WRONG_ORDER);
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);

    /* Check the IOMMU shared data (registered earlier from ring-3). */
    uint32_t const idxIommu = pIommuReg->idxIommu;
    ASMCompilerBarrier();
    AssertLogRelMsgReturn(idxIommu < RT_ELEMENTS(pGVM->pdm.s.aIommus), ("idxIommu=%#x\n", idxIommu), VERR_OUT_OF_RANGE);
    PPDMIOMMUR3 pIommuShared = &pGVM->pdm.s.aIommus[idxIommu];
    AssertLogRelMsgReturn(pIommuShared->idxIommu == idxIommu, ("%u vs %u\n", pIommuShared->idxIommu, idxIommu), VERR_INVALID_PARAMETER);
    AssertLogRelMsgReturn(pIommuShared->pDevInsR3 == pDevIns->pDevInsForR3,
                          ("%p vs %p (idxIommu=%u)\n", pIommuShared->pDevInsR3, pDevIns->pDevInsForR3, idxIommu), VERR_NOT_OWNER);

    /* Check that the IOMMU isn't already registered in ring-0. */
    AssertCompile(RT_ELEMENTS(pGVM->pdm.s.aIommus) == RT_ELEMENTS(pGVM->pdmr0.s.aIommus));
    PPDMIOMMUR0 pIommuR0 = &pGVM->pdmr0.s.aIommus[idxIommu];
    AssertLogRelMsgReturn(pIommuR0->pDevInsR0 == NULL,
                          ("%p (caller pDevIns=%p, idxIommu=%u)\n", pIommuR0->pDevInsR0, pDevIns, idxIommu),
                          VERR_ALREADY_EXISTS);

    /*
     * Register.
     */
    pIommuR0->idxIommu         = idxIommu;
    pIommuR0->uPadding0        = 0xdeaddead;
    pIommuR0->pDevInsR0        = pDevIns;
    pIommuR0->pfnMemAccess     = pIommuReg->pfnMemAccess;
    pIommuR0->pfnMemBulkAccess = pIommuReg->pfnMemBulkAccess;
    pIommuR0->pfnMsiRemap      = pIommuReg->pfnMsiRemap;

    *ppIommuHlp = &g_pdmR0IommuHlp;

    LogFlow(("pdmR0DevHlp_IommuSetUpContext: caller='%p'/%d: returns VINF_SUCCESS\n", pDevIns, pDevIns->iInstance));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnPICSetUpContext} */
static DECLCALLBACK(int) pdmR0DevHlp_PICSetUpContext(PPDMDEVINS pDevIns, PPDMPICREG pPicReg, PCPDMPICHLP *ppPicHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_PICSetUpContext: caller='%s'/%d: pPicReg=%p:{.u32Version=%#x, .pfnSetIrq=%p, .pfnGetInterrupt=%p, .u32TheEnd=%#x } ppPicHlp=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPicReg, pPicReg->u32Version, pPicReg->pfnSetIrq, pPicReg->pfnGetInterrupt, pPicReg->u32TheEnd, ppPicHlp));
    PGVM pGVM = pDevIns->Internal.s.pGVM;

    /*
     * Validate input.
     */
    AssertMsgReturn(pPicReg->u32Version == PDM_PICREG_VERSION,
                    ("%s/%d: u32Version=%#x expected %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, pPicReg->u32Version, PDM_PICREG_VERSION),
                    VERR_VERSION_MISMATCH);
    AssertPtrReturn(pPicReg->pfnSetIrq, VERR_INVALID_POINTER);
    AssertPtrReturn(pPicReg->pfnGetInterrupt, VERR_INVALID_POINTER);
    AssertMsgReturn(pPicReg->u32TheEnd == PDM_PICREG_VERSION,
                    ("%s/%d: u32TheEnd=%#x expected %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, pPicReg->u32TheEnd, PDM_PICREG_VERSION),
                    VERR_VERSION_MISMATCH);
    AssertPtrReturn(ppPicHlp, VERR_INVALID_POINTER);

    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_WRONG_ORDER);
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);

    /* Check that it's the same device as made the ring-3 registrations: */
    AssertLogRelMsgReturn(pGVM->pdm.s.Pic.pDevInsR3 == pDevIns->pDevInsForR3,
                          ("%p vs %p\n", pGVM->pdm.s.Pic.pDevInsR3, pDevIns->pDevInsForR3), VERR_NOT_OWNER);

    /* Check that it isn't already registered in ring-0: */
    AssertLogRelMsgReturn(pGVM->pdm.s.Pic.pDevInsR0 == NULL, ("%p (caller pDevIns=%p)\n", pGVM->pdm.s.Pic.pDevInsR0, pDevIns),
                          VERR_ALREADY_EXISTS);

    /*
     * Take down the callbacks and instance.
     */
    pGVM->pdm.s.Pic.pDevInsR0 = pDevIns;
    pGVM->pdm.s.Pic.pfnSetIrqR0 = pPicReg->pfnSetIrq;
    pGVM->pdm.s.Pic.pfnGetInterruptR0 = pPicReg->pfnGetInterrupt;
    Log(("PDM: Registered PIC device '%s'/%d pDevIns=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pDevIns));

    /* set the helper pointer and return. */
    *ppPicHlp = &g_pdmR0PicHlp;
    LogFlow(("pdmR0DevHlp_PICSetUpContext: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnApicSetUpContext} */
static DECLCALLBACK(int) pdmR0DevHlp_ApicSetUpContext(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_ApicSetUpContext: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));
    PGVM pGVM = pDevIns->Internal.s.pGVM;

    /*
     * Validate input.
     */
    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_WRONG_ORDER);
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);

    /* Check that it's the same device as made the ring-3 registrations: */
    AssertLogRelMsgReturn(pGVM->pdm.s.Apic.pDevInsR3 == pDevIns->pDevInsForR3,
                          ("%p vs %p\n", pGVM->pdm.s.Apic.pDevInsR3, pDevIns->pDevInsForR3), VERR_NOT_OWNER);

    /* Check that it isn't already registered in ring-0: */
    AssertLogRelMsgReturn(pGVM->pdm.s.Apic.pDevInsR0 == NULL, ("%p (caller pDevIns=%p)\n", pGVM->pdm.s.Apic.pDevInsR0, pDevIns),
                          VERR_ALREADY_EXISTS);

    /*
     * Take down the instance.
     */
    pGVM->pdm.s.Apic.pDevInsR0 = pDevIns;
    Log(("PDM: Registered APIC device '%s'/%d pDevIns=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pDevIns));

    /* set the helper pointer and return. */
    LogFlow(("pdmR0DevHlp_ApicSetUpContext: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnIoApicSetUpContext} */
static DECLCALLBACK(int) pdmR0DevHlp_IoApicSetUpContext(PPDMDEVINS pDevIns, PPDMIOAPICREG pIoApicReg, PCPDMIOAPICHLP *ppIoApicHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_IoApicSetUpContext: caller='%s'/%d: pIoApicReg=%p:{.u32Version=%#x, .pfnSetIrq=%p, .pfnSendMsi=%p, .pfnSetEoi=%p, .u32TheEnd=%#x } ppIoApicHlp=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pIoApicReg, pIoApicReg->u32Version, pIoApicReg->pfnSetIrq, pIoApicReg->pfnSendMsi, pIoApicReg->pfnSetEoi, pIoApicReg->u32TheEnd, ppIoApicHlp));
    PGVM pGVM = pDevIns->Internal.s.pGVM;

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

    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_WRONG_ORDER);
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);

    /* Check that it's the same device as made the ring-3 registrations: */
    AssertLogRelMsgReturn(pGVM->pdm.s.IoApic.pDevInsR3 == pDevIns->pDevInsForR3,
                          ("%p vs %p\n", pGVM->pdm.s.IoApic.pDevInsR3, pDevIns->pDevInsForR3), VERR_NOT_OWNER);

    /* Check that it isn't already registered in ring-0: */
    AssertLogRelMsgReturn(pGVM->pdm.s.IoApic.pDevInsR0 == NULL, ("%p (caller pDevIns=%p)\n", pGVM->pdm.s.IoApic.pDevInsR0, pDevIns),
                          VERR_ALREADY_EXISTS);

    /*
     * Take down the callbacks and instance.
     */
    pGVM->pdm.s.IoApic.pDevInsR0    = pDevIns;
    pGVM->pdm.s.IoApic.pfnSetIrqR0  = pIoApicReg->pfnSetIrq;
    pGVM->pdm.s.IoApic.pfnSendMsiR0 = pIoApicReg->pfnSendMsi;
    pGVM->pdm.s.IoApic.pfnSetEoiR0  = pIoApicReg->pfnSetEoi;
    Log(("PDM: Registered IOAPIC device '%s'/%d pDevIns=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pDevIns));

    /* set the helper pointer and return. */
    *ppIoApicHlp = &g_pdmR0IoApicHlp;
    LogFlow(("pdmR0DevHlp_IoApicSetUpContext: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnHpetSetUpContext} */
static DECLCALLBACK(int) pdmR0DevHlp_HpetSetUpContext(PPDMDEVINS pDevIns, PPDMHPETREG pHpetReg, PCPDMHPETHLPR0 *ppHpetHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_HpetSetUpContext: caller='%s'/%d: pHpetReg=%p:{.u32Version=%#x, } ppHpetHlp=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pHpetReg, pHpetReg->u32Version, ppHpetHlp));
    PGVM pGVM = pDevIns->Internal.s.pGVM;

    /*
     * Validate input.
     */
    AssertMsgReturn(pHpetReg->u32Version == PDM_HPETREG_VERSION,
                    ("%s/%d: u32Version=%#x expected %#x\n", pDevIns->pReg->szName, pDevIns->iInstance, pHpetReg->u32Version, PDM_HPETREG_VERSION),
                    VERR_VERSION_MISMATCH);
    AssertPtrReturn(ppHpetHlp, VERR_INVALID_POINTER);

    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_WRONG_ORDER);
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);

    /* Check that it's the same device as made the ring-3 registrations: */
    AssertLogRelMsgReturn(pGVM->pdm.s.pHpet == pDevIns->pDevInsForR3, ("%p vs %p\n", pGVM->pdm.s.pHpet, pDevIns->pDevInsForR3),
                          VERR_NOT_OWNER);

    ///* Check that it isn't already registered in ring-0: */
    //AssertLogRelMsgReturn(pGVM->pdm.s.Hpet.pDevInsR0 == NULL, ("%p (caller pDevIns=%p)\n", pGVM->pdm.s.Hpet.pDevInsR0, pDevIns),
    //                      VERR_ALREADY_EXISTS);

    /*
     * Nothing to take down here at present.
     */
    Log(("PDM: Registered HPET device '%s'/%d pDevIns=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pDevIns));

    /* set the helper pointer and return. */
    *ppHpetHlp = &g_pdmR0HpetHlp;
    LogFlow(("pdmR0DevHlp_HpetSetUpContext: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnPGMHandlerPhysicalTypeSetUpContext} */
static DECLCALLBACK(int) pdmR0DevHlp_PGMHandlerPhysicalTypeSetUpContext(PPDMDEVINS pDevIns, PGMPHYSHANDLERKIND enmKind,
                                                                        PFNPGMPHYSHANDLER pfnHandler,
                                                                        PFNPGMRZPHYSPFHANDLER pfnPfHandler,
                                                                        const char *pszDesc, PGMPHYSHANDLERTYPE hType)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_PGMHandlerPhysicalTypeSetUpContext: caller='%s'/%d: enmKind=%d pfnHandler=%p pfnPfHandler=%p pszDesc=%p:{%s} hType=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, enmKind, pfnHandler, pfnPfHandler, pszDesc, pszDesc, hType));

    int rc = PGMR0HandlerPhysicalTypeSetUpContext(pDevIns->Internal.s.pGVM, enmKind, PGMPHYSHANDLER_F_R0_DEVINS_IDX,
                                                  pfnHandler, pfnPfHandler, pszDesc, hType);

    Log(("pdmR0DevHlp_PGMHandlerPhysicalTypeSetUpContext: caller='%s'/%d: returns %Rrc\n",
         pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnPGMHandlerPhysicalPageTempOff} */
static DECLCALLBACK(int) pdmR0DevHlp_PGMHandlerPhysicalPageTempOff(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, RTGCPHYS GCPhysPage)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_PGMHandlerPhysicalPageTempOff: caller='%s'/%d: GCPhys=%RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, GCPhys));

    int rc = PGMHandlerPhysicalPageTempOff(pDevIns->Internal.s.pGVM, GCPhys, GCPhysPage);

    Log(("pdmR0DevHlp_PGMHandlerPhysicalPageTempOff: caller='%s'/%d: returns %Rrc\n",
         pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnMmioMapMmio2Page} */
static DECLCALLBACK(int) pdmR0DevHlp_MmioMapMmio2Page(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, RTGCPHYS offRegion,
                                                      uint64_t hMmio2, RTGCPHYS offMmio2, uint64_t fPageFlags)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_MmioMapMmio2Page: caller='%s'/%d: hRegion=%RX64 offRegion=%RGp hMmio2=%RX64 offMmio2=%RGp fPageFlags=%RX64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hRegion, offRegion, hMmio2, offMmio2, fPageFlags));

    int rc = IOMMmioMapMmio2Page(pDevIns->Internal.s.pGVM, pDevIns, hRegion, offRegion, hMmio2, offMmio2, fPageFlags);

    Log(("pdmR0DevHlp_MmioMapMmio2Page: caller='%s'/%d: returns %Rrc\n",
         pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnMmioResetRegion} */
static DECLCALLBACK(int) pdmR0DevHlp_MmioResetRegion(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_MmioResetRegion: caller='%s'/%d: hRegion=%RX64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hRegion));

    int rc = IOMMmioResetRegion(pDevIns->Internal.s.pGVM, pDevIns, hRegion);

    Log(("pdmR0DevHlp_MmioResetRegion: caller='%s'/%d: returns %Rrc\n",
         pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnGIMGetMmio2Regions} */
static DECLCALLBACK(PGIMMMIO2REGION) pdmR0DevHlp_GIMGetMmio2Regions(PPDMDEVINS pDevIns, uint32_t *pcRegions)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    LogFlow(("pdmR0DevHlp_GIMGetMmio2Regions: caller='%s'/%d: pcRegions=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pcRegions));

    PGIMMMIO2REGION pRegion = GIMGetMmio2Regions(pDevIns->Internal.s.pGVM, pcRegions);

    LogFlow(("pdmR0DevHlp_GIMGetMmio2Regions: caller='%s'/%d: returns %p\n", pDevIns->pReg->szName, pDevIns->iInstance, pRegion));
    return pRegion;
}


/**
 * The Ring-0 Device Helper Callbacks.
 */
extern DECLEXPORT(const PDMDEVHLPR0) g_pdmR0DevHlp =
{
    PDM_DEVHLPR0_VERSION,
    pdmR0DevHlp_IoPortSetUpContextEx,
    pdmR0DevHlp_MmioSetUpContextEx,
    pdmR0DevHlp_Mmio2SetUpContext,
    pdmR0DevHlp_PCIPhysRead,
    pdmR0DevHlp_PCIPhysWrite,
    pdmR0DevHlp_PCISetIrq,
    pdmR0DevHlp_ISASetIrq,
    pdmR0DevHlp_PhysRead,
    pdmR0DevHlp_PhysWrite,
    pdmR0DevHlp_A20IsEnabled,
    pdmR0DevHlp_VMState,
    pdmR0DevHlp_GetVM,
    pdmR0DevHlp_GetVMCPU,
    pdmR0DevHlp_GetCurrentCpuId,
    pdmR0DevHlp_GetMainExecutionEngine,
    pdmR0DevHlp_TimerFromMicro,
    pdmR0DevHlp_TimerFromMilli,
    pdmR0DevHlp_TimerFromNano,
    pdmR0DevHlp_TimerGet,
    pdmR0DevHlp_TimerGetFreq,
    pdmR0DevHlp_TimerGetNano,
    pdmR0DevHlp_TimerIsActive,
    pdmR0DevHlp_TimerIsLockOwner,
    pdmR0DevHlp_TimerLockClock,
    pdmR0DevHlp_TimerLockClock2,
    pdmR0DevHlp_TimerSet,
    pdmR0DevHlp_TimerSetFrequencyHint,
    pdmR0DevHlp_TimerSetMicro,
    pdmR0DevHlp_TimerSetMillies,
    pdmR0DevHlp_TimerSetNano,
    pdmR0DevHlp_TimerSetRelative,
    pdmR0DevHlp_TimerStop,
    pdmR0DevHlp_TimerUnlockClock,
    pdmR0DevHlp_TimerUnlockClock2,
    pdmR0DevHlp_TMTimeVirtGet,
    pdmR0DevHlp_TMTimeVirtGetFreq,
    pdmR0DevHlp_TMTimeVirtGetNano,
    pdmR0DevHlp_QueueAlloc,
    pdmR0DevHlp_QueueInsert,
    pdmR0DevHlp_QueueFlushIfNecessary,
    pdmR0DevHlp_TaskTrigger,
    pdmR0DevHlp_SUPSemEventSignal,
    pdmR0DevHlp_SUPSemEventWaitNoResume,
    pdmR0DevHlp_SUPSemEventWaitNsAbsIntr,
    pdmR0DevHlp_SUPSemEventWaitNsRelIntr,
    pdmR0DevHlp_SUPSemEventGetResolution,
    pdmR0DevHlp_SUPSemEventMultiSignal,
    pdmR0DevHlp_SUPSemEventMultiReset,
    pdmR0DevHlp_SUPSemEventMultiWaitNoResume,
    pdmR0DevHlp_SUPSemEventMultiWaitNsAbsIntr,
    pdmR0DevHlp_SUPSemEventMultiWaitNsRelIntr,
    pdmR0DevHlp_SUPSemEventMultiGetResolution,
    pdmR0DevHlp_CritSectGetNop,
    pdmR0DevHlp_SetDeviceCritSect,
    pdmR0DevHlp_CritSectEnter,
    pdmR0DevHlp_CritSectEnterDebug,
    pdmR0DevHlp_CritSectTryEnter,
    pdmR0DevHlp_CritSectTryEnterDebug,
    pdmR0DevHlp_CritSectLeave,
    pdmR0DevHlp_CritSectIsOwner,
    pdmR0DevHlp_CritSectIsInitialized,
    pdmR0DevHlp_CritSectHasWaiters,
    pdmR0DevHlp_CritSectGetRecursion,
    pdmR0DevHlp_CritSectScheduleExitEvent,
    pdmR0DevHlp_CritSectRwEnterShared,
    pdmR0DevHlp_CritSectRwEnterSharedDebug,
    pdmR0DevHlp_CritSectRwTryEnterShared,
    pdmR0DevHlp_CritSectRwTryEnterSharedDebug,
    pdmR0DevHlp_CritSectRwLeaveShared,
    pdmR0DevHlp_CritSectRwEnterExcl,
    pdmR0DevHlp_CritSectRwEnterExclDebug,
    pdmR0DevHlp_CritSectRwTryEnterExcl,
    pdmR0DevHlp_CritSectRwTryEnterExclDebug,
    pdmR0DevHlp_CritSectRwLeaveExcl,
    pdmR0DevHlp_CritSectRwIsWriteOwner,
    pdmR0DevHlp_CritSectRwIsReadOwner,
    pdmR0DevHlp_CritSectRwGetWriteRecursion,
    pdmR0DevHlp_CritSectRwGetWriterReadRecursion,
    pdmR0DevHlp_CritSectRwGetReadCount,
    pdmR0DevHlp_CritSectRwIsInitialized,
    pdmR0DevHlp_DBGFTraceBuf,
    pdmR0DevHlp_PCIBusSetUpContext,
    pdmR0DevHlp_IommuSetUpContext,
    pdmR0DevHlp_PICSetUpContext,
    pdmR0DevHlp_ApicSetUpContext,
    pdmR0DevHlp_IoApicSetUpContext,
    pdmR0DevHlp_HpetSetUpContext,
    pdmR0DevHlp_PGMHandlerPhysicalTypeSetUpContext,
    pdmR0DevHlp_PGMHandlerPhysicalPageTempOff,
    pdmR0DevHlp_MmioMapMmio2Page,
    pdmR0DevHlp_MmioResetRegion,
    pdmR0DevHlp_GIMGetMmio2Regions,
    NULL /*pfnReserved1*/,
    NULL /*pfnReserved2*/,
    NULL /*pfnReserved3*/,
    NULL /*pfnReserved4*/,
    NULL /*pfnReserved5*/,
    NULL /*pfnReserved6*/,
    NULL /*pfnReserved7*/,
    NULL /*pfnReserved8*/,
    NULL /*pfnReserved9*/,
    NULL /*pfnReserved10*/,
    PDM_DEVHLPR0_VERSION
};


#ifdef VBOX_WITH_DBGF_TRACING
/**
 * The Ring-0 Device Helper Callbacks - tracing variant.
 */
extern DECLEXPORT(const PDMDEVHLPR0) g_pdmR0DevHlpTracing =
{
    PDM_DEVHLPR0_VERSION,
    pdmR0DevHlpTracing_IoPortSetUpContextEx,
    pdmR0DevHlpTracing_MmioSetUpContextEx,
    pdmR0DevHlp_Mmio2SetUpContext,
    pdmR0DevHlpTracing_PCIPhysRead,
    pdmR0DevHlpTracing_PCIPhysWrite,
    pdmR0DevHlpTracing_PCISetIrq,
    pdmR0DevHlpTracing_ISASetIrq,
    pdmR0DevHlp_PhysRead,
    pdmR0DevHlp_PhysWrite,
    pdmR0DevHlp_A20IsEnabled,
    pdmR0DevHlp_VMState,
    pdmR0DevHlp_GetVM,
    pdmR0DevHlp_GetVMCPU,
    pdmR0DevHlp_GetCurrentCpuId,
    pdmR0DevHlp_GetMainExecutionEngine,
    pdmR0DevHlp_TimerFromMicro,
    pdmR0DevHlp_TimerFromMilli,
    pdmR0DevHlp_TimerFromNano,
    pdmR0DevHlp_TimerGet,
    pdmR0DevHlp_TimerGetFreq,
    pdmR0DevHlp_TimerGetNano,
    pdmR0DevHlp_TimerIsActive,
    pdmR0DevHlp_TimerIsLockOwner,
    pdmR0DevHlp_TimerLockClock,
    pdmR0DevHlp_TimerLockClock2,
    pdmR0DevHlp_TimerSet,
    pdmR0DevHlp_TimerSetFrequencyHint,
    pdmR0DevHlp_TimerSetMicro,
    pdmR0DevHlp_TimerSetMillies,
    pdmR0DevHlp_TimerSetNano,
    pdmR0DevHlp_TimerSetRelative,
    pdmR0DevHlp_TimerStop,
    pdmR0DevHlp_TimerUnlockClock,
    pdmR0DevHlp_TimerUnlockClock2,
    pdmR0DevHlp_TMTimeVirtGet,
    pdmR0DevHlp_TMTimeVirtGetFreq,
    pdmR0DevHlp_TMTimeVirtGetNano,
    pdmR0DevHlp_QueueAlloc,
    pdmR0DevHlp_QueueInsert,
    pdmR0DevHlp_QueueFlushIfNecessary,
    pdmR0DevHlp_TaskTrigger,
    pdmR0DevHlp_SUPSemEventSignal,
    pdmR0DevHlp_SUPSemEventWaitNoResume,
    pdmR0DevHlp_SUPSemEventWaitNsAbsIntr,
    pdmR0DevHlp_SUPSemEventWaitNsRelIntr,
    pdmR0DevHlp_SUPSemEventGetResolution,
    pdmR0DevHlp_SUPSemEventMultiSignal,
    pdmR0DevHlp_SUPSemEventMultiReset,
    pdmR0DevHlp_SUPSemEventMultiWaitNoResume,
    pdmR0DevHlp_SUPSemEventMultiWaitNsAbsIntr,
    pdmR0DevHlp_SUPSemEventMultiWaitNsRelIntr,
    pdmR0DevHlp_SUPSemEventMultiGetResolution,
    pdmR0DevHlp_CritSectGetNop,
    pdmR0DevHlp_SetDeviceCritSect,
    pdmR0DevHlp_CritSectEnter,
    pdmR0DevHlp_CritSectEnterDebug,
    pdmR0DevHlp_CritSectTryEnter,
    pdmR0DevHlp_CritSectTryEnterDebug,
    pdmR0DevHlp_CritSectLeave,
    pdmR0DevHlp_CritSectIsOwner,
    pdmR0DevHlp_CritSectIsInitialized,
    pdmR0DevHlp_CritSectHasWaiters,
    pdmR0DevHlp_CritSectGetRecursion,
    pdmR0DevHlp_CritSectScheduleExitEvent,
    pdmR0DevHlp_CritSectRwEnterShared,
    pdmR0DevHlp_CritSectRwEnterSharedDebug,
    pdmR0DevHlp_CritSectRwTryEnterShared,
    pdmR0DevHlp_CritSectRwTryEnterSharedDebug,
    pdmR0DevHlp_CritSectRwLeaveShared,
    pdmR0DevHlp_CritSectRwEnterExcl,
    pdmR0DevHlp_CritSectRwEnterExclDebug,
    pdmR0DevHlp_CritSectRwTryEnterExcl,
    pdmR0DevHlp_CritSectRwTryEnterExclDebug,
    pdmR0DevHlp_CritSectRwLeaveExcl,
    pdmR0DevHlp_CritSectRwIsWriteOwner,
    pdmR0DevHlp_CritSectRwIsReadOwner,
    pdmR0DevHlp_CritSectRwGetWriteRecursion,
    pdmR0DevHlp_CritSectRwGetWriterReadRecursion,
    pdmR0DevHlp_CritSectRwGetReadCount,
    pdmR0DevHlp_CritSectRwIsInitialized,
    pdmR0DevHlp_DBGFTraceBuf,
    pdmR0DevHlp_PCIBusSetUpContext,
    pdmR0DevHlp_IommuSetUpContext,
    pdmR0DevHlp_PICSetUpContext,
    pdmR0DevHlp_ApicSetUpContext,
    pdmR0DevHlp_IoApicSetUpContext,
    pdmR0DevHlp_HpetSetUpContext,
    pdmR0DevHlp_PGMHandlerPhysicalTypeSetUpContext,
    pdmR0DevHlp_PGMHandlerPhysicalPageTempOff,
    pdmR0DevHlp_MmioMapMmio2Page,
    pdmR0DevHlp_MmioResetRegion,
    pdmR0DevHlp_GIMGetMmio2Regions,
    NULL /*pfnReserved1*/,
    NULL /*pfnReserved2*/,
    NULL /*pfnReserved3*/,
    NULL /*pfnReserved4*/,
    NULL /*pfnReserved5*/,
    NULL /*pfnReserved6*/,
    NULL /*pfnReserved7*/,
    NULL /*pfnReserved8*/,
    NULL /*pfnReserved9*/,
    NULL /*pfnReserved10*/,
    PDM_DEVHLPR0_VERSION
};
#endif


/** @} */


/** @name PIC Ring-0 Helpers
 * @{
 */

/** @interface_method_impl{PDMPICHLP,pfnSetInterruptFF} */
static DECLCALLBACK(void) pdmR0PicHlp_SetInterruptFF(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PGVM     pGVM  = (PGVM)pDevIns->Internal.s.pGVM;
    PVMCPUCC pVCpu = &pGVM->aCpus[0];     /* for PIC we always deliver to CPU 0, MP use APIC */
    /** @todo r=ramshankar: Propagating rcRZ and make all callers handle it? */
    APICLocalInterrupt(pVCpu, 0 /* u8Pin */, 1 /* u8Level */, VINF_SUCCESS /* rcRZ */);
}


/** @interface_method_impl{PDMPICHLP,pfnClearInterruptFF} */
static DECLCALLBACK(void) pdmR0PicHlp_ClearInterruptFF(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PGVM     pGVM  = (PGVM)pDevIns->Internal.s.pGVM;
    PVMCPUCC pVCpu = &pGVM->aCpus[0];     /* for PIC we always deliver to CPU 0, MP use APIC */
    /** @todo r=ramshankar: Propagating rcRZ and make all callers handle it? */
    APICLocalInterrupt(pVCpu, 0 /* u8Pin */, 0 /* u8Level */, VINF_SUCCESS /* rcRZ */);
}


/** @interface_method_impl{PDMPICHLP,pfnLock} */
static DECLCALLBACK(int) pdmR0PicHlp_Lock(PPDMDEVINS pDevIns, int rc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return pdmLockEx(pDevIns->Internal.s.pGVM, rc);
}


/** @interface_method_impl{PDMPICHLP,pfnUnlock} */
static DECLCALLBACK(void) pdmR0PicHlp_Unlock(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    pdmUnlock(pDevIns->Internal.s.pGVM);
}


/**
 * The Ring-0 PIC Helper Callbacks.
 */
extern DECLEXPORT(const PDMPICHLP) g_pdmR0PicHlp =
{
    PDM_PICHLP_VERSION,
    pdmR0PicHlp_SetInterruptFF,
    pdmR0PicHlp_ClearInterruptFF,
    pdmR0PicHlp_Lock,
    pdmR0PicHlp_Unlock,
    PDM_PICHLP_VERSION
};

/** @} */


/** @name I/O APIC Ring-0 Helpers
 * @{
 */

/** @interface_method_impl{PDMIOAPICHLP,pfnApicBusDeliver} */
static DECLCALLBACK(int) pdmR0IoApicHlp_ApicBusDeliver(PPDMDEVINS pDevIns, uint8_t u8Dest, uint8_t u8DestMode,
                                                       uint8_t u8DeliveryMode, uint8_t uVector, uint8_t u8Polarity,
                                                       uint8_t u8TriggerMode, uint32_t uTagSrc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PGVM pGVM = pDevIns->Internal.s.pGVM;
    LogFlow(("pdmR0IoApicHlp_ApicBusDeliver: caller=%p/%d: u8Dest=%RX8 u8DestMode=%RX8 u8DeliveryMode=%RX8 uVector=%RX8 u8Polarity=%RX8 u8TriggerMode=%RX8 uTagSrc=%#x\n",
             pDevIns, pDevIns->iInstance, u8Dest, u8DestMode, u8DeliveryMode, uVector, u8Polarity, u8TriggerMode, uTagSrc));
    return APICBusDeliver(pGVM, u8Dest, u8DestMode, u8DeliveryMode, uVector, u8Polarity, u8TriggerMode, uTagSrc);
}


/** @interface_method_impl{PDMIOAPICHLP,pfnLock} */
static DECLCALLBACK(int) pdmR0IoApicHlp_Lock(PPDMDEVINS pDevIns, int rc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return pdmLockEx(pDevIns->Internal.s.pGVM, rc);
}


/** @interface_method_impl{PDMIOAPICHLP,pfnUnlock} */
static DECLCALLBACK(void) pdmR0IoApicHlp_Unlock(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    pdmUnlock(pDevIns->Internal.s.pGVM);
}


/** @interface_method_impl{PDMIOAPICHLP,pfnLockIsOwner} */
static DECLCALLBACK(bool) pdmR0IoApicHlp_LockIsOwner(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return pdmLockIsOwner(pDevIns->Internal.s.pGVM);
}


/** @interface_method_impl{PDMIOAPICHLP,pfnIommuMsiRemap} */
static DECLCALLBACK(int) pdmR0IoApicHlp_IommuMsiRemap(PPDMDEVINS pDevIns, uint16_t idDevice, PCMSIMSG pMsiIn, PMSIMSG pMsiOut)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0IoApicHlp_IommuMsiRemap: caller='%s'/%d: pMsiIn=(%#RX64, %#RU32)\n", pDevIns->pReg->szName,
             pDevIns->iInstance, pMsiIn->Addr.u64, pMsiIn->Data.u32));

#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
    if (pdmIommuIsPresent(pDevIns))
    {
        PGVM pGVM = pDevIns->Internal.s.pGVM;
        PPDMIOMMUR0 pIommu = &pGVM->pdmr0.s.aIommus[0];
        if (pIommu->pDevInsR0)
            return pdmIommuMsiRemap(pDevIns, idDevice, pMsiIn, pMsiOut);
        AssertMsgFailedReturn(("Implement queueing PDM task for remapping MSI via IOMMU in ring-3"), VERR_IOMMU_IPE_0);
    }
#else
    RT_NOREF(pDevIns, idDevice);
#endif
    return VERR_IOMMU_NOT_PRESENT;
}


/**
 * The Ring-0 I/O APIC Helper Callbacks.
 */
extern DECLEXPORT(const PDMIOAPICHLP) g_pdmR0IoApicHlp =
{
    PDM_IOAPICHLP_VERSION,
    pdmR0IoApicHlp_ApicBusDeliver,
    pdmR0IoApicHlp_Lock,
    pdmR0IoApicHlp_Unlock,
    pdmR0IoApicHlp_LockIsOwner,
    pdmR0IoApicHlp_IommuMsiRemap,
    PDM_IOAPICHLP_VERSION
};

/** @} */




/** @name PCI Bus Ring-0 Helpers
 * @{
 */

/** @interface_method_impl{PDMPCIHLPR0,pfnIsaSetIrq} */
static DECLCALLBACK(void) pdmR0PciHlp_IsaSetIrq(PPDMDEVINS pDevIns, int iIrq, int iLevel, uint32_t uTagSrc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    Log4(("pdmR0PciHlp_IsaSetIrq: iIrq=%d iLevel=%d uTagSrc=%#x\n", iIrq, iLevel, uTagSrc));
    PGVM pGVM = pDevIns->Internal.s.pGVM;

    pdmLock(pGVM);
    pdmR0IsaSetIrq(pGVM, iIrq, iLevel, uTagSrc);
    pdmUnlock(pGVM);
}


/** @interface_method_impl{PDMPCIHLPR0,pfnIoApicSetIrq} */
static DECLCALLBACK(void) pdmR0PciHlp_IoApicSetIrq(PPDMDEVINS pDevIns, PCIBDF uBusDevFn, int iIrq, int iLevel, uint32_t uTagSrc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    Log4(("pdmR0PciHlp_IoApicSetIrq: uBusDevFn=%#x iIrq=%d iLevel=%d uTagSrc=%#x\n", uBusDevFn, iIrq, iLevel, uTagSrc));
    PGVM pGVM = pDevIns->Internal.s.pGVM;

    if (pGVM->pdm.s.IoApic.pDevInsR0)
        pGVM->pdm.s.IoApic.pfnSetIrqR0(pGVM->pdm.s.IoApic.pDevInsR0, uBusDevFn, iIrq, iLevel, uTagSrc);
    else if (pGVM->pdm.s.IoApic.pDevInsR3)
    {
        /* queue for ring-3 execution. */
        PPDMDEVHLPTASK pTask = (PPDMDEVHLPTASK)PDMQueueAlloc(pGVM, pGVM->pdm.s.hDevHlpQueue, pGVM);
        if (pTask)
        {
            pTask->enmOp = PDMDEVHLPTASKOP_IOAPIC_SET_IRQ;
            pTask->pDevInsR3 = NIL_RTR3PTR; /* not required */
            pTask->u.IoApicSetIrq.uBusDevFn = uBusDevFn;
            pTask->u.IoApicSetIrq.iIrq = iIrq;
            pTask->u.IoApicSetIrq.iLevel = iLevel;
            pTask->u.IoApicSetIrq.uTagSrc = uTagSrc;

            PDMQueueInsert(pGVM, pGVM->pdm.s.hDevHlpQueue, pGVM, &pTask->Core);
        }
        else
            AssertMsgFailed(("We're out of devhlp queue items!!!\n"));
    }
}


/** @interface_method_impl{PDMPCIHLPR0,pfnIoApicSendMsi} */
static DECLCALLBACK(void) pdmR0PciHlp_IoApicSendMsi(PPDMDEVINS pDevIns, PCIBDF uBusDevFn, PCMSIMSG pMsi, uint32_t uTagSrc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    Assert(PCIBDF_IS_VALID(uBusDevFn));
    Log4(("pdmR0PciHlp_IoApicSendMsi: uBusDevFn=%#x Msi=(Addr:%#RX64 Data:%#RX32) uTagSrc=%#x\n", uBusDevFn, pMsi->Addr.u64,
          pMsi->Data.u32, uTagSrc));
    PDMIoApicSendMsi(pDevIns->Internal.s.pGVM, uBusDevFn, pMsi, uTagSrc);
}


/** @interface_method_impl{PDMPCIHLPR0,pfnLock} */
static DECLCALLBACK(int) pdmR0PciHlp_Lock(PPDMDEVINS pDevIns, int rc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return pdmLockEx(pDevIns->Internal.s.pGVM, rc);
}


/** @interface_method_impl{PDMPCIHLPR0,pfnUnlock} */
static DECLCALLBACK(void) pdmR0PciHlp_Unlock(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    pdmUnlock(pDevIns->Internal.s.pGVM);
}


/** @interface_method_impl{PDMPCIHLPR0,pfnGetBusByNo} */
static DECLCALLBACK(PPDMDEVINS) pdmR0PciHlp_GetBusByNo(PPDMDEVINS pDevIns, uint32_t idxPdmBus)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PGVM pGVM = pDevIns->Internal.s.pGVM;
    AssertReturn(idxPdmBus < RT_ELEMENTS(pGVM->pdmr0.s.aPciBuses), NULL);
    PPDMDEVINS pRetDevIns = pGVM->pdmr0.s.aPciBuses[idxPdmBus].pDevInsR0;
    LogFlow(("pdmR3PciHlp_GetBusByNo: caller='%s'/%d: returns %p\n", pDevIns->pReg->szName, pDevIns->iInstance, pRetDevIns));
    return pRetDevIns;
}


/**
 * The Ring-0 PCI Bus Helper Callbacks.
 */
extern DECLEXPORT(const PDMPCIHLPR0) g_pdmR0PciHlp =
{
    PDM_PCIHLPR0_VERSION,
    pdmR0PciHlp_IsaSetIrq,
    pdmR0PciHlp_IoApicSetIrq,
    pdmR0PciHlp_IoApicSendMsi,
    pdmR0PciHlp_Lock,
    pdmR0PciHlp_Unlock,
    pdmR0PciHlp_GetBusByNo,
    PDM_PCIHLPR0_VERSION, /* the end */
};

/** @} */


/** @name IOMMU Ring-0 Helpers
 * @{
 */

/** @interface_method_impl{PDMIOMMUHLPR0,pfnLock} */
static DECLCALLBACK(int) pdmR0IommuHlp_Lock(PPDMDEVINS pDevIns, int rc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return pdmLockEx(pDevIns->Internal.s.pGVM, rc);
}


/** @interface_method_impl{PDMIOMMUHLPR0,pfnUnlock} */
static DECLCALLBACK(void) pdmR0IommuHlp_Unlock(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    pdmUnlock(pDevIns->Internal.s.pGVM);
}


/** @interface_method_impl{PDMIOMMUHLPR0,pfnLockIsOwner} */
static DECLCALLBACK(bool) pdmR0IommuHlp_LockIsOwner(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return pdmLockIsOwner(pDevIns->Internal.s.pGVM);
}


/** @interface_method_impl{PDMIOMMUHLPR0,pfnSendMsi} */
static DECLCALLBACK(void) pdmR0IommuHlp_SendMsi(PPDMDEVINS pDevIns, PCMSIMSG pMsi, uint32_t uTagSrc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PDMIoApicSendMsi(pDevIns->Internal.s.pGVM, NIL_PCIBDF, pMsi, uTagSrc);
}


/**
 * The Ring-0 IOMMU Helper Callbacks.
 */
extern DECLEXPORT(const PDMIOMMUHLPR0) g_pdmR0IommuHlp =
{
    PDM_IOMMUHLPR0_VERSION,
    pdmR0IommuHlp_Lock,
    pdmR0IommuHlp_Unlock,
    pdmR0IommuHlp_LockIsOwner,
    pdmR0IommuHlp_SendMsi,
    PDM_IOMMUHLPR0_VERSION, /* the end */
};

/** @} */


/** @name HPET Ring-0 Helpers
 * @{
 */
/* none */

/**
 * The Ring-0 HPET Helper Callbacks.
 */
extern DECLEXPORT(const PDMHPETHLPR0) g_pdmR0HpetHlp =
{
    PDM_HPETHLPR0_VERSION,
    PDM_HPETHLPR0_VERSION, /* the end */
};

/** @} */


/** @name Raw PCI Ring-0 Helpers
 * @{
 */
/* none */

/**
 * The Ring-0 PCI raw Helper Callbacks.
 */
extern DECLEXPORT(const PDMPCIRAWHLPR0) g_pdmR0PciRawHlp =
{
    PDM_PCIRAWHLPR0_VERSION,
    PDM_PCIRAWHLPR0_VERSION, /* the end */
};

/** @} */




/**
 * Sets an irq on the PIC and I/O APIC.
 *
 * @returns true if delivered, false if postponed.
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   iIrq        The irq.
 * @param   iLevel      The new level.
 * @param   uTagSrc     The IRQ tag and source.
 *
 * @remarks The caller holds the PDM lock.
 */
DECLHIDDEN(bool) pdmR0IsaSetIrq(PGVM pGVM, int iIrq, int iLevel, uint32_t uTagSrc)
{
    if (RT_LIKELY(    (   pGVM->pdm.s.IoApic.pDevInsR0
                       || !pGVM->pdm.s.IoApic.pDevInsR3)
                  &&  (   pGVM->pdm.s.Pic.pDevInsR0
                       || !pGVM->pdm.s.Pic.pDevInsR3)))
    {
        if (pGVM->pdm.s.Pic.pDevInsR0)
            pGVM->pdm.s.Pic.pfnSetIrqR0(pGVM->pdm.s.Pic.pDevInsR0, iIrq, iLevel, uTagSrc);
        if (pGVM->pdm.s.IoApic.pDevInsR0)
            pGVM->pdm.s.IoApic.pfnSetIrqR0(pGVM->pdm.s.IoApic.pDevInsR0, NIL_PCIBDF, iIrq, iLevel, uTagSrc);
        return true;
    }

    /* queue for ring-3 execution. */
    PPDMDEVHLPTASK pTask = (PPDMDEVHLPTASK)PDMQueueAlloc(pGVM, pGVM->pdm.s.hDevHlpQueue, pGVM);
    AssertReturn(pTask, false);

    pTask->enmOp = PDMDEVHLPTASKOP_ISA_SET_IRQ;
    pTask->pDevInsR3 = NIL_RTR3PTR; /* not required */
    pTask->u.IsaSetIrq.uBusDevFn = NIL_PCIBDF;
    pTask->u.IsaSetIrq.iIrq = iIrq;
    pTask->u.IsaSetIrq.iLevel = iLevel;
    pTask->u.IsaSetIrq.uTagSrc = uTagSrc;

    PDMQueueInsert(pGVM, pGVM->pdm.s.hDevHlpQueue, pGVM, &pTask->Core);
    return false;
}

