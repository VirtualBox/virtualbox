/* $Id: PDMDevMiscHlp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM - Pluggable Device and Driver Manager, Misc. Device Helpers.
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
#include "PDMInternal.h"
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/hm.h>
#ifndef VBOX_VMM_TARGET_ARMV8
# include <VBox/vmm/apic.h>
#endif
#include <VBox/vmm/vm.h>
#include <VBox/vmm/vmm.h>

#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/msi.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/thread.h>


#include "PDMInline.h"
#include "dtrace/VBoxVMM.h"



/** @name Ring-3 PIC Helpers
 * @{
 */

/** @interface_method_impl{PDMPICHLP,pfnSetInterruptFF} */
static DECLCALLBACK(void) pdmR3PicHlp_SetInterruptFF(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM    pVM = pDevIns->Internal.s.pVMR3;

    /* IRQ state should be loaded as-is by "LoadExec". Changes can be made from LoadDone. */
    Assert(pVM->enmVMState != VMSTATE_LOADING || pVM->pdm.s.fStateLoaded);

#if defined(VBOX_VMM_TARGET_ARMV8)
    AssertReleaseFailed();
    RT_NOREF(pVM);
#else
    PVMCPU pVCpu = pVM->apCpusR3[0];  /* for PIC we always deliver to CPU 0, SMP uses APIC */
    APICLocalInterrupt(pVCpu, 0 /* u8Pin */, 1 /* u8Level */, VINF_SUCCESS /* rcRZ */);
#endif
}


/** @interface_method_impl{PDMPICHLP,pfnClearInterruptFF} */
static DECLCALLBACK(void) pdmR3PicHlp_ClearInterruptFF(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;

    /* IRQ state should be loaded as-is by "LoadExec". Changes can be made from LoadDone. */
    Assert(pVM->enmVMState != VMSTATE_LOADING || pVM->pdm.s.fStateLoaded);

#if defined(VBOX_VMM_TARGET_ARMV8)
    AssertReleaseFailed();
    RT_NOREF(pVM);
#else
    PVMCPU pVCpu = pVM->apCpusR3[0];  /* for PIC we always deliver to CPU 0, SMP uses APIC */
    APICLocalInterrupt(pVCpu, 0 /* u8Pin */,  0 /* u8Level */, VINF_SUCCESS /* rcRZ */);
#endif
}


/** @interface_method_impl{PDMPICHLP,pfnLock} */
static DECLCALLBACK(int) pdmR3PicHlp_Lock(PPDMDEVINS pDevIns, int rc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    return pdmLockEx(pDevIns->Internal.s.pVMR3, rc);
}


/** @interface_method_impl{PDMPICHLP,pfnUnlock} */
static DECLCALLBACK(void) pdmR3PicHlp_Unlock(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    pdmUnlock(pDevIns->Internal.s.pVMR3);
}


/**
 * PIC Device Helpers.
 */
const PDMPICHLP g_pdmR3DevPicHlp =
{
    PDM_PICHLP_VERSION,
    pdmR3PicHlp_SetInterruptFF,
    pdmR3PicHlp_ClearInterruptFF,
    pdmR3PicHlp_Lock,
    pdmR3PicHlp_Unlock,
    PDM_PICHLP_VERSION /* the end */
};

/** @} */


/** @name Ring-3 I/O APIC Helpers
 * @{
 */

/** @interface_method_impl{PDMIOAPICHLP,pfnApicBusDeliver} */
static DECLCALLBACK(int) pdmR3IoApicHlp_ApicBusDeliver(PPDMDEVINS pDevIns, uint8_t u8Dest, uint8_t u8DestMode,
                                                       uint8_t u8DeliveryMode, uint8_t uVector, uint8_t u8Polarity,
                                                       uint8_t u8TriggerMode, uint32_t uTagSrc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3IoApicHlp_ApicBusDeliver: caller='%s'/%d: u8Dest=%RX8 u8DestMode=%RX8 u8DeliveryMode=%RX8 uVector=%RX8 u8Polarity=%RX8 u8TriggerMode=%RX8 uTagSrc=%#x\n",
             pDevIns->pReg->szName, pDevIns->iInstance, u8Dest, u8DestMode, u8DeliveryMode, uVector, u8Polarity, u8TriggerMode, uTagSrc));
