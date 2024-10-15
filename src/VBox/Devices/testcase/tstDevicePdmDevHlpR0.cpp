/* $Id: tstDevicePdmDevHlpR0.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstDevice - Test framework for PDM devices/drivers, PDM fake R0 helper implementation.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#define IN_RING0
#define IN_SUP_R0
#define LINUX_VERSION_CODE 0
#define KERNEL_VERSION(a,b,c) 1
#include <iprt/linux/version.h>
#include <VBox/types.h>
#include <VBox/version.h>
#include <VBox/vmm/pdmpci.h>

#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/string.h>

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

#undef RT_VALID_PTR
#define RT_VALID_PTR(ptr) true


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/** @interface_method_impl{PDMDEVHLPR0,pfnIoPortSetUpContextEx} */
static DECLCALLBACK(int) pdmR0DevHlp_IoPortSetUpContextEx(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts,
                                                          PFNIOMIOPORTNEWOUT pfnOut, PFNIOMIOPORTNEWIN pfnIn,
                                                          PFNIOMIOPORTNEWOUTSTRING pfnOutStr, PFNIOMIOPORTNEWINSTRING pfnInStr,
                                                          void *pvUser)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_IoPortSetUpContextEx: caller='%s'/%d: hIoPorts=%#x pfnOut=%p pfnIn=%p pfnOutStr=%p pfnInStr=%p pvUser=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hIoPorts, pfnOut, pfnIn, pfnOutStr, pfnInStr, pvUser));

    int rc = VINF_SUCCESS;
    PRTDEVDUTIOPORT pIoPort = (PRTDEVDUTIOPORT)hIoPorts;
    if (RT_LIKELY(pIoPort))
    {
        pIoPort->pvUserR0    = pvUser;
        pIoPort->pfnOutR0    = pfnOut;
        pIoPort->pfnInR0     = pfnIn;
        pIoPort->pfnOutStrR0 = pfnOutStr;
        pIoPort->pfnInStrR0  = pfnInStr;
    }
    else
        AssertReleaseFailed();

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

    int rc = VINF_SUCCESS;
    PRTDEVDUTMMIO pMmio = (PRTDEVDUTMMIO)hRegion;
    if (RT_LIKELY(pMmio))
    {
        pMmio->pvUserR0    = pvUser;
        pMmio->pfnWriteR0  = pfnWrite;
        pMmio->pfnReadR0   = pfnRead;
        pMmio->pfnFillR0   = pfnFill;
    }
    else
        AssertReleaseFailed();

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

    int rc = VERR_NOT_IMPLEMENTED;
    AssertFailed();

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

    AssertFailed();

    LogFlow(("pdmR0DevHlp_PCISetIrq: caller=%p/%d: returns void; uTagSrc=%#x\n", pDevIns, pDevIns->iInstance, 0 /*uTagSrc*/));
}


/** @interface_method_impl{PDMDEVHLPR0,pfnISASetIrq} */
static DECLCALLBACK(void) pdmR0DevHlp_ISASetIrq(PPDMDEVINS pDevIns, int iIrq, int iLevel)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_ISASetIrq: caller=%p/%d: iIrq=%d iLevel=%d\n", pDevIns, pDevIns->iInstance, iIrq, iLevel));

    AssertFailed();

    LogFlow(("pdmR0DevHlp_ISASetIrq: caller=%p/%d: returns void; uTagSrc=%#x\n", pDevIns, pDevIns->iInstance, 0 /*uTagSrc*/));
}