#if defined(VBOX_VMM_TARGET_ARMV8)
    AssertReleaseFailed();
    RT_NOREF(pDevIns, u8Dest, u8DestMode, u8DeliveryMode, uVector, u8Polarity, u8TriggerMode, uTagSrc);
    return VERR_NOT_IMPLEMENTED;
#else
    PVM pVM = pDevIns->Internal.s.pVMR3;
    return APICBusDeliver(pVM, u8Dest, u8DestMode, u8DeliveryMode, uVector, u8Polarity, u8TriggerMode, uTagSrc);
#endif
}


/** @interface_method_impl{PDMIOAPICHLP,pfnLock} */
static DECLCALLBACK(int) pdmR3IoApicHlp_Lock(PPDMDEVINS pDevIns, int rc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3IoApicHlp_Lock: caller='%s'/%d: rc=%Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return pdmLockEx(pDevIns->Internal.s.pVMR3, rc);
}


/** @interface_method_impl{PDMIOAPICHLP,pfnUnlock} */
static DECLCALLBACK(void) pdmR3IoApicHlp_Unlock(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3IoApicHlp_Unlock: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));
    pdmUnlock(pDevIns->Internal.s.pVMR3);
}


/** @interface_method_impl{PDMIOAPICHLP,pfnLockIsOwner} */
static DECLCALLBACK(bool) pdmR3IoApicHlp_LockIsOwner(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3IoApicHlp_LockIsOwner: caller='%s'/%d\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return pdmLockIsOwner(pDevIns->Internal.s.pVMR3);
}


/** @interface_method_impl{PDMIOAPICHLP,pfnIommuMsiRemap} */
static DECLCALLBACK(int) pdmR3IoApicHlp_IommuMsiRemap(PPDMDEVINS pDevIns, uint16_t idDevice, PCMSIMSG pMsiIn, PMSIMSG pMsiOut)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3IoApicHlp_IommuRemapMsi: caller='%s'/%d: pMsiIn=(%#RX64, %#RU32)\n", pDevIns->pReg->szName,
             pDevIns->iInstance, pMsiIn->Addr.u64, pMsiIn->Data.u32));

#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
    if (pdmIommuIsPresent(pDevIns))
        return pdmIommuMsiRemap(pDevIns, idDevice, pMsiIn, pMsiOut);
#else
    RT_NOREF(pDevIns, idDevice, pMsiIn, pMsiOut);
#endif
    return VERR_IOMMU_NOT_PRESENT;
}


/**
 * I/O APIC Device Helpers.
 */
const PDMIOAPICHLP g_pdmR3DevIoApicHlp =
{
    PDM_IOAPICHLP_VERSION,
    pdmR3IoApicHlp_ApicBusDeliver,
    pdmR3IoApicHlp_Lock,
    pdmR3IoApicHlp_Unlock,
    pdmR3IoApicHlp_LockIsOwner,
    pdmR3IoApicHlp_IommuMsiRemap,
    PDM_IOAPICHLP_VERSION /* the end */
};

/** @} */




/** @name Ring-3 PCI Bus Helpers
 * @{
 */

/** @interface_method_impl{PDMPCIHLPR3,pfnIsaSetIrq} */
static DECLCALLBACK(void) pdmR3PciHlp_IsaSetIrq(PPDMDEVINS pDevIns, int iIrq, int iLevel, uint32_t uTagSrc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    Log4(("pdmR3PciHlp_IsaSetIrq: iIrq=%d iLevel=%d uTagSrc=%#x\n", iIrq, iLevel, uTagSrc));
    PDMIsaSetIrq(pDevIns->Internal.s.pVMR3, iIrq, iLevel, uTagSrc);
}