/** @interface_method_impl{PDMDEVHLPR0,pfnPhysRead} */
static DECLCALLBACK(int) pdmR0DevHlp_PhysRead(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead, uint32_t fFlags)
{
    RT_NOREF(fFlags);

    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_PhysRead: caller=%p/%d: GCPhys=%RGp pvBuf=%p cbRead=%#x\n",
             pDevIns, pDevIns->iInstance, GCPhys, pvBuf, cbRead));

    VBOXSTRICTRC rcStrict = VERR_NOT_IMPLEMENTED;
    AssertFailed();

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

    VBOXSTRICTRC rcStrict = VERR_NOT_IMPLEMENTED;
    AssertFailed();

    Log(("pdmR0DevHlp_PhysWrite: caller=%p/%d: returns %Rrc\n", pDevIns, pDevIns->iInstance, VBOXSTRICTRC_VAL(rcStrict) ));
    return VBOXSTRICTRC_VAL(rcStrict);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnA20IsEnabled} */
static DECLCALLBACK(bool) pdmR0DevHlp_A20IsEnabled(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_A20IsEnabled: caller=%p/%d:\n", pDevIns, pDevIns->iInstance));

    bool fEnabled = false;
    AssertFailed();

    Log(("pdmR0DevHlp_A20IsEnabled: caller=%p/%d: returns %RTbool\n", pDevIns, pDevIns->iInstance, fEnabled));
    return fEnabled;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnVMState} */
static DECLCALLBACK(VMSTATE) pdmR0DevHlp_VMState(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    VMSTATE enmVMState = VMSTATE_CREATING;// pDevIns->Internal.s.pGVM->enmVMState;

    LogFlow(("pdmR0DevHlp_VMState: caller=%p/%d: returns %d\n", pDevIns, pDevIns->iInstance, enmVMState));
    return enmVMState;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnGetVM} */
static DECLCALLBACK(PVMCC)  pdmR0DevHlp_GetVM(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_GetVM: caller='%p'/%d\n", pDevIns, pDevIns->iInstance));
    AssertFailed();
    return NULL; //pDevIns->Internal.s.pGVM;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnGetVMCPU} */
static DECLCALLBACK(PVMCPUCC) pdmR0DevHlp_GetVMCPU(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_GetVMCPU: caller='%p'/%d\n", pDevIns, pDevIns->iInstance));
    AssertFailed();
    return NULL; //VMMGetCpu(pDevIns->Internal.s.pGVM);
}


/** @interface_method_impl{PDMDEVHLPRC,pfnGetCurrentCpuId} */
static DECLCALLBACK(VMCPUID) pdmR0DevHlp_GetCurrentCpuId(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_GetCurrentCpuId: caller='%p'/%d for CPU %u\n", pDevIns, pDevIns->iInstance, 0 /*idCpu*/));
    AssertFailed();
    return 0; //idCpu;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnGetMainExecutionEngine} */
static DECLCALLBACK(uint8_t) pdmR0DevHlp_GetMainExecutionEngine(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_GetMainExecutionEngine: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return VM_EXEC_ENGINE_NOT_SET;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerFromMicro} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TimerFromMicro(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMicroSecs)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return 0; //TMTimerFromMicro(pDevIns->Internal.s.pGVM, hTimer, cMicroSecs);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerFromMilli} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TimerFromMilli(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMilliSecs)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return 0;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerFromNano} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TimerFromNano(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cNanoSecs)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return 0;
}

/** @interface_method_impl{PDMDEVHLPR0,pfnTimerGet} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TimerGet(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return 0;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerGetFreq} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TimerGetFreq(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return 0;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerGetNano} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TimerGetNano(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return 0;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerIsActive} */
static DECLCALLBACK(bool) pdmR0DevHlp_TimerIsActive(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return false;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerIsLockOwner} */
static DECLCALLBACK(bool) pdmR0DevHlp_TimerIsLockOwner(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return false;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerLockClock} */
static DECLCALLBACK(VBOXSTRICTRC) pdmR0DevHlp_TimerLockClock(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerLockClock2} */
static DECLCALLBACK(VBOXSTRICTRC) pdmR0DevHlp_TimerLockClock2(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer,
                                                              PPDMCRITSECT pCritSect, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerSet} */
static DECLCALLBACK(int) pdmR0DevHlp_TimerSet(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t uExpire)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerSetFrequencyHint} */
static DECLCALLBACK(int) pdmR0DevHlp_TimerSetFrequencyHint(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint32_t uHz)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerSetMicro} */
static DECLCALLBACK(int) pdmR0DevHlp_TimerSetMicro(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMicrosToNext)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerSetMillies} */
static DECLCALLBACK(int) pdmR0DevHlp_TimerSetMillies(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMilliesToNext)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerSetNano} */
static DECLCALLBACK(int) pdmR0DevHlp_TimerSetNano(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cNanosToNext)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerSetRelative} */
static DECLCALLBACK(int) pdmR0DevHlp_TimerSetRelative(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cTicksToNext, uint64_t *pu64Now)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerStop} */
static DECLCALLBACK(int) pdmR0DevHlp_TimerStop(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerUnlockClock} */
static DECLCALLBACK(void) pdmR0DevHlp_TimerUnlockClock(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTimerUnlockClock2} */
static DECLCALLBACK(void) pdmR0DevHlp_TimerUnlockClock2(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTMTimeVirtGet} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TMTimeVirtGet(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_TMTimeVirtGet: caller='%p'/%d\n", pDevIns, pDevIns->iInstance));
    AssertFailed();
    return 0;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTMTimeVirtGetFreq} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TMTimeVirtGetFreq(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_TMTimeVirtGetFreq: caller='%p'/%d\n", pDevIns, pDevIns->iInstance));
    AssertFailed();
    return 0;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTMTimeVirtGetNano} */