/** @interface_method_impl{PDMPCIHLPR3,pfnIoApicSetIrq} */
static DECLCALLBACK(void) pdmR3PciHlp_IoApicSetIrq(PPDMDEVINS pDevIns, PCIBDF uBusDevFn, int iIrq, int iLevel, uint32_t uTagSrc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    Log4(("pdmR3PciHlp_IoApicSetIrq: uBusDevFn=%#x iIrq=%d iLevel=%d uTagSrc=%#x\n", uBusDevFn, iIrq, iLevel, uTagSrc));
    PDMIoApicSetIrq(pDevIns->Internal.s.pVMR3, uBusDevFn, iIrq, iLevel, uTagSrc);
}


/** @interface_method_impl{PDMPCIHLPR3,pfnIoApicSendMsi} */
static DECLCALLBACK(void) pdmR3PciHlp_IoApicSendMsi(PPDMDEVINS pDevIns, PCIBDF uBusDevFn, PCMSIMSG pMsi, uint32_t uTagSrc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    Assert(PCIBDF_IS_VALID(uBusDevFn));
    Log4(("pdmR3PciHlp_IoApicSendMsi: uBusDevFn=%#x Msi (Addr=%#RX64 Data=%#x) uTagSrc=%#x\n", uBusDevFn,
          pMsi->Addr.u64, pMsi->Data.u32, uTagSrc));
    PDMIoApicSendMsi(pDevIns->Internal.s.pVMR3, uBusDevFn, pMsi, uTagSrc);
}


/** @interface_method_impl{PDMPCIHLPR3,pfnLock} */
static DECLCALLBACK(int) pdmR3PciHlp_Lock(PPDMDEVINS pDevIns, int rc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3PciHlp_Lock: caller='%s'/%d: rc=%Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return pdmLockEx(pDevIns->Internal.s.pVMR3, rc);
}


/** @interface_method_impl{PDMPCIHLPR3,pfnUnlock} */
static DECLCALLBACK(void) pdmR3PciHlp_Unlock(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3PciHlp_Unlock: caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));
    pdmUnlock(pDevIns->Internal.s.pVMR3);
}


/** @interface_method_impl{PDMPCIHLPR3,pfnGetBusByNo} */
static DECLCALLBACK(PPDMDEVINS) pdmR3PciHlp_GetBusByNo(PPDMDEVINS pDevIns, uint32_t idxPdmBus)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    AssertReturn(idxPdmBus < RT_ELEMENTS(pVM->pdm.s.aPciBuses), NULL);
    PPDMDEVINS pRetDevIns = pVM->pdm.s.aPciBuses[idxPdmBus].pDevInsR3;
    LogFlow(("pdmR3PciHlp_GetBusByNo: caller='%s'/%d: returns %p\n", pDevIns->pReg->szName, pDevIns->iInstance, pRetDevIns));
    return pRetDevIns;
}


/**
 * PCI Bus Device Helpers.
 */
const PDMPCIHLPR3 g_pdmR3DevPciHlp =
{
    PDM_PCIHLPR3_VERSION,
    pdmR3PciHlp_IsaSetIrq,
    pdmR3PciHlp_IoApicSetIrq,
    pdmR3PciHlp_IoApicSendMsi,
    pdmR3PciHlp_Lock,
    pdmR3PciHlp_Unlock,
    pdmR3PciHlp_GetBusByNo,
    PDM_PCIHLPR3_VERSION, /* the end */
};

/** @} */


/** @name Ring-3 IOMMU Helpers
 * @{
 */