static DECLCALLBACK(uint64_t) pdmR0DevHlp_TMTimeVirtGetNano(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_TMTimeVirtGetNano: caller='%p'/%d\n", pDevIns, pDevIns->iInstance));
    AssertFailed();
    return 0;
}


/** Converts a queue handle to a ring-0 queue pointer. */
DECLINLINE(PPDMQUEUE)  pdmR0DevHlp_QueueToPtr(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return NULL;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnQueueAlloc} */
static DECLCALLBACK(PPDMQUEUEITEMCORE) pdmR0DevHlp_QueueAlloc(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue)
{
    AssertFailed();
    return NULL; //PDMQueueAlloc(pdmR0DevHlp_QueueToPtr(pDevIns, hQueue));
}


/** @interface_method_impl{PDMDEVHLPR0,pfnQueueInsert} */
static DECLCALLBACK(void) pdmR0DevHlp_QueueInsert(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue, PPDMQUEUEITEMCORE pItem)
{
    AssertFailed();
    //return PDMQueueInsert(pdmR0DevHlp_QueueToPtr(pDevIns, hQueue), pItem);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnQueueFlushIfNecessary} */
static DECLCALLBACK(bool) pdmR0DevHlp_QueueFlushIfNecessary(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue)
{
    AssertFailed();
    return false; //PDMQueueFlushIfNecessary(pdmR0DevHlp_QueueToPtr(pDevIns, hQueue));
}


/** @interface_method_impl{PDMDEVHLPR0,pfnTaskTrigger} */
static DECLCALLBACK(int) pdmR0DevHlp_TaskTrigger(PPDMDEVINS pDevIns, PDMTASKHANDLE hTask)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_TaskTrigger: caller='%s'/%d: hTask=%RU64\n", pDevIns->pReg->szName, pDevIns->iInstance, hTask));

    AssertFailed();
    int rc = VERR_NOT_IMPLEMENTED; //PDMTaskTrigger(pDevIns->Internal.s.pGVM, PDMTASKTYPE_DEV, pDevIns->pDevInsForR3, hTask);

    LogFlow(("pdmR0DevHlp_TaskTrigger: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventSignal} */
static DECLCALLBACK(int) pdmR0DevHlp_SUPSemEventSignal(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventSignal: caller='%s'/%d: hEvent=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, hEvent));

    AssertFailed();
    int rc = VERR_NOT_IMPLEMENTED; //SUPSemEventSignal(pDevIns->Internal.s.pGVM->pSession, hEvent);

    LogFlow(("pdmR0DevHlp_SUPSemEventSignal: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventWaitNoResume} */
static DECLCALLBACK(int) pdmR0DevHlp_SUPSemEventWaitNoResume(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint32_t cMillies)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventWaitNoResume: caller='%s'/%d: hEvent=%p cNsTimeout=%RU32\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEvent, cMillies));

    AssertFailed();
    int rc = VERR_NOT_IMPLEMENTED; //SUPSemEventWaitNoResume(pDevIns->Internal.s.pGVM->pSession, hEvent, cMillies);

    LogFlow(("pdmR0DevHlp_SUPSemEventWaitNoResume: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventWaitNsAbsIntr} */
static DECLCALLBACK(int) pdmR0DevHlp_SUPSemEventWaitNsAbsIntr(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint64_t uNsTimeout)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventWaitNsAbsIntr: caller='%s'/%d: hEvent=%p uNsTimeout=%RU64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEvent, uNsTimeout));

    AssertFailed();
    int rc = VERR_NOT_IMPLEMENTED; //SUPSemEventWaitNsAbsIntr(pDevIns->Internal.s.pGVM->pSession, hEvent, uNsTimeout);

    LogFlow(("pdmR0DevHlp_SUPSemEventWaitNsAbsIntr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventWaitNsRelIntr} */
static DECLCALLBACK(int) pdmR0DevHlp_SUPSemEventWaitNsRelIntr(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint64_t cNsTimeout)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventWaitNsRelIntr: caller='%s'/%d: hEvent=%p cNsTimeout=%RU64\n",
             pDevIns->pReg->szName, pDevIns->iInstance, hEvent, cNsTimeout));

    AssertFailed();
    int rc = VERR_NOT_IMPLEMENTED; //SUPSemEventWaitNsRelIntr(pDevIns->Internal.s.pGVM->pSession, hEvent, cNsTimeout);

    LogFlow(("pdmR0DevHlp_SUPSemEventWaitNsRelIntr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventGetResolution} */
static DECLCALLBACK(uint32_t) pdmR0DevHlp_SUPSemEventGetResolution(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventGetResolution: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    AssertFailed();
    uint32_t cNsResolution = 0; //SUPSemEventGetResolution(pDevIns->Internal.s.pGVM->pSession);

    LogFlow(("pdmR0DevHlp_SUPSemEventGetResolution: caller='%s'/%d: returns %u\n", pDevIns->pReg->szName, pDevIns->iInstance, cNsResolution));
    return cNsResolution;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventMultiSignal} */
static DECLCALLBACK(int) pdmR0DevHlp_SUPSemEventMultiSignal(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventMultiSignal: caller='%s'/%d: hEventMulti=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, hEventMulti));

    AssertFailed();
    int rc = VERR_NOT_IMPLEMENTED; //SUPSemEventMultiSignal(pDevIns->Internal.s.pGVM->pSession, hEventMulti);

    LogFlow(("pdmR0DevHlp_SUPSemEventMultiSignal: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventMultiReset} */
static DECLCALLBACK(int) pdmR0DevHlp_SUPSemEventMultiReset(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventMultiReset: caller='%s'/%d: hEventMulti=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, hEventMulti));

    AssertFailed();
    int rc = VERR_NOT_IMPLEMENTED; //SUPSemEventMultiReset(pDevIns->Internal.s.pGVM->pSession, hEventMulti);

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

    AssertFailed();
    int rc = VERR_NOT_IMPLEMENTED; //SUPSemEventMultiWaitNoResume(pDevIns->Internal.s.pGVM->pSession, hEventMulti, cMillies);

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

    AssertFailed();
    int rc = VERR_NOT_IMPLEMENTED; //SUPSemEventMultiWaitNsAbsIntr(pDevIns->Internal.s.pGVM->pSession, hEventMulti, uNsTimeout);

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

    AssertFailed();
    int rc = VERR_NOT_IMPLEMENTED; //SUPSemEventMultiWaitNsRelIntr(pDevIns->Internal.s.pGVM->pSession, hEventMulti, cNsTimeout);

    LogFlow(("pdmR0DevHlp_SUPSemEventMultiWaitNsRelIntr: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnSUPSemEventMultiGetResolution} */
static DECLCALLBACK(uint32_t) pdmR0DevHlp_SUPSemEventMultiGetResolution(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_SUPSemEventMultiGetResolution: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));

    AssertFailed();
    uint32_t cNsResolution = 0; //SUPSemEventMultiGetResolution(pDevIns->Internal.s.pGVM->pSession);

    LogFlow(("pdmR0DevHlp_SUPSemEventMultiGetResolution: caller='%s'/%d: returns %u\n", pDevIns->pReg->szName, pDevIns->iInstance, cNsResolution));
    return cNsResolution;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectGetNop} */
static DECLCALLBACK(PPDMCRITSECT) pdmR0DevHlp_CritSectGetNop(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);

    PPDMCRITSECT pCritSect = &pDevIns->Internal.s.pDut->CritSectNop;
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
    AssertReturn(RTCritSectIsInitialized(&pCritSect->s.CritSect), VERR_INVALID_PARAMETER);

    pDevIns->pCritSectRoR0 = pCritSect;

    LogFlow(("pdmR0DevHlp_SetDeviceCritSect: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectEnter} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectEnter(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectEnter(pDevIns->Internal.s.pGVM, pCritSect, rcBusy);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectEnterDebug} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectEnterDebug(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, int rcBusy, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectEnterDebug(pDevIns->Internal.s.pGVM, pCritSect, rcBusy, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectTryEnter} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectTryEnter(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectTryEnter(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectTryEnterDebug} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectTryEnterDebug(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectTryEnterDebug(pDevIns->Internal.s.pGVM, pCritSect, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectLeave} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectLeave(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectLeave(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectIsOwner} */
static DECLCALLBACK(bool)     pdmR0DevHlp_CritSectIsOwner(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectIsOwner(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectIsInitialized} */
static DECLCALLBACK(bool)     pdmR0DevHlp_CritSectIsInitialized(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectIsInitialized(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectHasWaiters} */
static DECLCALLBACK(bool)     pdmR0DevHlp_CritSectHasWaiters(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectHasWaiters(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectGetRecursion} */
static DECLCALLBACK(uint32_t) pdmR0DevHlp_CritSectGetRecursion(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectGetRecursion(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectScheduleExitEvent} */
static DECLCALLBACK(int) pdmR0DevHlp_CritSectScheduleExitEvent(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect,
                                                               SUPSEMEVENT hEventToSignal)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMHCCritSectScheduleExitEvent(pCritSect, hEventToSignal);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwEnterShared} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwEnterShared(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectRwEnterShared(pDevIns->Internal.s.pGVM, pCritSect, rcBusy);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwEnterSharedDebug} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwEnterSharedDebug(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, int rcBusy,
                                                                     RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectRwEnterSharedDebug(pDevIns->Internal.s.pGVM, pCritSect, rcBusy, uId, RT_SRC_POS_ARGS);
}



/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwTryEnterShared} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwTryEnterShared(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectRwTryEnterShared(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwTryEnterSharedDebug} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwTryEnterSharedDebug(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect,
                                                                        RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectRwTryEnterSharedDebug(pDevIns->Internal.s.pGVM, pCritSect, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwLeaveShared} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwLeaveShared(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectRwLeaveShared(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwEnterExcl} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwEnterExcl(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, int rcBusy)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectRwEnterExcl(pDevIns->Internal.s.pGVM, pCritSect, rcBusy);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwEnterExclDebug} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwEnterExclDebug(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, int rcBusy,
                                                                   RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectRwEnterExclDebug(pDevIns->Internal.s.pGVM, pCritSect, rcBusy, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwTryEnterExcl} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwTryEnterExcl(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectRwTryEnterExcl(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwTryEnterExclDebug} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwTryEnterExclDebug(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect,
                                                                      RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectRwTryEnterExclDebug(pDevIns->Internal.s.pGVM, pCritSect, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwLeaveExcl} */
static DECLCALLBACK(int)      pdmR0DevHlp_CritSectRwLeaveExcl(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED; //PDMCritSectRwLeaveExcl(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwIsWriteOwner} */
static DECLCALLBACK(bool)     pdmR0DevHlp_CritSectRwIsWriteOwner(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return false; //PDMCritSectRwIsWriteOwner(pDevIns->Internal.s.pGVM, pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwIsReadOwner} */
static DECLCALLBACK(bool)     pdmR0DevHlp_CritSectRwIsReadOwner(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect, bool fWannaHear)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    return false; //PDMCritSectRwIsReadOwner(pDevIns->Internal.s.pGVM, pCritSect, fWannaHear);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwGetWriteRecursion} */
static DECLCALLBACK(uint32_t) pdmR0DevHlp_CritSectRwGetWriteRecursion(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    AssertFailed();
    return 0; //PDMCritSectRwGetWriteRecursion(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwGetWriterReadRecursion} */
static DECLCALLBACK(uint32_t) pdmR0DevHlp_CritSectRwGetWriterReadRecursion(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    AssertFailed();
    return 0; //PDMCritSectRwGetWriterReadRecursion(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwGetReadCount} */
static DECLCALLBACK(uint32_t) pdmR0DevHlp_CritSectRwGetReadCount(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    AssertFailed();
    return 0; //PDMCritSectRwGetReadCount(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnCritSectRwIsInitialized} */
static DECLCALLBACK(bool)     pdmR0DevHlp_CritSectRwIsInitialized(PPDMDEVINS pDevIns, PPDMCRITSECTRW pCritSect)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    RT_NOREF(pDevIns);
    AssertFailed();
    return false; //PDMCritSectRwIsInitialized(pCritSect);
}


/** @interface_method_impl{PDMDEVHLPR0,pfnDBGFTraceBuf} */
static DECLCALLBACK(RTTRACEBUF) pdmR0DevHlp_DBGFTraceBuf(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    AssertFailed();
    RTTRACEBUF hTraceBuf = NULL; //pDevIns->Internal.s.pGVM->hTraceBufR0;
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
#if 0
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
#endif

    AssertFailed();
    *ppPciHlp = NULL; //&g_pdmR0PciHlp;

    LogFlow(("pdmR0DevHlp_PCIBusSetUpContext: caller='%p'/%d: returns VINF_SUCCESS\n", pDevIns, pDevIns->iInstance));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnIommuSetUpContext} */
static DECLCALLBACK(int) pdmR0DevHlp_IommuSetUpContext(PPDMDEVINS pDevIns, PPDMIOMMUREGR0 pIommuReg, PCPDMIOMMUHLPR0 *ppIommuHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_IommuSetUpContext: caller='%p'/%d: pIommuReg=%p{.u32Version=%#x, u32TheEnd=%#x} ppIommuHlp=%p\n",
             pDevIns, pDevIns->iInstance, pIommuReg, pIommuReg->u32Version, pIommuReg->u32TheEnd, ppIommuHlp));
#if 0
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
#endif

    AssertFailed();
    *ppIommuHlp = NULL; //&g_pdmR0IommuHlp;

    LogFlow(("pdmR0DevHlp_IommuSetUpContext: caller='%p'/%d: returns VINF_SUCCESS\n", pDevIns, pDevIns->iInstance));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnPICSetUpContext} */
static DECLCALLBACK(int) pdmR0DevHlp_PICSetUpContext(PPDMDEVINS pDevIns, PPDMPICREG pPicReg, PCPDMPICHLP *ppPicHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_PICSetUpContext: caller='%s'/%d: pPicReg=%p:{.u32Version=%#x, .pfnSetIrq=%p, .pfnGetInterrupt=%p, .u32TheEnd=%#x } ppPicHlp=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pPicReg, pPicReg->u32Version, pPicReg->pfnSetIrq, pPicReg->pfnGetInterrupt, pPicReg->u32TheEnd, ppPicHlp));
#if 0
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
#endif
    Log(("PDM: Registered PIC device '%s'/%d pDevIns=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pDevIns));

    /* set the helper pointer and return. */
    AssertFailed();
    *ppPicHlp = NULL; //&g_pdmR0PicHlp;
    LogFlow(("pdmR0DevHlp_PICSetUpContext: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnApicSetUpContext} */
static DECLCALLBACK(int) pdmR0DevHlp_ApicSetUpContext(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_ApicSetUpContext: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));
#if 0
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
#endif
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
#if 0
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
#endif
    Log(("PDM: Registered IOAPIC device '%s'/%d pDevIns=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pDevIns));

    /* set the helper pointer and return. */
    AssertFailed();
    *ppIoApicHlp = NULL; //&g_pdmR0IoApicHlp;
    LogFlow(("pdmR0DevHlp_IoApicSetUpContext: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnHpetSetUpContext} */
static DECLCALLBACK(int) pdmR0DevHlp_HpetSetUpContext(PPDMDEVINS pDevIns, PPDMHPETREG pHpetReg, PCPDMHPETHLPR0 *ppHpetHlp)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_HpetSetUpContext: caller='%s'/%d: pHpetReg=%p:{.u32Version=%#x, } ppHpetHlp=%p\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pHpetReg, pHpetReg->u32Version, ppHpetHlp));
#if 0
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
#endif
    /*
     * Nothing to take down here at present.
     */
    Log(("PDM: Registered HPET device '%s'/%d pDevIns=%p\n", pDevIns->pReg->szName, pDevIns->iInstance, pDevIns));

    /* set the helper pointer and return. */
    AssertFailed();
    *ppHpetHlp = NULL; //&g_pdmR0HpetHlp;
    LogFlow(("pdmR0DevHlp_HpetSetUpContext: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVHLPR0,pfnPGMHandlerPhysicalPageTempOff} */
static DECLCALLBACK(int) pdmR0DevHlp_PGMHandlerPhysicalPageTempOff(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, RTGCPHYS GCPhysPage)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR0DevHlp_PGMHandlerPhysicalPageTempOff: caller='%s'/%d: GCPhys=%RGp\n", pDevIns->pReg->szName, pDevIns->iInstance, GCPhys));

    AssertFailed();
    int rc = VERR_NOT_IMPLEMENTED; //PGMHandlerPhysicalPageTempOff(pDevIns->Internal.s.pGVM, GCPhys, GCPhysPage);

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

    AssertFailed();
    int rc = VERR_NOT_IMPLEMENTED; //IOMMmioMapMmio2Page(pDevIns->Internal.s.pGVM, pDevIns, hRegion, offRegion, hMmio2, offMmio2, fPageFlags);

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

    AssertFailed();
    int rc = VERR_NOT_IMPLEMENTED; //IOMMmioResetRegion(pDevIns->Internal.s.pGVM, pDevIns, hRegion);

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

    AssertFailed();
    PGIMMMIO2REGION pRegion = NULL; //GIMGetMmio2Regions(pDevIns->Internal.s.pGVM, pcRegions);

    LogFlow(("pdmR0DevHlp_GIMGetMmio2Regions: caller='%s'/%d: returns %p\n", pDevIns->pReg->szName, pDevIns->iInstance, pRegion));
    return pRegion;
}


/**
 * The Ring-0 Device Helper Callbacks.
 */
const PDMDEVHLPR0 g_tstDevPdmDevHlpR0 =
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