/** @interface_method_impl{PDMIOMMUHLPR3,pfnLock} */
static DECLCALLBACK(int) pdmR3IommuHlp_Lock(PPDMDEVINS pDevIns, int rc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlowFunc(("caller='%s'/%d: rc=%Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return pdmLockEx(pDevIns->Internal.s.pVMR3, rc);
}


/** @interface_method_impl{PDMIOMMUHLPR3,pfnUnlock} */
static DECLCALLBACK(void) pdmR3IommuHlp_Unlock(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlowFunc(("caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));
    pdmUnlock(pDevIns->Internal.s.pVMR3);
}


/** @interface_method_impl{PDMIOMMUHLPR3,pfnLockIsOwner} */
static DECLCALLBACK(bool) pdmR3IommuHlp_LockIsOwner(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlowFunc(("caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));
    return pdmLockIsOwner(pDevIns->Internal.s.pVMR3);
}


/** @interface_method_impl{PDMIOMMUHLPR3,pfnSendMsi} */
static DECLCALLBACK(void) pdmR3IommuHlp_SendMsi(PPDMDEVINS pDevIns, PCMSIMSG pMsi, uint32_t uTagSrc)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlowFunc(("caller='%s'/%d:\n", pDevIns->pReg->szName, pDevIns->iInstance));
    PDMIoApicSendMsi(pDevIns->Internal.s.pVMR3, NIL_PCIBDF, pMsi, uTagSrc);
}


/**
 * IOMMU Device Helpers.
 */
const PDMIOMMUHLPR3 g_pdmR3DevIommuHlp =
{
    PDM_IOMMUHLPR3_VERSION,
    pdmR3IommuHlp_Lock,
    pdmR3IommuHlp_Unlock,
    pdmR3IommuHlp_LockIsOwner,
    pdmR3IommuHlp_SendMsi,
    PDM_IOMMUHLPR3_VERSION /* the end */
};

/** @} */


/** @name Ring-3 HPET Helpers
 * @{
 */

/** @interface_method_impl{PDMHPETHLPR3,pfnSetLegacyMode} */
static DECLCALLBACK(int) pdmR3HpetHlp_SetLegacyMode(PPDMDEVINS pDevIns, bool fActivated)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3HpetHlp_SetLegacyMode: caller='%s'/%d: fActivated=%RTbool\n", pDevIns->pReg->szName, pDevIns->iInstance, fActivated));

    size_t                      i;
    int                         rc = VINF_SUCCESS;
    static const char * const   s_apszDevsToNotify[] =
    {
        "i8254",
        "mc146818"
    };
    for (i = 0; i < RT_ELEMENTS(s_apszDevsToNotify); i++)
    {
        PPDMIBASE pBase;
        rc = PDMR3QueryDevice(pDevIns->Internal.s.pVMR3->pUVM, "i8254", 0, &pBase);
        if (RT_SUCCESS(rc))
        {
            PPDMIHPETLEGACYNOTIFY pPort = PDMIBASE_QUERY_INTERFACE(pBase, PDMIHPETLEGACYNOTIFY);
            AssertLogRelMsgBreakStmt(pPort, ("%s\n", s_apszDevsToNotify[i]), rc = VERR_PDM_HPET_LEGACY_NOTIFY_MISSING);
            pPort->pfnModeChanged(pPort, fActivated);
        }
        else if (   rc == VERR_PDM_DEVICE_NOT_FOUND
                 || rc == VERR_PDM_DEVICE_INSTANCE_NOT_FOUND)
            rc = VINF_SUCCESS; /* the device isn't configured, ignore. */
        else
            AssertLogRelMsgFailedBreak(("%s -> %Rrc\n", s_apszDevsToNotify[i], rc));
    }

    /* Don't bother cleaning up, any failure here will cause a guru meditation. */

    LogFlow(("pdmR3HpetHlp_SetLegacyMode: caller='%s'/%d: returns %Rrc\n", pDevIns->pReg->szName, pDevIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMHPETHLPR3,pfnSetIrq} */
static DECLCALLBACK(int) pdmR3HpetHlp_SetIrq(PPDMDEVINS pDevIns, int iIrq, int iLevel)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    LogFlow(("pdmR3HpetHlp_SetIrq: caller='%s'/%d: iIrq=%d iLevel=%d\n", pDevIns->pReg->szName, pDevIns->iInstance, iIrq, iLevel));
    PVM pVM = pDevIns->Internal.s.pVMR3;

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

    PDMIsaSetIrq(pVM, iIrq, iLevel, uTagSrc); /* (The API takes the lock recursively.) */

    if (iLevel == PDM_IRQ_LEVEL_LOW)
        VBOXVMM_PDM_IRQ_LOW(VMMGetCpu(pVM), RT_LOWORD(uTagSrc), RT_HIWORD(uTagSrc));
    pdmUnlock(pVM);
    return 0;
}


/**
 * HPET Device Helpers.
 */
const PDMHPETHLPR3 g_pdmR3DevHpetHlp =
{
    PDM_HPETHLPR3_VERSION,
    pdmR3HpetHlp_SetLegacyMode,
    pdmR3HpetHlp_SetIrq,
    PDM_HPETHLPR3_VERSION, /* the end */
};

/** @} */


/** @name Ring-3 Raw PCI Device Helpers
 * @{
 */

/** @interface_method_impl{PDMPCIRAWHLPR3,pfnGetRCHelpers} */
static DECLCALLBACK(PCPDMPCIRAWHLPRC) pdmR3PciRawHlp_GetRCHelpers(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

    RTRCPTR pRCHelpers = NIL_RTRCPTR;
#if 0
    if (VM_IS_RAW_MODE_ENABLED(pVM))
    {
        int rc = PDMR3LdrGetSymbolRC(pVM, NULL, "g_pdmRCPciRawHlp", &pRCHelpers);
        AssertReleaseRC(rc);
        AssertRelease(pRCHelpers);
    }
#else
    RT_NOREF(pVM, pDevIns);
#endif

    LogFlow(("pdmR3PciRawHlp_GetGCHelpers: caller='%s'/%d: returns %RRv\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pRCHelpers));
    return pRCHelpers;
}


/** @interface_method_impl{PDMPCIRAWHLPR3,pfnGetR0Helpers} */
static DECLCALLBACK(PCPDMPCIRAWHLPR0) pdmR3PciRawHlp_GetR0Helpers(PPDMDEVINS pDevIns)
{
    PDMDEV_ASSERT_DEVINS(pDevIns);
    PVM pVM = pDevIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    PCPDMHPETHLPR0 pR0Helpers = NIL_RTR0PTR;
    int rc = PDMR3LdrGetSymbolR0(pVM, NULL, "g_pdmR0PciRawHlp", &pR0Helpers);
    AssertReleaseRC(rc);
    AssertRelease(pR0Helpers);
    LogFlow(("pdmR3PciRawHlp_GetR0Helpers: caller='%s'/%d: returns %RHv\n",
             pDevIns->pReg->szName, pDevIns->iInstance, pR0Helpers));
    return pR0Helpers;
}


/**
 * Raw PCI Device Helpers.
 */
const PDMPCIRAWHLPR3 g_pdmR3DevPciRawHlp =
{
    PDM_PCIRAWHLPR3_VERSION,
    pdmR3PciRawHlp_GetRCHelpers,
    pdmR3PciRawHlp_GetR0Helpers,
    PDM_PCIRAWHLPR3_VERSION, /* the end */
};

/** @} */


/* none yet */

/**
 * Firmware Device Helpers.
 */
const PDMFWHLPR3 g_pdmR3DevFirmwareHlp =
{
    PDM_FWHLPR3_VERSION,
    PDM_FWHLPR3_VERSION
};

/**
 * DMAC Device Helpers.
 */
const PDMDMACHLP g_pdmR3DevDmacHlp =
{
    PDM_DMACHLP_VERSION
};




/* none yet */

/**
 * RTC Device Helpers.
 */
const PDMRTCHLP g_pdmR3DevRtcHlp =
{
    PDM_RTCHLP_VERSION
};

