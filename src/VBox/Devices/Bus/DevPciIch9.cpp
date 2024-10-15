/* $Id: DevPciIch9.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevPCI - ICH9 southbridge PCI bus emulation device.
 *
 * @remarks We'll be slowly promoting the code in this file to common PCI bus
 *          code.   Function without 'static' and using 'devpci' as prefix is
 *          also used by DevPCI.cpp and have a prototype in DevPciInternal.h.
 *
 *          For the time being the DevPciMerge1.cpp.h file will remain separate,
 *          due to 5.1.  We can merge it into this one later in the dev cycle.
 *
 *          DO NOT use the PDMPciDev* or PCIDev* family of functions in this
 *          file except in the two callbacks for config space access (and the
 *          functions which are used exclusively by that code) and the two
 *          device constructors when setting up the config space for the
 *          bridges.  Everything else need extremely careful review.  Using
 *          them elsewhere (especially in the init code) causes weird failures
 *          with PCI passthrough, as it would only update the array of
 *          (emulated) config space, but not talk to the actual device (needs
 *          invoking the respective callback).
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV_PCI
#define PDMPCIDEV_INCLUDE_PRIVATE  /* Hack to get pdmpcidevint.h included at the right point. */
#include <VBox/vmm/pdmpcidev.h>

#include <VBox/AssertGuest.h>
#include <VBox/msi.h>
#ifdef VBOX_WITH_IOMMU_AMD
# include <VBox/iommu-amd.h>
#endif
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/mm.h>
#include <iprt/asm.h>
#include <iprt/asm-mem.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#ifdef IN_RING3
# include <iprt/mem.h>
# include <iprt/uuid.h>
#endif

#include "PciInline.h"
#include "VBoxDD.h"
#include "MsiCommon.h"
#include "DevPciInternal.h"
#ifdef VBOX_WITH_IOMMU_AMD
# include "../Bus/DevIommuAmd.h"
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * PCI configuration space address.
 */
typedef struct
{
    uint8_t  iBus;
    uint8_t  iDeviceFunc;
    uint16_t iRegister;
} PciAddress;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Saved state version of the ICH9 PCI bus device. */
#define VBOX_ICH9PCI_SAVED_STATE_VERSION                VBOX_ICH9PCI_SAVED_STATE_VERSION_4KB_CFG_SPACE
/** 4KB config space */
#define VBOX_ICH9PCI_SAVED_STATE_VERSION_4KB_CFG_SPACE  4
/** Adds I/O region types and sizes for dealing changes in resource regions. */
#define VBOX_ICH9PCI_SAVED_STATE_VERSION_REGION_SIZES   3
/** This appears to be the first state we need to care about. */
#define VBOX_ICH9PCI_SAVED_STATE_VERSION_MSI            2
/** This is apparently not supported or has a grossly incomplete state, juding
 * from hints in the code. */
#define VBOX_ICH9PCI_SAVED_STATE_VERSION_NOMSI          1

/** Invalid PCI region mapping address. */
#define INVALID_PCI_ADDRESS     UINT32_MAX


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/* Prototypes */
static void ich9pciSetIrqInternal(PPDMDEVINS pDevIns, PDEVPCIROOT pPciRoot, PDEVPCIBUSCC pBusCC,
                                  uint8_t uDevFn, PPDMPCIDEV pPciDev, int iIrq, int iLevel, uint32_t uTagSrc);
#ifdef IN_RING3
static int ich9pciFakePCIBIOS(PPDMDEVINS pDevIns);
static void ich9pciBiosInitAllDevicesOnBus(PPDMDEVINS pDevIns, PDEVPCIROOT pPciRoot, PDEVPCIBUS pBus);
static bool ich9pciBiosInitAllDevicesPrefetchableOnBus(PPDMDEVINS pDevIns, PDEVPCIROOT pPciRoot, PDEVPCIBUS pBus, bool fUse64Bit, bool fDryrun);
#endif


/**
 * See 7.2.2. PCI Express Enhanced Configuration Mechanism for details of address
 * mapping, we take n=6 approach
 */
DECLINLINE(void) ich9pciPhysToPciAddr(PDEVPCIROOT pPciRoot, RTGCPHYS off, PciAddress *pPciAddr)
{
    NOREF(pPciRoot);
    pPciAddr->iBus          = (off >> 20) & ((1<<6)       - 1);
    pPciAddr->iDeviceFunc   = (off >> 12) & ((1<<(5+3))   - 1); // 5 bits - device, 3 bits - function
    pPciAddr->iRegister     = (off >>  0) & ((1<<(6+4+2)) - 1); // 6 bits - register, 4 bits - extended register, 2 bits -Byte Enable
    RT_UNTRUSTED_VALIDATED_FENCE(); /* paranoia */
}

DECLINLINE(void) ich9pciStateToPciAddr(PDEVPCIROOT pPciRoot, RTGCPHYS addr, PciAddress *pPciAddr)
{
    pPciAddr->iBus         = (pPciRoot->uConfigReg >> 16) & 0xff;
    pPciAddr->iDeviceFunc  = (pPciRoot->uConfigReg >> 8) & 0xff;
    pPciAddr->iRegister    = (pPciRoot->uConfigReg & 0xfc) | (addr & 3);
    RT_UNTRUSTED_VALIDATED_FENCE(); /* paranoia */
}

static DECLCALLBACK(void) ich9pciSetIrq(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq, int iLevel, uint32_t uTagSrc)
{
    LogFlowFunc(("invoked by %p/%d: iIrq=%d iLevel=%d uTagSrc=%#x\n", pDevIns, pDevIns->iInstance, iIrq, iLevel, uTagSrc));
    ich9pciSetIrqInternal(pDevIns, PDMINS_2_DATA(pDevIns, PDEVPCIROOT), PDMINS_2_DATA_CC(pDevIns, PDEVPCIBUSCC),
                          pPciDev->uDevFn, pPciDev, iIrq, iLevel, uTagSrc);
}

/**
 * Worker for ich9pcibridgeSetIrq and pcibridgeSetIrq that walks up to the root
 * bridges and permutates iIrq accordingly.
 *
 * See ich9pciBiosInitAllDevicesOnBus for corresponding configuration code.
 */
DECLHIDDEN(PPDMDEVINS) devpcibridgeCommonSetIrqRootWalk(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq,
                                                        PDEVPCIBUS *ppBus, uint8_t *puDevFnBridge, int *piIrqPinBridge)
{
    PDEVPCIBUSCC const  pBridgeBusCC   = PDMINS_2_DATA_CC(pDevIns, PDEVPCIBUSCC); /* For keep using our own pcihlp.  */
    PPDMDEVINS const    pBridgeDevIns  = pDevIns;                                 /* ditto */

    PDEVPCIBUS          pBus           = PDMINS_2_DATA(pDevIns, PDEVPCIBUS);
    PPDMDEVINS          pDevInsBus;
    PPDMPCIDEV          pPciDevBus     = pDevIns->apPciDevs[0];
    uint8_t             uDevFnBridge   = pPciDevBus->uDevFn;
    int                 iIrqPinBridge  = ((pPciDev->uDevFn >> 3) + iIrq) & 3;
    uint64_t            bmSeen[256/64] = { 0, 0, 0, 0 };
    AssertCompile(sizeof(pPciDevBus->Int.s.idxPdmBus) == 1);
    ASMBitSet(bmSeen, pPciDevBus->Int.s.idxPdmBus);

    /* Walk the chain until we reach the host bus. */
    Assert(pBus->iBus != 0);
    for (;;)
    {
        /* Get the parent. */
        pDevInsBus = pBridgeBusCC->CTX_SUFF(pPciHlp)->pfnGetBusByNo(pBridgeDevIns, pPciDevBus->Int.s.idxPdmBus);
        AssertLogRelReturn(pDevInsBus, NULL);

        pBus       = PDMINS_2_DATA(pDevInsBus, PDEVPCIBUS);
        pPciDevBus = pDevInsBus->apPciDevs[0];
        if (pBus->iBus == 0)
        {
            *ppBus          = pBus;
            *puDevFnBridge  = uDevFnBridge;
            *piIrqPinBridge = iIrqPinBridge;
            return pDevInsBus;
        }

        uDevFnBridge  = pPciDevBus->uDevFn;
        iIrqPinBridge = ((uDevFnBridge >> 3) + iIrqPinBridge) & 3;

        /* Make sure that we cannot end up in a loop here: */
        AssertCompile(sizeof(pPciDevBus->Int.s.idxPdmBus) == 1);
        AssertMsgReturn(ASMBitTestAndSet(bmSeen, pPciDevBus->Int.s.idxPdmBus),
                        ("idxPdmBus=%u\n", pPciDevBus->Int.s.idxPdmBus),
                        NULL);
    }

}

static DECLCALLBACK(void) ich9pcibridgeSetIrq(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq, int iLevel, uint32_t uTagSrc)
{
    /*
     * The PCI-to-PCI bridge specification defines how the interrupt pins
     * are routed from the secondary to the primary bus (see chapter 9).
     * iIrq gives the interrupt pin the pci device asserted.
     * We change iIrq here according to the spec and call the SetIrq function
     * of our parent passing the device which asserted the interrupt instead of the device of the bridge.
     *
     * See ich9pciBiosInitAllDevicesOnBus for corresponding configuration code.
     */
    PDEVPCIBUS pBus;
    uint8_t    uDevFnBridge;
    int        iIrqPinBridge;
    PPDMDEVINS pDevInsBus = devpcibridgeCommonSetIrqRootWalk(pDevIns, pPciDev, iIrq, &pBus, &uDevFnBridge, &iIrqPinBridge);
    AssertReturnVoid(pDevInsBus);
    AssertMsg(pBus->iBus == 0, ("This is not the host pci bus iBus=%d\n", pBus->iBus));
    Assert(pDevInsBus->pReg == &g_DevicePciIch9); /* ASSUMPTION: Same style root bus.  Need callback interface to mix types. */

    /*
     * For MSI/MSI-X enabled devices the iIrq doesn't denote the pin but rather a vector which is completely
     * orthogonal to the pin based approach. The vector is not subject to the pin based routing with PCI bridges.
     */
    int iIrqPinVector = iIrqPinBridge;
    if (   MsiIsEnabled(pPciDev)
        || MsixIsEnabled(pPciDev))
        iIrqPinVector = iIrq;
    ich9pciSetIrqInternal(pDevInsBus, DEVPCIBUS_2_DEVPCIROOT(pBus), PDMINS_2_DATA_CC(pDevInsBus, PDEVPCIBUSCC),
                          uDevFnBridge, pPciDev, iIrqPinVector, iLevel, uTagSrc);
}

#ifdef IN_RING3

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,
 *      Port I/O Handler for Fake PCI BIOS trigger OUT operations at 0410h.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
ich9pciR3IOPortMagicPCIWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    Assert(offPort == 0); RT_NOREF2(pvUser, offPort);
    LogFlowFunc(("offPort=%#x u32=%#x cb=%d\n", offPort, u32, cb));
    if (cb == 4)
    {
        if (u32 == UINT32_C(19200509)) // Richard Adams
        {
            int rc = ich9pciFakePCIBIOS(pDevIns);
            AssertRC(rc);
        }
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,
 *      Port I/O Handler for Fake PCI BIOS trigger IN operations at 0410h.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
ich9pciR3IOPortMagicPCIRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    Assert(offPort == 0); RT_NOREF5(pDevIns, pvUser, offPort, pu32, cb);
    LogFunc(("offPort=%#x cb=%d VERR_IOM_IOPORT_UNUSED\n", offPort, cb));
    return VERR_IOM_IOPORT_UNUSED;
}

#endif /* IN_RING3 */

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,
 *      Port I/O Handler for PCI address OUT operations.}
 *
 * Emulates writes to Configuration Address Port at 0CF8h for Configuration
 * Mechanism \#1.
 */
static DECLCALLBACK(VBOXSTRICTRC)
ich9pciIOPortAddressWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    LogFlowFunc(("offPort=%#x u32=%#x cb=%d\n", offPort, u32, cb));
    Assert(offPort == 0); RT_NOREF2(offPort, pvUser);
    if (cb == 4)
    {
        PDEVPCIROOT pThis = PDMINS_2_DATA(pDevIns, PDEVPCIROOT);

        /*
         * bits [1:0] are hard-wired, read-only and must return zeroes
         * when read.
         */
        u32 &= ~3;

        PCI_LOCK_RET(pDevIns, VINF_IOM_R3_IOPORT_WRITE);
        pThis->uConfigReg = u32;
        PCI_UNLOCK(pDevIns);
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,
 *      Port I/O Handler for PCI data IN operations.}
 *
 * Emulates reads from Configuration Address Port at 0CF8h for Configuration
 * Mechanism \#1.
 */
static DECLCALLBACK(VBOXSTRICTRC)
ich9pciIOPortAddressRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    Assert(offPort == 0); RT_NOREF2(offPort, pvUser);
    if (cb == 4)
    {
        PDEVPCIROOT pThis = PDMINS_2_DATA(pDevIns, PDEVPCIROOT);

        PCI_LOCK_RET(pDevIns, VINF_IOM_R3_IOPORT_READ);
        *pu32 = pThis->uConfigReg;
        PCI_UNLOCK(pDevIns);

        LogFlowFunc(("offPort=%#x cb=%d -> %#x\n", offPort, cb, *pu32));
        return VINF_SUCCESS;
    }

    LogFunc(("offPort=%#x cb=%d VERR_IOM_IOPORT_UNUSED\n", offPort, cb));
    return VERR_IOM_IOPORT_UNUSED;
}


/**
 * Perform configuration space write.
 */
static VBOXSTRICTRC ich9pciConfigWrite(PPDMDEVINS pDevIns, PDEVPCIROOT pPciRoot, PciAddress const *pPciAddr,
                                       uint32_t u32Value, unsigned cb, int rcReschedule)
{
    VBOXSTRICTRC rcStrict = VINF_SUCCESS;

    if (pPciAddr->iBus != 0)       /* forward to subordinate bus */
    {
        if (pPciRoot->PciBus.cBridges)
        {
#ifdef IN_RING3 /** @todo do lookup in R0/RC too! r=klaus don't think that it can work, since the config space access callback only works in R3 */
            PPDMPCIDEV pBridgeDevice = devpciR3FindBridge(&pPciRoot->PciBus, pPciAddr->iBus);
            if (pBridgeDevice)
            {
                AssertPtr(pBridgeDevice->Int.s.pfnBridgeConfigWrite);
                rcStrict = pBridgeDevice->Int.s.pfnBridgeConfigWrite(pBridgeDevice->Int.s.CTX_SUFF(pDevIns), pPciAddr->iBus,
                                                                     pPciAddr->iDeviceFunc, pPciAddr->iRegister, cb, u32Value);
            }
#else
            rcStrict = rcReschedule;
#endif
        }
    }
    else                    /* forward to directly connected device */
    {
        R3PTRTYPE(PDMPCIDEV *) pPciDev = pPciRoot->PciBus.apDevices[pPciAddr->iDeviceFunc];
        if (pPciDev)
        {
#ifdef IN_RING3
            rcStrict = VINF_PDM_PCI_DO_DEFAULT;
            if (pPciDev->Int.s.pfnConfigWrite)
                rcStrict = pPciDev->Int.s.pfnConfigWrite(pPciDev->Int.s.CTX_SUFF(pDevIns), pPciDev,
                                                         pPciAddr->iRegister, cb, u32Value);
            if (rcStrict == VINF_PDM_PCI_DO_DEFAULT)
                rcStrict = devpciR3CommonConfigWriteWorker(pDevIns, PDMINS_2_DATA_CC(pDevIns, PDEVPCIBUSCC),
                                                           pPciDev, pPciAddr->iRegister, cb, u32Value);
            RT_NOREF(rcReschedule);
#else
            rcStrict = rcReschedule;
            RT_NOREF(pDevIns, u32Value, cb);
#endif
        }
    }

    Log2Func(("%02x:%02x.%u reg %x(%u) %x %Rrc\n", pPciAddr->iBus, pPciAddr->iDeviceFunc >> 3, pPciAddr->iDeviceFunc & 0x7,
              pPciAddr->iRegister, cb, u32Value, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,
 *      Port I/O Handler for PCI data OUT operations.}
 *
 * Emulates writes to Configuration Data Port at 0CFCh for Configuration
 * Mechanism \#1.
 */
static DECLCALLBACK(VBOXSTRICTRC)
ich9pciIOPortDataWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PDEVPCIROOT pThis = PDMINS_2_DATA(pDevIns, PDEVPCIROOT);
    LogFlowFunc(("offPort=%u u32=%#x cb=%d (config=%#10x)\n", offPort, u32, cb, pThis->uConfigReg));
    Assert(offPort < 4); NOREF(pvUser);

    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    if (!(offPort % cb))
    {
        PCI_LOCK_RET(pDevIns, VINF_IOM_R3_IOPORT_WRITE);

        if (pThis->uConfigReg & (1 << 31))
        {

            /* Decode target device from Configuration Address Port */
            PciAddress aPciAddr;
            ich9pciStateToPciAddr(pThis, offPort, &aPciAddr);

            /* Perform configuration space write */
            rcStrict = ich9pciConfigWrite(pDevIns, pThis, &aPciAddr, u32, cb, VINF_IOM_R3_IOPORT_WRITE);
        }

        PCI_UNLOCK(pDevIns);
    }
    else
        AssertMsgFailed(("Unaligned write to offPort=%u u32=%#x cb=%d\n", offPort, u32, cb));

    return rcStrict;
}


/**
 * Perform configuration space read.
 */
static VBOXSTRICTRC ich9pciConfigRead(PDEVPCIROOT pPciRoot, PciAddress* pPciAddr, unsigned cb, uint32_t *pu32Value, int rcReschedule)
{
    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
#ifdef IN_RING3
    NOREF(rcReschedule);
#else
    NOREF(cb);
#endif

    if (pPciAddr->iBus != 0)    /* forward to subordinate bus */
    {
        if (pPciRoot->PciBus.cBridges)
        {
#ifdef IN_RING3 /** @todo do lookup in R0/RC too! r=klaus don't think that it can work, since the config space access callback only works in R3 */
            PPDMPCIDEV pBridgeDevice = devpciR3FindBridge(&pPciRoot->PciBus, pPciAddr->iBus);
            if (pBridgeDevice)
            {
                AssertPtr(pBridgeDevice->Int.s.pfnBridgeConfigRead);
                rcStrict = pBridgeDevice->Int.s.pfnBridgeConfigRead(pBridgeDevice->Int.s.CTX_SUFF(pDevIns), pPciAddr->iBus,
                                                                    pPciAddr->iDeviceFunc, pPciAddr->iRegister, cb, pu32Value);
            }
            else
                *pu32Value = UINT32_MAX;
#else
            rcStrict = rcReschedule;
#endif
        }
        else
            *pu32Value = 0xffffffff;
    }
    else                    /* forward to directly connected device */
    {
        R3PTRTYPE(PDMPCIDEV *) pPciDev = pPciRoot->PciBus.apDevices[pPciAddr->iDeviceFunc];
        if (pPciDev)
        {
#ifdef IN_RING3
            rcStrict = VINF_PDM_PCI_DO_DEFAULT;
            if (pPciDev->Int.s.pfnConfigRead)
                rcStrict = pPciDev->Int.s.pfnConfigRead(pPciDev->Int.s.CTX_SUFF(pDevIns), pPciDev,
                                                        pPciAddr->iRegister, cb, pu32Value);
            if (rcStrict == VINF_PDM_PCI_DO_DEFAULT)
                rcStrict = devpciR3CommonConfigReadWorker(pPciDev, pPciAddr->iRegister, cb, pu32Value);
#else
            rcStrict = rcReschedule;
#endif
        }
        else
            *pu32Value = UINT32_MAX;
    }

    Log3Func(("%02x:%02x.%d reg %x(%d) gave %x %Rrc\n", pPciAddr->iBus, pPciAddr->iDeviceFunc >> 3, pPciAddr->iDeviceFunc & 0x7,
              pPciAddr->iRegister, cb, *pu32Value, VBOXSTRICTRC_VAL(rcStrict) ));
    return rcStrict;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,
 *      Port I/O Handler for PCI data IN operations.}
 *
 * Emulates reads from Configuration Data Port at 0CFCh for Configuration
 * Mechanism \#1.
 */
static DECLCALLBACK(VBOXSTRICTRC)
ich9pciIOPortDataRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    NOREF(pvUser);
    Assert(offPort < 4);
    if (!(offPort % cb))
    {
        PDEVPCIROOT pThis = PDMINS_2_DATA(pDevIns, PDEVPCIROOT);
        *pu32 = 0xffffffff;

        PCI_LOCK_RET(pDevIns, VINF_IOM_R3_IOPORT_READ);

        /* Configuration space mapping enabled? */
        VBOXSTRICTRC rcStrict;
        if (!(pThis->uConfigReg & (1 << 31)))
            rcStrict = VINF_SUCCESS;
        else
        {
            /* Decode target device and configuration space register */
            PciAddress aPciAddr;
            ich9pciStateToPciAddr(pThis, offPort, &aPciAddr);

            /* Perform configuration space read */
            rcStrict = ich9pciConfigRead(pThis, &aPciAddr, cb, pu32, VINF_IOM_R3_IOPORT_READ);
        }

        PCI_UNLOCK(pDevIns);

        LogFlowFunc(("offPort=%u cb=%#x (config=%#10x) -> %#x (%Rrc)\n", offPort, cb, *pu32, pThis->uConfigReg, VBOXSTRICTRC_VAL(rcStrict)));
        return rcStrict;
    }
    AssertMsgFailed(("Unaligned read from offPort=%u cb=%d\n", offPort, cb));
    return VERR_IOM_IOPORT_UNUSED;
}


/* Compute mapping of PCI slot and IRQ number to APIC interrupt line */
DECLINLINE(int) ich9pciSlot2ApicIrq(uint8_t uSlot, int irq_num)
{
    return (irq_num + uSlot) & 7;
}

#ifdef IN_RING3

/* return the global irq number corresponding to a given device irq
   pin. We could also use the bus number to have a more precise
   mapping. This is the implementation note described in the PCI spec chapter 2.2.6 */
DECLINLINE(int) ich9pciSlotGetPirq(uint8_t uBus, uint8_t uDevFn, uint8_t uIrqNum)
{
    NOREF(uBus);
    int iSlotAddend = (uDevFn >> 3) - 1;
    return (uIrqNum + iSlotAddend) & 3;
}

/* irqs corresponding to PCI irqs A-D, must match pci_irq_list in pcibios.inc */
/** @todo r=klaus inconsistent! ich9 doesn't implement PIRQ yet, so both needs to be addressed and tested thoroughly. */
static const uint8_t aPciIrqs[4] = { 11, 10, 9, 5 };

#endif /* IN_RING3 */

/* Add one more level up request on APIC input line */
DECLINLINE(void) ich9pciApicLevelUp(PDEVPCIROOT pPciRoot, int irq_num)
{
    ASMAtomicIncU32(&pPciRoot->auPciApicIrqLevels[irq_num]);
}

/* Remove one level up request on APIC input line */
DECLINLINE(void) ich9pciApicLevelDown(PDEVPCIROOT pPciRoot, int irq_num)
{
    ASMAtomicDecU32(&pPciRoot->auPciApicIrqLevels[irq_num]);
}

static void ich9pciApicSetIrq(PPDMDEVINS pDevIns, PDEVPCIBUS pBus, PDEVPCIBUSCC pBusCC,
                              uint8_t uDevFn, PDMPCIDEV *pPciDev, int irq_num1, int iLevel, uint32_t uTagSrc, int iForcedIrq)
{
    /* This is only allowed to be called with a pointer to the root bus. */
    AssertMsg(pBus->iBus == 0, ("iBus=%u\n", pBus->iBus));
    uint16_t const uBusDevFn = PCIBDF_MAKE(pBus->iBus, uDevFn);

    if (iForcedIrq == -1)
    {
        int apic_irq, apic_level;
        PDEVPCIROOT pPciRoot = DEVPCIBUS_2_DEVPCIROOT(pBus);
        int irq_num = ich9pciSlot2ApicIrq(uDevFn >> 3, irq_num1);

        if ((iLevel & PDM_IRQ_LEVEL_HIGH) == PDM_IRQ_LEVEL_HIGH)
            ich9pciApicLevelUp(pPciRoot, irq_num);
        else if ((iLevel & PDM_IRQ_LEVEL_HIGH) == PDM_IRQ_LEVEL_LOW)
            ich9pciApicLevelDown(pPciRoot, irq_num);

        apic_irq = irq_num + 0x10;
        apic_level = pPciRoot->auPciApicIrqLevels[irq_num] != 0;
        Log3Func(("%s: irq_num1=%d level=%d apic_irq=%d apic_level=%d irq_num1=%d uTagSrc=%#x\n",
                  R3STRING(pPciDev->pszNameR3), irq_num1, iLevel, apic_irq, apic_level, irq_num, uTagSrc));
        pBusCC->CTX_SUFF(pPciHlp)->pfnIoApicSetIrq(pDevIns, uBusDevFn, apic_irq, apic_level, uTagSrc);

        if ((iLevel & PDM_IRQ_LEVEL_FLIP_FLOP) == PDM_IRQ_LEVEL_FLIP_FLOP)
        {
            /*
             *  we raised it few lines above, as PDM_IRQ_LEVEL_FLIP_FLOP has
             * PDM_IRQ_LEVEL_HIGH bit set
             */
            ich9pciApicLevelDown(pPciRoot, irq_num);
            pPciDev->Int.s.uIrqPinState = PDM_IRQ_LEVEL_LOW;
            apic_level = pPciRoot->auPciApicIrqLevels[irq_num] != 0;
            Log3Func(("%s: irq_num1=%d level=%d apic_irq=%d apic_level=%d irq_num1=%d uTagSrc=%#x (flop)\n",
                      R3STRING(pPciDev->pszNameR3), irq_num1, iLevel, apic_irq, apic_level, irq_num, uTagSrc));
            pBusCC->CTX_SUFF(pPciHlp)->pfnIoApicSetIrq(pDevIns, uBusDevFn, apic_irq, apic_level, uTagSrc);
        }
    } else {
        Log3Func(("(forced) %s: irq_num1=%d level=%d acpi_irq=%d uTagSrc=%#x\n",
                  R3STRING(pPciDev->pszNameR3), irq_num1, iLevel, iForcedIrq, uTagSrc));
        pBusCC->CTX_SUFF(pPciHlp)->pfnIoApicSetIrq(pDevIns, uBusDevFn, iForcedIrq, iLevel, uTagSrc);
    }
}

static void ich9pciSetIrqInternal(PPDMDEVINS pDevIns, PDEVPCIROOT pPciRoot, PDEVPCIBUSCC pBusCC,
                                  uint8_t uDevFn, PPDMPCIDEV pPciDev, int iIrq, int iLevel, uint32_t uTagSrc)
{
    /* If MSI or MSI-X is enabled, PCI INTx# signals are disabled regardless of the PCI command
     * register interrupt bit state.
     * PCI 3.0 (section 6.8) forbids MSI and MSI-X to be enabled at the same time and makes
     * that undefined behavior. We check for MSI first, then MSI-X.
     */
    if (MsiIsEnabled(pPciDev))
    {
        Assert(!MsixIsEnabled(pPciDev));    /* Not allowed -- see note above. */
        LogFlowFunc(("PCI Dev %p : MSI\n", pPciDev));
        MsiNotify(pDevIns, pBusCC->CTX_SUFF(pPciHlp), pPciDev, iIrq, iLevel, uTagSrc);
        return;
    }

    if (MsixIsEnabled(pPciDev))
    {
        LogFlowFunc(("PCI Dev %p : MSI-X\n", pPciDev));
        MsixNotify(pDevIns, pBusCC->CTX_SUFF(pPciHlp), pPciDev, iIrq, iLevel, uTagSrc);
        return;
    }

    PDEVPCIBUS pBus = &pPciRoot->PciBus;
    /* safe, only needs to go to the config space array */
    const bool fIsAcpiDevice = PDMPciDevGetDeviceId(pPciDev) == 0x7113;

    LogFlowFunc(("PCI Dev %p : IRQ\n", pPciDev));
    /* Check if the state changed. */
    if (pPciDev->Int.s.uIrqPinState != iLevel)
    {
        pPciDev->Int.s.uIrqPinState = (iLevel & PDM_IRQ_LEVEL_HIGH);

        /** @todo r=klaus: implement PIRQ handling (if APIC isn't active). Needed for legacy OSes which don't use the APIC stuff. */

        /* Send interrupt to I/O APIC only now. */
        if (fIsAcpiDevice)
            /*
             * ACPI needs special treatment since SCI is hardwired and
             * should not be affected by PCI IRQ routing tables at the
             * same time SCI IRQ is shared in PCI sense hence this
             * kludge (i.e. we fetch the hardwired value from ACPIs
             * PCI device configuration space).
             */
            /* safe, only needs to go to the config space array */
            ich9pciApicSetIrq(pDevIns, pBus, pBusCC, uDevFn, pPciDev, -1, iLevel, uTagSrc, PDMPciDevGetInterruptLine(pPciDev));
        else
            ich9pciApicSetIrq(pDevIns, pBus, pBusCC, uDevFn, pPciDev, iIrq, iLevel, uTagSrc, -1);
    }
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE,
 * Emulates writes to configuration space.}
 */
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) devpciCommonMcfgMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PDEVPCIROOT pPciRoot = PDMINS_2_DATA(pDevIns, PDEVPCIROOT);
    Log2Func(("%RGp LB %d\n", off, cb));
    NOREF(pvUser);

    /* Decode target device and configuration space register */
    PciAddress aDest;
    ich9pciPhysToPciAddr(pPciRoot, off, &aDest);

    /* Get the value. */
    uint32_t u32;
    switch (cb)
    {
        case 1:
            u32 = *(uint8_t const *)pv;
            break;
        case 2:
            u32 = *(uint16_t const *)pv;
            break;
        case 4:
            u32 = *(uint32_t const *)pv;
            break;
        default:
            ASSERT_GUEST_MSG_FAILED(("cb=%u off=%RGp\n", cb, off)); /** @todo how the heck should this work? Split it, right? */
            u32 = 0;
            break;
    }

    /* Perform configuration space write */
    PCI_LOCK_RET(pDevIns, VINF_IOM_R3_MMIO_WRITE);
    VBOXSTRICTRC rcStrict = ich9pciConfigWrite(pDevIns, pPciRoot, &aDest, u32, cb, VINF_IOM_R3_MMIO_WRITE);
    PCI_UNLOCK(pDevIns);

    return rcStrict;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE,
 * Emulates reads from configuration space.}
 */
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) devpciCommonMcfgMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PDEVPCIROOT pPciRoot = PDMINS_2_DATA(pDevIns, PDEVPCIROOT);
    LogFlowFunc(("%RGp LB %u\n", off, cb));
    NOREF(pvUser);

    /* Decode target device and configuration space register */
    PciAddress aDest;
    ich9pciPhysToPciAddr(pPciRoot, off, &aDest);

    /* Perform configuration space read */
    uint32_t     u32Value = 0;
    PCI_LOCK_RET(pDevIns, VINF_IOM_R3_MMIO_READ);
    VBOXSTRICTRC rcStrict = ich9pciConfigRead(pPciRoot, &aDest, cb, &u32Value, VINF_IOM_R3_MMIO_READ);
    PCI_UNLOCK(pDevIns);

    if (RT_SUCCESS(rcStrict)) /** @todo this is wrong, though it probably works fine due to double buffering... */
    {
        switch (cb)
        {
            case 1:
                *(uint8_t *)pv   = (uint8_t)u32Value;
                break;
            case 2:
                *(uint16_t *)pv  = (uint16_t)u32Value;
                break;
            case 4:
                *(uint32_t *)pv  = u32Value;
                break;
            default:
                ASSERT_GUEST_MSG_FAILED(("cb=%u off=%RGp\n", cb, off)); /** @todo how the heck should this work? Split it, right? */
                rcStrict = VINF_IOM_MMIO_UNUSED_00;
                break;
        }
    }

    return VBOXSTRICTRC_TODO(rcStrict);
}

#ifdef IN_RING3

uint32_t devpciR3GetCfg(PPDMPCIDEV pPciDev, int32_t iRegister, int cb)
{
    uint32_t     u32Value = UINT32_MAX;
    VBOXSTRICTRC rcStrict = VINF_PDM_PCI_DO_DEFAULT;
    if (pPciDev->Int.s.pfnConfigRead)
        rcStrict = pPciDev->Int.s.pfnConfigRead(pPciDev->Int.s.CTX_SUFF(pDevIns), pPciDev, iRegister, cb, &u32Value);
    if (rcStrict == VINF_PDM_PCI_DO_DEFAULT)
        rcStrict = devpciR3CommonConfigReadWorker(pPciDev, iRegister, cb, &u32Value);
    AssertRCSuccess(VBOXSTRICTRC_VAL(rcStrict));
    return u32Value;
}

DECLINLINE(uint32_t) devpciGetRegionReg(int iRegion)
{
    return iRegion == VBOX_PCI_ROM_SLOT
         ?  VBOX_PCI_ROM_ADDRESS : (VBOX_PCI_BASE_ADDRESS_0 + iRegion * 4);
}

/**
 * Worker for devpciR3SetByte(), devpciR3SetWord() and devpciR3SetDWord(), also
 * used during state restore.
 */
void devpciR3SetCfg(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int32_t iRegister, uint32_t u32Value, int cb)
{
    Assert(cb <= 4 && cb != 3);
    VBOXSTRICTRC rcStrict = VINF_PDM_PCI_DO_DEFAULT;
    if (pPciDev->Int.s.pfnConfigWrite)
        rcStrict = pPciDev->Int.s.pfnConfigWrite(pPciDev->Int.s.CTX_SUFF(pDevIns), pPciDev, iRegister, cb, u32Value);
    if (rcStrict == VINF_PDM_PCI_DO_DEFAULT)
        rcStrict = devpciR3CommonConfigWriteWorker(pDevIns, PDMINS_2_DATA_CC(pDevIns, PDEVPCIBUSCC),
                                                   pPciDev, iRegister, cb, u32Value);
    AssertRCSuccess(VBOXSTRICTRC_VAL(rcStrict));
}


/* -=-=-=-=-=- PCI Bus Interface Methods (PDMPCIBUSREG) -=-=-=-=-=- */

/**
 * Search for a completely unused device entry (all 8 functions are unused).
 *
 * @returns VBox status code.
 * @param   pBus            The bus to register with.
 * @remarks Caller enters the PDM critical section.
 */
static uint8_t devpciR3CommonFindUnusedDeviceNo(PDEVPCIBUS pBus)
{
    for (uint8_t uPciDevNo = pBus->iDevSearch >> VBOX_PCI_DEVFN_DEV_SHIFT; uPciDevNo < VBOX_PCI_MAX_DEVICES; uPciDevNo++)
        if (   !pBus->apDevices[VBOX_PCI_DEVFN_MAKE(uPciDevNo, 0)]
            && !pBus->apDevices[VBOX_PCI_DEVFN_MAKE(uPciDevNo, 1)]
            && !pBus->apDevices[VBOX_PCI_DEVFN_MAKE(uPciDevNo, 2)]
            && !pBus->apDevices[VBOX_PCI_DEVFN_MAKE(uPciDevNo, 3)]
            && !pBus->apDevices[VBOX_PCI_DEVFN_MAKE(uPciDevNo, 4)]
            && !pBus->apDevices[VBOX_PCI_DEVFN_MAKE(uPciDevNo, 5)]
            && !pBus->apDevices[VBOX_PCI_DEVFN_MAKE(uPciDevNo, 6)]
            && !pBus->apDevices[VBOX_PCI_DEVFN_MAKE(uPciDevNo, 7)])
            return uPciDevNo;
    return UINT8_MAX;
}



/**
 * Registers the device with the specified PCI bus.
 *
 * This is shared between the pci bus and pci bridge code.
 *
 * @returns VBox status code.
 * @param   pDevIns         The PCI bus device instance.
 * @param   pBus            The bus to register with.
 * @param   pPciDev         The PCI device structure.
 * @param   fFlags          Reserved for future use, PDMPCIDEVREG_F_MBZ.
 * @param   uPciDevNo       PDMPCIDEVREG_DEV_NO_FIRST_UNUSED, or a specific
 *                          device number (0-31).
 * @param   uPciFunNo       PDMPCIDEVREG_FUN_NO_FIRST_UNUSED, or a specific
 *                          function number (0-7).
 * @param   pszName         Device name (static but not unique).
 *
 * @remarks Caller enters the PDM critical section.
 */
static int devpciR3CommonRegisterDeviceOnBus(PPDMDEVINS pDevIns, PDEVPCIBUS pBus, PPDMPCIDEV pPciDev, uint32_t fFlags,
                                             uint8_t uPciDevNo, uint8_t uPciFunNo, const char *pszName)
{
    RT_NOREF(pDevIns);

    /*
     * Validate input.
     */
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertPtrReturn(pPciDev, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~PDMPCIDEVREG_F_VALID_MASK), VERR_INVALID_FLAGS);
    AssertReturn(uPciDevNo < VBOX_PCI_MAX_DEVICES   || uPciDevNo == PDMPCIDEVREG_DEV_NO_FIRST_UNUSED, VERR_INVALID_PARAMETER);
    AssertReturn(uPciFunNo < VBOX_PCI_MAX_FUNCTIONS || uPciFunNo == PDMPCIDEVREG_FUN_NO_FIRST_UNUSED, VERR_INVALID_PARAMETER);

    /*
     * Assign device & function numbers.
     */

    /* Work the optional assignment flag. */
    if (fFlags & PDMPCIDEVREG_F_NOT_MANDATORY_NO)
    {
        AssertLogRelMsgReturn(uPciDevNo < VBOX_PCI_MAX_DEVICES && uPciFunNo < VBOX_PCI_MAX_FUNCTIONS,
                              ("PDMPCIDEVREG_F_NOT_MANDATORY_NO not implemented for #Dev=%#x / #Fun=%#x\n", uPciDevNo, uPciFunNo),
                              VERR_NOT_IMPLEMENTED);
        if (pBus->apDevices[VBOX_PCI_DEVFN_MAKE(uPciDevNo, uPciFunNo)])
        {
            uPciDevNo = PDMPCIDEVREG_DEV_NO_FIRST_UNUSED;
            uPciFunNo = PDMPCIDEVREG_FUN_NO_FIRST_UNUSED;
        }
    }

    if (uPciDevNo == PDMPCIDEVREG_DEV_NO_FIRST_UNUSED)
    {
        /* Just find the next unused device number and we're good. */
        uPciDevNo = devpciR3CommonFindUnusedDeviceNo(pBus);
        AssertLogRelMsgReturn(uPciDevNo < VBOX_PCI_MAX_DEVICES, ("Couldn't find a free spot!\n"), VERR_PDM_TOO_PCI_MANY_DEVICES);
        if (uPciFunNo == PDMPCIDEVREG_FUN_NO_FIRST_UNUSED)
            uPciFunNo = 0;
    }
    else
    {
        /*
         * Direct assignment of device number can be more complicated.
         */
        PPDMPCIDEV pClash;
        if (uPciFunNo != PDMPCIDEVREG_FUN_NO_FIRST_UNUSED)
        {
            /* In the case of a specified function, we only relocate an existing
               device if it belongs to a different device instance.  Reasoning is
               that the device should figure out it's own function assignments.
               Note! We could make this more flexible by relocating functions assigned
                     via PDMPCIDEVREG_FUN_NO_FIRST_UNUSED, but it can wait till it's needed. */
            pClash = pBus->apDevices[VBOX_PCI_DEVFN_MAKE(uPciDevNo, uPciFunNo)];
            if (!pClash)
            { /* likely */ }
            else if (pClash->Int.s.pDevInsR3 == pPciDev->Int.s.pDevInsR3)
                AssertLogRelMsgFailedReturn(("PCI Configuration conflict at %u.%u: %s vs %s (same pDevIns)!\n",
                                             uPciDevNo, uPciFunNo, pClash->pszNameR3, pszName),
                                            VERR_PDM_TOO_PCI_MANY_DEVICES);
            else if (!pClash->Int.s.fReassignableDevNo)
                AssertLogRelMsgFailedReturn(("PCI Configuration conflict at %u.%u: %s vs %s (different pDevIns)!\n",
                                             uPciDevNo, uPciFunNo, pClash->pszNameR3, pszName),
                                            VERR_PDM_TOO_PCI_MANY_DEVICES);
        }
        else
        {
            /* First unused function slot.  Again, we only relocate the whole
               thing if all the device instance differs, because we otherwise
               reason that a device should manage its own functions correctly. */
            unsigned cSameDevInses = 0;
            for (uPciFunNo = 0, pClash = NULL; uPciFunNo < VBOX_PCI_MAX_FUNCTIONS; uPciFunNo++)
            {
                pClash = pBus->apDevices[VBOX_PCI_DEVFN_MAKE(uPciDevNo, uPciFunNo)];
                if (!pClash)
                    break;
                cSameDevInses += pClash->Int.s.pDevInsR3 == pPciDev->Int.s.pDevInsR3;
            }
            if (!pClash)
                Assert(uPciFunNo < VBOX_PCI_MAX_FUNCTIONS);
            else
                AssertLogRelMsgReturn(cSameDevInses == 0,
                                      ("PCI Configuration conflict at %u.* appending %s (%u of %u pDevIns matches)!\n",
                                       uPciDevNo, pszName, cSameDevInses, VBOX_PCI_MAX_FUNCTIONS),
                                      VERR_PDM_TOO_PCI_MANY_DEVICES);
        }
        if (pClash)
        {
            /*
             * Try relocate the existing device.
             */
            /* Check that all functions can be moved. */
            for (uint8_t uMoveFun = 0; uMoveFun < VBOX_PCI_MAX_FUNCTIONS; uMoveFun++)
            {
                PPDMPCIDEV pMovePciDev = pBus->apDevices[VBOX_PCI_DEVFN_MAKE(uPciDevNo, uMoveFun)];
                AssertLogRelMsgReturn(!pMovePciDev || pMovePciDev->Int.s.fReassignableDevNo,
                                      ("PCI Configuration conflict at %u.%u: %s vs %s\n",
                                       uPciDevNo, uMoveFun, pMovePciDev->pszNameR3, pszName),
                                      VERR_PDM_TOO_PCI_MANY_DEVICES);
            }

            /* Find a free device number to move it to. */
            uint8_t uMoveToDevNo = devpciR3CommonFindUnusedDeviceNo(pBus);
            Assert(uMoveToDevNo != uPciFunNo);
            AssertLogRelMsgReturn(uMoveToDevNo < VBOX_PCI_MAX_DEVICES,
                                  ("No space to relocate device at %u so '%s' can be placed there instead!\n", uPciFunNo, pszName),
                                  VERR_PDM_TOO_PCI_MANY_DEVICES);

            /* Execute the move. */
            for (uint8_t uMoveFun = 0; uMoveFun < VBOX_PCI_MAX_FUNCTIONS; uMoveFun++)
            {
                PPDMPCIDEV pMovePciDev = pBus->apDevices[VBOX_PCI_DEVFN_MAKE(uPciDevNo, uMoveFun)];
                if (pMovePciDev)
                {
                    Log(("PCI: Relocating '%s' from %u.%u to %u.%u.\n", pMovePciDev->pszNameR3, uPciDevNo, uMoveFun, uMoveToDevNo, uMoveFun));
                    pBus->apDevices[VBOX_PCI_DEVFN_MAKE(uPciDevNo, uMoveFun)] = NULL;
                    pBus->apDevices[VBOX_PCI_DEVFN_MAKE(uMoveToDevNo, uMoveFun)] = pMovePciDev;
                    pMovePciDev->uDevFn = VBOX_PCI_DEVFN_MAKE(uMoveToDevNo, uMoveFun);
                }
            }
        }
    }

    /*
     * Now, initialize the rest of the PCI device structure.
     */
    Assert(!pBus->apDevices[VBOX_PCI_DEVFN_MAKE(uPciDevNo, uPciFunNo)]);
    pBus->apDevices[VBOX_PCI_DEVFN_MAKE(uPciDevNo, uPciFunNo)] = pPciDev;

    pPciDev->uDevFn                 = VBOX_PCI_DEVFN_MAKE(uPciDevNo, uPciFunNo);
    pPciDev->Int.s.pBusR3           = pBus;
    Assert(pBus == PDMINS_2_DATA(pDevIns, PDEVPCIBUS));
    pPciDev->Int.s.pfnConfigRead    = NULL;
    pPciDev->Int.s.pfnConfigWrite   = NULL;
    pPciDev->Int.s.hMmioMsix        = NIL_IOMMMIOHANDLE;
    if (pBus->enmType == DEVPCIBUSTYPE_PIIX3 && pPciDev->cbConfig > 256)
        pPciDev->cbConfig = 256;

    /* Remember and mark bridges. */
    if (fFlags & PDMPCIDEVREG_F_PCI_BRIDGE)
    {
        AssertLogRelMsgReturn(pBus->cBridges < RT_ELEMENTS(pBus->apDevices),
                              ("Number of bridges exceeds the number of possible devices on the bus\n"),
                              VERR_INTERNAL_ERROR_3);
        pBus->papBridgesR3[pBus->cBridges++] = pPciDev;
        pciDevSetPci2PciBridge(pPciDev);
    }

    Log(("PCI: Registered device %d function %d (%#x) '%s'.\n",
         uPciDevNo, uPciFunNo, UINT32_C(0x80000000) | (pPciDev->uDevFn << 8), pszName));

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMPCIBUSREGR3,pfnRegisterR3}
 */
DECLCALLBACK(int) devpciR3CommonRegisterDevice(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t fFlags,
                                               uint8_t uPciDevNo, uint8_t uPciFunNo, const char *pszName)
{
    PDEVPCIBUS pBus = PDMINS_2_DATA(pDevIns, PDEVPCIBUS);
    AssertCompileMemberOffset(DEVPCIROOT, PciBus, 0);
    return devpciR3CommonRegisterDeviceOnBus(pDevIns, pBus, pPciDev, fFlags, uPciDevNo, uPciFunNo, pszName);
}


/**
 * @interface_method_impl{PDMPCIBUSREGR3,pfnRegisterR3}
 */
DECLCALLBACK(int) devpcibridgeR3CommonRegisterDevice(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t fFlags,
                                                     uint8_t uPciDevNo, uint8_t uPciFunNo, const char *pszName)
{
    PDEVPCIBUS pBus = PDMINS_2_DATA(pDevIns, PDEVPCIBUS);
    return devpciR3CommonRegisterDeviceOnBus(pDevIns, pBus, pPciDev, fFlags, uPciDevNo, uPciFunNo, pszName);
}


static DECLCALLBACK(int) ich9pciRegisterMsi(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, PPDMMSIREG pMsiReg)
{
    //PDEVPCIBUS   pBus   = PDMINS_2_DATA(pDevIns, PDEVPCIBUS);
    PDEVPCIBUSCC pBusCC = PDMINS_2_DATA_CC(pDevIns, PDEVPCIBUSCC);

    int rc = MsiR3Init(pPciDev, pMsiReg);
    if (RT_SUCCESS(rc))
        rc = MsixR3Init(pBusCC->CTX_SUFF(pPciHlp), pPciDev, pMsiReg);

    return rc;
}


/**
 * @interface_method_impl{PDMPCIBUSREGR3,pfnIORegionRegisterR3}
 */
DECLCALLBACK(int) devpciR3CommonIORegionRegister(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                                 RTGCPHYS cbRegion, PCIADDRESSSPACE enmType, uint32_t fFlags,
                                                 uint64_t hHandle, PFNPCIIOREGIONMAP pfnMapUnmap)
{
    LogFunc(("%s: region #%u size %RGp type %x fFlags=%#x hHandle=%#RX64\n",
             pPciDev->pszNameR3, iRegion, cbRegion, enmType, fFlags, hHandle));
    RT_NOREF(pDevIns);

    /*
     * Validate.
     */
    AssertMsgReturn(   enmType == (PCI_ADDRESS_SPACE_MEM | PCI_ADDRESS_SPACE_BAR32)
                    || enmType == (PCI_ADDRESS_SPACE_MEM_PREFETCH | PCI_ADDRESS_SPACE_BAR32)
                    || enmType == (PCI_ADDRESS_SPACE_MEM | PCI_ADDRESS_SPACE_BAR64)
                    || enmType == (PCI_ADDRESS_SPACE_MEM_PREFETCH | PCI_ADDRESS_SPACE_BAR64)
                    || enmType ==  PCI_ADDRESS_SPACE_IO
                    ,
                    ("Invalid enmType=%#x? Or was this a bitmask after all...\n", enmType),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn((unsigned)iRegion < VBOX_PCI_NUM_REGIONS,
                    ("Invalid iRegion=%d VBOX_PCI_NUM_REGIONS=%d\n", iRegion, VBOX_PCI_NUM_REGIONS),
                    VERR_INVALID_PARAMETER);
    int iLastSet = ASMBitLastSetU64(cbRegion);
    AssertMsgReturn(    iLastSet != 0
                    &&  RT_BIT_64(iLastSet - 1) == cbRegion,
                    ("Invalid cbRegion=%RGp iLastSet=%#x (not a power of 2 or 0)\n", cbRegion, iLastSet),
                    VERR_INVALID_PARAMETER);
    switch (fFlags & PDMPCIDEV_IORGN_F_HANDLE_MASK)
    {
        case PDMPCIDEV_IORGN_F_IOPORT_HANDLE:
        case PDMPCIDEV_IORGN_F_MMIO_HANDLE:
        case PDMPCIDEV_IORGN_F_MMIO2_HANDLE:
            AssertReturn(hHandle != UINT64_MAX, VERR_INVALID_HANDLE);
            break;
        default:
            AssertReturn(hHandle == UINT64_MAX, VERR_INVALID_HANDLE);
    }

    /* Make sure that we haven't marked this region as continuation of 64-bit region. */
    AssertReturn(pPciDev->Int.s.aIORegions[iRegion].type != 0xff, VERR_NOT_AVAILABLE);

    /*
     * Register the I/O region.
     */
    PPCIIOREGION pRegion = &pPciDev->Int.s.aIORegions[iRegion];
    pRegion->addr        = INVALID_PCI_ADDRESS;
    pRegion->size        = cbRegion;
    pRegion->fFlags      = fFlags;
    pRegion->hHandle     = hHandle;
    pRegion->type        = enmType;
    pRegion->pfnMap      = pfnMapUnmap;

    if ((enmType & PCI_ADDRESS_SPACE_BAR64) != 0)
    {
        /* VBOX_PCI_BASE_ADDRESS_5 and VBOX_PCI_ROM_ADDRESS are excluded. */
        AssertMsgReturn(iRegion < VBOX_PCI_NUM_REGIONS - 2,
                        ("Region %d cannot be 64-bit\n", iRegion),
                        VERR_INVALID_PARAMETER);
        /* Mark next region as continuation of this one. */
        pPciDev->Int.s.aIORegions[iRegion + 1].type = 0xff;
    }

    /* Set type in the PCI config space. */
    AssertCompile(PCI_ADDRESS_SPACE_MEM          == 0);
    AssertCompile(PCI_ADDRESS_SPACE_IO           == 1);
    AssertCompile(PCI_ADDRESS_SPACE_BAR64        == RT_BIT_32(2));
    AssertCompile(PCI_ADDRESS_SPACE_MEM_PREFETCH == RT_BIT_32(3));
    uint32_t u32Value   = (uint32_t)enmType & (PCI_ADDRESS_SPACE_IO | PCI_ADDRESS_SPACE_BAR64 | PCI_ADDRESS_SPACE_MEM_PREFETCH);
    /* safe, only needs to go to the config space array */
    PDMPciDevSetDWord(pPciDev, devpciGetRegionReg(iRegion), u32Value);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMPCIBUSREGR3,pfnInterceptConfigAccesses}
 */
DECLCALLBACK(void) devpciR3CommonInterceptConfigAccesses(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                         PFNPCICONFIGREAD pfnRead, PFNPCICONFIGWRITE pfnWrite)
{
    NOREF(pDevIns);

    pPciDev->Int.s.pfnConfigRead  = pfnRead;
    pPciDev->Int.s.pfnConfigWrite = pfnWrite;
}


static int ich9pciR3CommonSaveExec(PCPDMDEVHLPR3 pHlp, PDEVPCIBUS pBus, PSSMHANDLE pSSM)
{
    /*
     * Iterate thru all the devices.
     */
    for (uint32_t uDevFn = 0; uDevFn < RT_ELEMENTS(pBus->apDevices); uDevFn++)
    {
        PPDMPCIDEV pDev = pBus->apDevices[uDevFn];
        if (pDev)
        {
            /* Device position */
            pHlp->pfnSSMPutU32(pSSM, uDevFn);

            /* PCI config registers */
            pHlp->pfnSSMPutU32(pSSM, sizeof(pDev->abConfig));
            pHlp->pfnSSMPutMem(pSSM, pDev->abConfig, sizeof(pDev->abConfig));

            /* Device flags */
            pHlp->pfnSSMPutU32(pSSM, pDev->Int.s.fFlags);

            /* IRQ pin state */
            pHlp->pfnSSMPutS32(pSSM, pDev->Int.s.uIrqPinState);

            /* MSI info */
            pHlp->pfnSSMPutU8(pSSM, pDev->Int.s.u8MsiCapOffset);
            pHlp->pfnSSMPutU8(pSSM, pDev->Int.s.u8MsiCapSize);

            /* MSI-X info */
            pHlp->pfnSSMPutU8(pSSM, pDev->Int.s.u8MsixCapOffset);
            pHlp->pfnSSMPutU8(pSSM, pDev->Int.s.u8MsixCapSize);

            /* Save MSI-X page state */
            if (pDev->Int.s.u8MsixCapOffset != 0)
            {
                pHlp->pfnSSMPutU32(pSSM, pDev->Int.s.cbMsixRegion);
                pHlp->pfnSSMPutMem(pSSM, pDev->abMsixState, pDev->Int.s.cbMsixRegion);
            }
            else
                pHlp->pfnSSMPutU32(pSSM, 0);

            /* Save the type an size of all the regions. */
            for (uint32_t iRegion = 0; iRegion < VBOX_PCI_NUM_REGIONS; iRegion++)
            {
                pHlp->pfnSSMPutU8(pSSM, pDev->Int.s.aIORegions[iRegion].type);
                pHlp->pfnSSMPutU64(pSSM, pDev->Int.s.aIORegions[iRegion].size);
            }
        }
    }
    return pHlp->pfnSSMPutU32(pSSM, UINT32_MAX); /* terminator */
}

DECL_HIDDEN_CALLBACK(int) devpciR3CommonSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PDEVPCIROOT     pThis = PDMINS_2_DATA(pDevIns, PDEVPCIROOT);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    /*
     * Bus state data.
     */
    pHlp->pfnSSMPutU32(pSSM, pThis->uConfigReg);

    /*
     * Save IRQ states.
     */
    if (pThis->PciBus.enmType == DEVPCIBUSTYPE_ICH9)
    {
        for (unsigned i = 0; i < RT_ELEMENTS(pThis->auPciApicIrqLevels); i++)
            pHlp->pfnSSMPutU32(pSSM, pThis->auPciApicIrqLevels[i]);
    }
    else if (pThis->PciBus.enmType == DEVPCIBUSTYPE_GENERIC_ECAM)
    {
        for (unsigned i = 0; i < RT_ELEMENTS(pThis->u.GenericEcam.auPciIrqLevels); i++)
            pHlp->pfnSSMPutU32(pSSM, pThis->u.GenericEcam.auPciIrqLevels[i]);
    }
    else
        AssertReleaseFailed();

    pHlp->pfnSSMPutU32(pSSM, UINT32_MAX);  /* separator */

    return ich9pciR3CommonSaveExec(pHlp, &pThis->PciBus, pSSM);
}


DECL_HIDDEN_CALLBACK(int) devpciR3BridgeCommonSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PDEVPCIBUS      pThis = PDMINS_2_DATA(pDevIns, PDEVPCIBUS);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    return ich9pciR3CommonSaveExec(pHlp, pThis, pSSM);
}


/**
 * @callback_method_impl{FNPCIBRIDGECONFIGWRITE}
 */
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) devpciR3BridgeCommonConfigWrite(PPDMDEVINSR3 pDevIns, uint8_t iBus, uint8_t iDevice,
                                                                   uint32_t u32Address, unsigned cb, uint32_t u32Value)
{
    PDEVPCIBUS   pBus     = PDMINS_2_DATA(pDevIns, PDEVPCIBUS);
    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    LogFlowFunc(("pDevIns=%p iBus=%d iDevice=%d u32Address=%#x cb=%d u32Value=%#x\n", pDevIns, iBus, iDevice, u32Address, cb, u32Value));

    /* If the current bus is not the target bus search for the bus which contains the device. */
    /* safe, only needs to go to the config space array */
    if (iBus != PDMPciDevGetByte(pDevIns->apPciDevs[0], VBOX_PCI_SECONDARY_BUS))
    {
        PPDMPCIDEV pBridgeDevice = devpciR3FindBridge(pBus, iBus);
        if (pBridgeDevice)
        {
            AssertPtr(pBridgeDevice->Int.s.pfnBridgeConfigWrite);
            pBridgeDevice->Int.s.pfnBridgeConfigWrite(pBridgeDevice->Int.s.CTX_SUFF(pDevIns), iBus, iDevice,
                                                      u32Address, cb, u32Value);
        }
    }
    else
    {
        /* This is the target bus, pass the write to the device. */
        PPDMPCIDEV pPciDev = pBus->apDevices[iDevice];
        if (pPciDev)
        {
            LogFunc(("%s: addr=%02x val=%08x len=%d\n", pPciDev->pszNameR3, u32Address, u32Value, cb));
            rcStrict = VINF_PDM_PCI_DO_DEFAULT;
            if (pPciDev->Int.s.pfnConfigWrite)
                rcStrict = pPciDev->Int.s.pfnConfigWrite(pPciDev->Int.s.CTX_SUFF(pDevIns), pPciDev, u32Address, cb, u32Value);
            if (rcStrict == VINF_PDM_PCI_DO_DEFAULT)
                rcStrict = devpciR3CommonConfigWriteWorker(pDevIns, PDMINS_2_DATA_CC(pDevIns, PDEVPCIBUSCC),
                                                           pPciDev, u32Address, cb, u32Value);
        }
    }
    return rcStrict;
}

/**
 * @callback_method_impl{FNPCIBRIDGECONFIGREAD}
 */
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) devpciR3BridgeCommonConfigRead(PPDMDEVINSR3 pDevIns, uint8_t iBus, uint8_t iDevice,
                                                                  uint32_t u32Address, unsigned cb, uint32_t *pu32Value)
{
    PDEVPCIBUS   pBus     = PDMINS_2_DATA(pDevIns, PDEVPCIBUS);
    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    LogFlowFunc(("pDevIns=%p iBus=%d iDevice=%d u32Address=%#x cb=%d\n", pDevIns, iBus, iDevice, u32Address, cb));

    /* If the current bus is not the target bus search for the bus which contains the device. */
    /* safe, only needs to go to the config space array */
    if (iBus != PDMPciDevGetByte(pDevIns->apPciDevs[0], VBOX_PCI_SECONDARY_BUS))
    {
        PPDMPCIDEV pBridgeDevice = devpciR3FindBridge(pBus, iBus);
        if (pBridgeDevice)
        {
            AssertPtr(pBridgeDevice->Int.s.pfnBridgeConfigRead);
            rcStrict = pBridgeDevice->Int.s.pfnBridgeConfigRead(pBridgeDevice->Int.s.CTX_SUFF(pDevIns), iBus, iDevice,
                                                                u32Address, cb, pu32Value);
        }
        else
            *pu32Value = UINT32_MAX;
    }
    else
    {
        /* This is the target bus, pass the read to the device. */
        PPDMPCIDEV pPciDev = pBus->apDevices[iDevice];
        if (pPciDev)
        {
            rcStrict = VINF_PDM_PCI_DO_DEFAULT;
            if (pPciDev->Int.s.pfnConfigRead)
                rcStrict = pPciDev->Int.s.pfnConfigRead(pPciDev->Int.s.CTX_SUFF(pDevIns), pPciDev, u32Address, cb, pu32Value);
            if (rcStrict == VINF_PDM_PCI_DO_DEFAULT)
                rcStrict = devpciR3CommonConfigReadWorker(pPciDev, u32Address, cb, pu32Value);
            LogFunc(("%s: u32Address=%02x *pu32Value=%#010x cb=%d\n", pPciDev->pszNameR3, u32Address, *pu32Value, cb));
        }
        else
            *pu32Value = UINT32_MAX;
    }

    return rcStrict;
}



/* -=-=-=-=-=- Saved State -=-=-=-=-=- */


/**
 * Common routine for restoring the config registers of a PCI device.
 *
 * @param   pDevIns             The device instance of the PC bus.
 * @param   pDev                The PCI device.
 * @param   pbSrcConfig         The configuration register values to be loaded.
 */
void devpciR3CommonRestoreConfig(PPDMDEVINS pDevIns, PPDMPCIDEV pDev, uint8_t const *pbSrcConfig)
{
    /*
     * This table defines the fields for normal devices and bridge devices, and
     * the order in which they need to be restored.
     */
    static const struct PciField
    {
        uint8_t     off;
        uint8_t     cb;
        uint8_t     fWritable;
        uint8_t     fBridge;
        const char *pszName;
    } s_aFields[] =
    {
        /* off,cb,fW,fB, pszName */
        { 0x00, 2, 0, 3, "VENDOR_ID" },
        { 0x02, 2, 0, 3, "DEVICE_ID" },
        { 0x06, 2, 1, 3, "STATUS" },
        { 0x08, 1, 0, 3, "REVISION_ID" },
        { 0x09, 1, 0, 3, "CLASS_PROG" },
        { 0x0a, 1, 0, 3, "CLASS_SUB" },
        { 0x0b, 1, 0, 3, "CLASS_BASE" },
        { 0x0c, 1, 1, 3, "CACHE_LINE_SIZE" },
        { 0x0d, 1, 1, 3, "LATENCY_TIMER" },
        { 0x0e, 1, 0, 3, "HEADER_TYPE" },
        { 0x0f, 1, 1, 3, "BIST" },
        { 0x10, 4, 1, 3, "BASE_ADDRESS_0" },
        { 0x14, 4, 1, 3, "BASE_ADDRESS_1" },
        { 0x18, 4, 1, 1, "BASE_ADDRESS_2" },
        { 0x18, 1, 1, 2, "PRIMARY_BUS" },
        { 0x19, 1, 1, 2, "SECONDARY_BUS" },
        { 0x1a, 1, 1, 2, "SUBORDINATE_BUS" },
        { 0x1b, 1, 1, 2, "SEC_LATENCY_TIMER" },
        { 0x1c, 4, 1, 1, "BASE_ADDRESS_3" },
        { 0x1c, 1, 1, 2, "IO_BASE" },
        { 0x1d, 1, 1, 2, "IO_LIMIT" },
        { 0x1e, 2, 1, 2, "SEC_STATUS" },
        { 0x20, 4, 1, 1, "BASE_ADDRESS_4" },
        { 0x20, 2, 1, 2, "MEMORY_BASE" },
        { 0x22, 2, 1, 2, "MEMORY_LIMIT" },
        { 0x24, 4, 1, 1, "BASE_ADDRESS_5" },
        { 0x24, 2, 1, 2, "PREF_MEMORY_BASE" },
        { 0x26, 2, 1, 2, "PREF_MEMORY_LIMIT" },
        { 0x28, 4, 0, 1, "CARDBUS_CIS" },
        { 0x28, 4, 1, 2, "PREF_BASE_UPPER32" },
        { 0x2c, 2, 0, 1, "SUBSYSTEM_VENDOR_ID" },
        { 0x2c, 4, 1, 2, "PREF_LIMIT_UPPER32" },
        { 0x2e, 2, 0, 1, "SUBSYSTEM_ID" },
        { 0x30, 4, 1, 1, "ROM_ADDRESS" },
        { 0x30, 2, 1, 2, "IO_BASE_UPPER16" },
        { 0x32, 2, 1, 2, "IO_LIMIT_UPPER16" },
        { 0x34, 4, 0, 3, "CAPABILITY_LIST" },
        { 0x38, 4, 1, 1, "RESERVED_38" },
        { 0x38, 4, 1, 2, "ROM_ADDRESS_BR" },
        { 0x3c, 1, 1, 3, "INTERRUPT_LINE" },
        { 0x3d, 1, 0, 3, "INTERRUPT_PIN" },
        { 0x3e, 1, 0, 1, "MIN_GNT" },
        { 0x3e, 2, 1, 2, "BRIDGE_CONTROL" },
        { 0x3f, 1, 0, 1, "MAX_LAT" },
        /* The COMMAND register must come last as it requires the *ADDRESS*
           registers to be restored before we pretent to change it from 0 to
           whatever value the guest assigned it. */
        { 0x04, 2, 1, 3, "COMMAND" },
    };

#ifdef RT_STRICT
    /* Check that we've got full register coverage. */
    uint32_t bmDevice[0x40 / 32];
    uint32_t bmBridge[0x40 / 32];
    RT_ZERO(bmDevice);
    RT_ZERO(bmBridge);
    for (uint32_t i = 0; i < RT_ELEMENTS(s_aFields); i++)
    {
        uint8_t off = s_aFields[i].off;
        uint8_t cb  = s_aFields[i].cb;
        uint8_t f   = s_aFields[i].fBridge;
        while (cb-- > 0)
        {
            if (f & 1) AssertMsg(!ASMBitTest(bmDevice, off), ("%#x\n", off));
            if (f & 2) AssertMsg(!ASMBitTest(bmBridge, off), ("%#x\n", off));
            if (f & 1) ASMBitSet(bmDevice, off);
            if (f & 2) ASMBitSet(bmBridge, off);
            off++;
        }
    }
    for (uint32_t off = 0; off < 0x40; off++)
    {
        AssertMsg(ASMBitTest(bmDevice, off), ("%#x\n", off));
        AssertMsg(ASMBitTest(bmBridge, off), ("%#x\n", off));
    }
#endif

    /*
     * Loop thru the fields covering the 64 bytes of standard registers.
     */
    uint8_t const fBridge = pciDevIsPci2PciBridge(pDev) ? 2 : 1;
    Assert(!pciDevIsPassthrough(pDev));
    uint8_t *pbDstConfig = &pDev->abConfig[0];

    for (uint32_t i = 0; i < RT_ELEMENTS(s_aFields); i++)
        if (s_aFields[i].fBridge & fBridge)
        {
            uint8_t const   off = s_aFields[i].off;
            uint8_t const   cb  = s_aFields[i].cb;
            uint32_t        u32Src;
            uint32_t        u32Dst;
            switch (cb)
            {
                case 1:
                    u32Src = pbSrcConfig[off];
                    u32Dst = pbDstConfig[off];
                    break;
                case 2:
                    u32Src = *(uint16_t const *)&pbSrcConfig[off];
                    u32Dst = *(uint16_t const *)&pbDstConfig[off];
                    break;
                case 4:
                    u32Src = *(uint32_t const *)&pbSrcConfig[off];
                    u32Dst = *(uint32_t const *)&pbDstConfig[off];
                    break;
                default:
                    AssertFailed();
                    continue;
            }

            if (    u32Src != u32Dst
                ||  off == VBOX_PCI_COMMAND)
            {
                if (u32Src != u32Dst)
                {
                    if (!s_aFields[i].fWritable)
                        LogRel(("PCI: %8s/%u: %2u-bit field %s: %x -> %x - !READ ONLY!\n",
                                pDev->pszNameR3, pDev->Int.s.CTX_SUFF(pDevIns)->iInstance, cb*8, s_aFields[i].pszName, u32Dst, u32Src));
                    else
                        LogRel(("PCI: %8s/%u: %2u-bit field %s: %x -> %x\n",
                                pDev->pszNameR3, pDev->Int.s.CTX_SUFF(pDevIns)->iInstance, cb*8, s_aFields[i].pszName, u32Dst, u32Src));
                }
                if (off == VBOX_PCI_COMMAND)
                    /* safe, only needs to go to the config space array */
                    PDMPciDevSetCommand(pDev, 0); /* For remapping, see pciR3CommonLoadExec/ich9pciR3CommonLoadExec. */
                devpciR3SetCfg(pDevIns, pDev, off, u32Src, cb);
            }
        }

    /*
     * The device dependent registers.
     *
     * We will not use ConfigWrite here as we have no clue about the size
     * of the registers, so the device is responsible for correctly
     * restoring functionality governed by these registers.
     */
    for (uint32_t off = 0x40; off < sizeof(pDev->abConfig); off++)
        if (pbDstConfig[off] != pbSrcConfig[off])
        {
            LogRel(("PCI: %8s/%u: register %02x: %02x -> %02x\n",
                    pDev->pszNameR3, pDev->Int.s.CTX_SUFF(pDevIns)->iInstance, off, pbDstConfig[off], pbSrcConfig[off])); /** @todo make this Log() later. */
            pbDstConfig[off] = pbSrcConfig[off];
        }
}


/**
 * @callback_method_impl{FNPCIIOREGIONOLDSETTER}
 */
static DECLCALLBACK(int) devpciR3CommonRestoreOldSetRegion(PPDMPCIDEV pPciDev, uint32_t iRegion,
                                                           RTGCPHYS cbRegion, PCIADDRESSSPACE enmType)
{
    AssertLogRelReturn(iRegion < RT_ELEMENTS(pPciDev->Int.s.aIORegions), VERR_INVALID_PARAMETER);
    pPciDev->Int.s.aIORegions[iRegion].type = enmType;
    pPciDev->Int.s.aIORegions[iRegion].size = cbRegion;
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNPCIIOREGIONSWAP}
 */
static DECLCALLBACK(int) devpciR3CommonRestoreSwapRegions(PPDMPCIDEV pPciDev, uint32_t iRegion, uint32_t iOtherRegion)
{
    AssertReturn(iRegion < iOtherRegion, VERR_INVALID_PARAMETER);
    AssertLogRelReturn(iOtherRegion < RT_ELEMENTS(pPciDev->Int.s.aIORegions), VERR_INVALID_PARAMETER);
    AssertReturn(pPciDev->Int.s.bPadding0 == (0xe0 | (uint8_t)iRegion), VERR_INVALID_PARAMETER);

    PCIIOREGION Tmp = pPciDev->Int.s.aIORegions[iRegion];
    pPciDev->Int.s.aIORegions[iRegion] = pPciDev->Int.s.aIORegions[iOtherRegion];
    pPciDev->Int.s.aIORegions[iOtherRegion] = Tmp;

    return VINF_SUCCESS;
}


/**
 * Checks for and deals with changes in resource sizes and types.
 *
 * @returns VBox status code.
 * @param   pHlp                The device instance helper callback table.
 * @param   pSSM                The Saved state handle.
 * @param   pPciDev             The PCI device in question.
 * @param   paIoRegions         I/O regions with the size and type fields from
 *                              the saved state.
 * @param   fNewState           Set if this is a new state with I/O region sizes
 *                              and types, clear if old one.
 */
int devpciR3CommonRestoreRegions(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM, PPDMPCIDEV pPciDev, PPCIIOREGION paIoRegions, bool fNewState)
{
    int rc;
    if (fNewState)
    {
        for (uint32_t iRegion = 0; iRegion < VBOX_PCI_NUM_REGIONS; iRegion++)
        {
            if (   pPciDev->Int.s.aIORegions[iRegion].type != paIoRegions[iRegion].type
                || pPciDev->Int.s.aIORegions[iRegion].size != paIoRegions[iRegion].size)
            {
                AssertLogRelMsgFailed(("PCI: %8s/%u: region #%u size/type load change: %#RGp/%#x -> %#RGp/%#x\n",
                                       pPciDev->pszNameR3, pPciDev->Int.s.CTX_SUFF(pDevIns)->iInstance, iRegion,
                                       pPciDev->Int.s.aIORegions[iRegion].size, pPciDev->Int.s.aIORegions[iRegion].type,
                                       paIoRegions[iRegion].size, paIoRegions[iRegion].type));
                if (pPciDev->pfnRegionLoadChangeHookR3)
                {
                    pPciDev->Int.s.bPadding0 = 0xe0 | (uint8_t)iRegion;
                    rc = pPciDev->pfnRegionLoadChangeHookR3(pPciDev->Int.s.pDevInsR3, pPciDev, iRegion, paIoRegions[iRegion].size,
                                                            (PCIADDRESSSPACE)paIoRegions[iRegion].type, NULL /*pfnOldSetter*/,
                                                            devpciR3CommonRestoreSwapRegions);
                    pPciDev->Int.s.bPadding0 = 0;
                    if (RT_FAILURE(rc))
                        return pHlp->pfnSSMSetLoadError(pSSM, rc, RT_SRC_POS,
                                                        N_("Device %s/%u failed to respond to region #%u size/type changing from %#RGp/%#x to %#RGp/%#x: %Rrc"),
                                                        pPciDev->pszNameR3, pPciDev->Int.s.CTX_SUFF(pDevIns)->iInstance, iRegion,
                                                        pPciDev->Int.s.aIORegions[iRegion].size, pPciDev->Int.s.aIORegions[iRegion].type,
                                                        paIoRegions[iRegion].size, paIoRegions[iRegion].type, rc);
                }
                pPciDev->Int.s.aIORegions[iRegion].type = paIoRegions[iRegion].type;
                pPciDev->Int.s.aIORegions[iRegion].size = paIoRegions[iRegion].size;
            }
        }
    }
    /* Old saved state without sizes and types.  Do a special hook call to give
       devices with changes a chance to adjust resources back to old values. */
    else if (pPciDev->pfnRegionLoadChangeHookR3)
    {
        rc = pPciDev->pfnRegionLoadChangeHookR3(pPciDev->Int.s.pDevInsR3, pPciDev, UINT32_MAX, RTGCPHYS_MAX, (PCIADDRESSSPACE)-1,
                                                devpciR3CommonRestoreOldSetRegion, NULL);
        if (RT_FAILURE(rc))
            return pHlp->pfnSSMSetLoadError(pSSM, rc, RT_SRC_POS,  N_("Device %s/%u failed to resize its resources: %Rrc"),
                                            pPciDev->pszNameR3, pPciDev->Int.s.CTX_SUFF(pDevIns)->iInstance, rc);
    }
    return VINF_SUCCESS;
}


/**
 * Common worker for ich9pciR3LoadExec and ich9pcibridgeR3LoadExec.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance of the bus.
 * @param   pBus                The bus which data is being loaded.
 * @param   pSSM                The saved state handle.
 * @param   uVersion            The data version.
 * @param   uPass               The pass.
 */
static int ich9pciR3CommonLoadExec(PPDMDEVINS pDevIns, PDEVPCIBUS pBus, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PCPDMDEVHLPR3   pHlp = pDevIns->pHlpR3;
    uint32_t     u32;
    int          rc;

    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);
    if (   uVersion < VBOX_ICH9PCI_SAVED_STATE_VERSION_MSI
        || uVersion > VBOX_ICH9PCI_SAVED_STATE_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /*
     * Iterate thru all the devices and write 0 to the COMMAND register so
     * that all the memory is unmapped before we start restoring the saved
     * mapping locations.
     *
     * The register value is restored afterwards so we can do proper
     * LogRels in devpciR3CommonRestoreConfig.
     */
    for (uint32_t uDevFn = 0; uDevFn < RT_ELEMENTS(pBus->apDevices); uDevFn++)
    {
        PPDMPCIDEV pDev = pBus->apDevices[uDevFn];
        if (pDev)
        {
            /* safe, only needs to go to the config space array */
            uint16_t u16 = PDMPciDevGetCommand(pDev);
            devpciR3SetCfg(pDevIns, pDev, VBOX_PCI_COMMAND, 0 /*u32Value*/, 2 /*cb*/);
            /* safe, only needs to go to the config space array */
            PDMPciDevSetCommand(pDev, u16);
            /* safe, only needs to go to the config space array */
            Assert(PDMPciDevGetCommand(pDev) == u16);
        }
    }

    /*
     * Iterate all the devices.
     */
    for (uint32_t uDevFn = 0;; uDevFn++)
    {
        /* index / terminator */
        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        if (RT_FAILURE(rc))
            break;
        if (u32 == (uint32_t)~0)
            break;
        AssertLogRelMsgBreak(u32 < RT_ELEMENTS(pBus->apDevices) && u32 >= uDevFn, ("u32=%#x uDevFn=%#x\n", u32, uDevFn));

        /* skip forward to the device checking that no new devices are present. */
        PPDMPCIDEV pDev;
        for (; uDevFn < u32; uDevFn++)
        {
            pDev = pBus->apDevices[uDevFn];
            if (pDev)
            {
                /* safe, only needs to go to the config space array */
                LogRel(("PCI: New device in slot %#x, %s (vendor=%#06x device=%#06x)\n", uDevFn, pDev->pszNameR3,
                        PDMPciDevGetVendorId(pDev), PDMPciDevGetDeviceId(pDev)));
                if (pHlp->pfnSSMHandleGetAfter(pSSM) != SSMAFTER_DEBUG_IT)
                {
                    /* safe, only needs to go to the config space array */
                    rc = pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("New device in slot %#x, %s (vendor=%#06x device=%#06x)"),
                                                 uDevFn, pDev->pszNameR3, PDMPciDevGetVendorId(pDev), PDMPciDevGetDeviceId(pDev));
                    break;
                }
            }
        }
        if (RT_FAILURE(rc))
            break;
        pDev = pBus->apDevices[uDevFn];

        /* get the data */
        union
        {
            PDMPCIDEV DevTmp;
            uint8_t   abPadding[RT_UOFFSETOF(PDMPCIDEV, abMsixState) + _32K + _16K]; /* the MSI-X state shouldn't be much more than 32KB. */
        } u;
        RT_ZERO(u);
        u.DevTmp.Int.s.fFlags = 0;
        u.DevTmp.Int.s.u8MsiCapOffset = 0;
        u.DevTmp.Int.s.u8MsiCapSize = 0;
        u.DevTmp.Int.s.u8MsixCapOffset = 0;
        u.DevTmp.Int.s.u8MsixCapSize = 0;
        u.DevTmp.Int.s.uIrqPinState = ~0; /* Invalid value in case we have an older saved state to force a state change in pciSetIrq. */
        uint32_t cbConfig = 256;
        if (uVersion >= VBOX_ICH9PCI_SAVED_STATE_VERSION_4KB_CFG_SPACE)
        {
            rc = pHlp->pfnSSMGetU32(pSSM, &cbConfig);
            AssertRCReturn(rc, rc);
            if (cbConfig != 256 && cbConfig != _4K)
                return pHlp->pfnSSMSetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                                "cbConfig=%#RX32, expected 0x100 or 0x1000", cbConfig);
        }
        pHlp->pfnSSMGetMem(pSSM, u.DevTmp.abConfig, cbConfig);

        pHlp->pfnSSMGetU32(pSSM, &u.DevTmp.Int.s.fFlags);
        pHlp->pfnSSMGetS32(pSSM, &u.DevTmp.Int.s.uIrqPinState);
        pHlp->pfnSSMGetU8(pSSM, &u.DevTmp.Int.s.u8MsiCapOffset);
        pHlp->pfnSSMGetU8(pSSM, &u.DevTmp.Int.s.u8MsiCapSize);
        pHlp->pfnSSMGetU8(pSSM, &u.DevTmp.Int.s.u8MsixCapOffset);
        rc = pHlp->pfnSSMGetU8(pSSM, &u.DevTmp.Int.s.u8MsixCapSize);
        AssertRCReturn(rc, rc);

        /* Load MSI-X page state */
        uint32_t cbMsixState = u.DevTmp.Int.s.u8MsixCapOffset != 0 ? _4K : 0;
        if (uVersion >= VBOX_ICH9PCI_SAVED_STATE_VERSION_4KB_CFG_SPACE)
        {
            rc = pHlp->pfnSSMGetU32(pSSM, &cbMsixState);
            AssertRCReturn(rc, rc);
        }
        if (cbMsixState)
        {
            if (   cbMsixState > (uint32_t)(pDev ? pDev->cbMsixState : _32K + _16K)
                || cbMsixState > sizeof(u) - RT_UOFFSETOF(PDMPCIDEV, abMsixState))
                return pHlp->pfnSSMSetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                                "cbMsixState=%#RX32, expected at most RT_MIN(%#x, %#zx)",
                                                cbMsixState, (pDev ? pDev->cbMsixState : _32K + _16K),
                                                sizeof(u) - RT_UOFFSETOF(PDMPCIDEV, abMsixState));
            rc = pHlp->pfnSSMGetMem(pSSM, u.DevTmp.abMsixState, cbMsixState);
            AssertRCReturn(rc, rc);
        }

        /* Load the region types and sizes. */
        if (uVersion >= VBOX_ICH9PCI_SAVED_STATE_VERSION_REGION_SIZES)
        {
            for (uint32_t iRegion = 0; iRegion < VBOX_PCI_NUM_REGIONS; iRegion++)
            {
                pHlp->pfnSSMGetU8(pSSM, &u.DevTmp.Int.s.aIORegions[iRegion].type);
                rc = pHlp->pfnSSMGetU64(pSSM, &u.DevTmp.Int.s.aIORegions[iRegion].size);
                AssertLogRelRCReturn(rc, rc);
            }
        }

        /*
         * Check that it's still around.
         */
        pDev = pBus->apDevices[uDevFn];
        if (!pDev)
        {
            /* safe, only needs to go to the config space array */
            LogRel(("PCI: Device in slot %#x has been removed! vendor=%#06x device=%#06x\n", uDevFn,
                    PDMPciDevGetVendorId(&u.DevTmp), PDMPciDevGetDeviceId(&u.DevTmp)));
            if (pHlp->pfnSSMHandleGetAfter(pSSM) != SSMAFTER_DEBUG_IT)
            {
                /* safe, only needs to go to the config space array */
                rc = pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Device in slot %#x has been removed! vendor=%#06x device=%#06x"),
                                             uDevFn, PDMPciDevGetVendorId(&u.DevTmp), PDMPciDevGetDeviceId(&u.DevTmp));
                break;
            }
            continue;
        }

        /* match the vendor id assuming that this will never be changed. */
        /* safe, only needs to go to the config space array */
        if (PDMPciDevGetVendorId(&u.DevTmp) != PDMPciDevGetVendorId(pDev))
        {
            /* safe, only needs to go to the config space array */
            rc = pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Device in slot %#x (%s) vendor id mismatch! saved=%.4Rhxs current=%.4Rhxs"),
                                         uDevFn, pDev->pszNameR3, PDMPciDevGetVendorId(&u.DevTmp), PDMPciDevGetVendorId(pDev));
            break;
        }

        /* commit the loaded device config. */
        rc = devpciR3CommonRestoreRegions(pHlp, pSSM, pDev, u.DevTmp.Int.s.aIORegions,
                                          uVersion >= VBOX_ICH9PCI_SAVED_STATE_VERSION_REGION_SIZES);
        if (RT_FAILURE(rc))
            break;
        Assert(!pciDevIsPassthrough(pDev));
        devpciR3CommonRestoreConfig(pDevIns, pDev, &u.DevTmp.abConfig[0]);

        pDev->Int.s.uIrqPinState    = u.DevTmp.Int.s.uIrqPinState;
        pDev->Int.s.u8MsiCapOffset  = u.DevTmp.Int.s.u8MsiCapOffset;
        pDev->Int.s.u8MsiCapSize    = u.DevTmp.Int.s.u8MsiCapSize;
        pDev->Int.s.u8MsixCapOffset = u.DevTmp.Int.s.u8MsixCapOffset;
        pDev->Int.s.u8MsixCapSize   = u.DevTmp.Int.s.u8MsixCapSize;
        if (u.DevTmp.Int.s.u8MsixCapSize != 0) /** @todo r=bird: Why isn't this checkin u8MsixCapOffset??? */
        {
            Assert(pDev->Int.s.cbMsixRegion != 0);
            Assert(pDev->cbMsixState != 0);
            memcpy(pDev->abMsixState, u.DevTmp.abMsixState, RT_MIN(pDev->Int.s.cbMsixRegion, _32K + _16K));
        }
    }

    return rc;
}

DECL_HIDDEN_CALLBACK(int) devpciR3CommonLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PDEVPCIROOT     pThis = PDMINS_2_DATA(pDevIns, PDEVPCIROOT);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    PDEVPCIBUS      pBus  = &pThis->PciBus;
    uint32_t        u32;
    int             rc;

    /* We ignore this version as there's no saved state with it anyway */
    if (uVersion <= VBOX_ICH9PCI_SAVED_STATE_VERSION_NOMSI)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    if (uVersion > VBOX_ICH9PCI_SAVED_STATE_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /*
     * Bus state data.
     */
    pHlp->pfnSSMGetU32(pSSM, &pThis->uConfigReg);

    /*
     * Load IRQ states.
     */
    if (pThis->PciBus.enmType == DEVPCIBUSTYPE_ICH9)
    {
        for (unsigned i = 0; i < RT_ELEMENTS(pThis->auPciApicIrqLevels); i++)
            pHlp->pfnSSMGetU32V(pSSM, &pThis->auPciApicIrqLevels[i]);
    }
    else if (pThis->PciBus.enmType == DEVPCIBUSTYPE_GENERIC_ECAM)
    {
        for (unsigned i = 0; i < RT_ELEMENTS(pThis->u.GenericEcam.auPciIrqLevels); i++)
            pHlp->pfnSSMGetU32V(pSSM, &pThis->u.GenericEcam.auPciIrqLevels[i]);
    }
    else
        AssertReleaseFailed();

    /* separator */
    rc = pHlp->pfnSSMGetU32(pSSM, &u32);
    if (RT_FAILURE(rc))
        return rc;
    if (u32 != (uint32_t)~0)
        AssertMsgFailedReturn(("u32=%#x\n", u32), rc);

    return ich9pciR3CommonLoadExec(pDevIns, pBus, pSSM, uVersion, uPass);
}

DECL_HIDDEN_CALLBACK(int) devpciR3BridgeCommonLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PDEVPCIBUS pThis = PDMINS_2_DATA(pDevIns, PDEVPCIBUS);
    return ich9pciR3CommonLoadExec(pDevIns, pThis, pSSM, uVersion, uPass);
}



/* -=-=-=-=-=- Fake PCI BIOS Init -=-=-=-=-=- */


void devpciR3BiosInitSetRegionAddress(PPDMDEVINS pDevIns, PDEVPCIBUS pBus, PPDMPCIDEV pPciDev, int iRegion, uint64_t addr)
{
    NOREF(pBus);
    uint32_t uReg = devpciGetRegionReg(iRegion);

    /* Read memory type first. */
    uint8_t uResourceType = devpciR3GetByte(pPciDev, uReg);
    bool    f64Bit =    (uResourceType & ((uint8_t)(PCI_ADDRESS_SPACE_BAR64 | PCI_ADDRESS_SPACE_IO)))
                     == PCI_ADDRESS_SPACE_BAR64;

    Log(("Set region address: %02x:%02x.%d region %d address=%RX64%s\n",
         pBus->iBus, pPciDev->uDevFn >> 3, pPciDev->uDevFn & 7, iRegion, addr, f64Bit ? " (64-bit)" : ""));

    /* Write address of the device. */
    devpciR3SetDWord(pDevIns, pPciDev, uReg, (uint32_t)addr);
    if (f64Bit)
        devpciR3SetDWord(pDevIns, pPciDev, uReg + 4, (uint32_t)(addr >> 32));
}


static void ich9pciBiosInitBridge(PPDMDEVINS pDevIns, PDEVPCIROOT pPciRoot, PDEVPCIBUS pBus)
{
    PPDMPCIDEV pBridge = pDevIns->apPciDevs[0];
    Log(("BIOS init bridge: %02x:%02x.%d\n", pBus->iBus, pBridge->uDevFn >> 3, pBridge->uDevFn & 7));

    /*
     * The I/O range for the bridge must be aligned to a 4KB boundary.
     * This does not change anything really as the access to the device is not going
     * through the bridge but we want to be compliant to the spec.
     */
    if ((pPciRoot->uPciBiosIo % _4K) != 0)
    {
        pPciRoot->uPciBiosIo = RT_ALIGN_32(pPciRoot->uPciBiosIo, _4K);
        LogFunc(("Aligned I/O start address. New address %#x\n", pPciRoot->uPciBiosIo));
    }
    devpciR3SetByte(pDevIns, pBridge, VBOX_PCI_IO_BASE, (pPciRoot->uPciBiosIo >> 8) & 0xf0);

    /* The MMIO range for the bridge must be aligned to a 1MB boundary. */
    if ((pPciRoot->uPciBiosMmio % _1M) != 0)
    {
        pPciRoot->uPciBiosMmio = RT_ALIGN_32(pPciRoot->uPciBiosMmio, _1M);
        LogFunc(("Aligned MMIO start address. New address %#x\n", pPciRoot->uPciBiosMmio));
    }
    devpciR3SetWord(pDevIns, pBridge, VBOX_PCI_MEMORY_BASE, (pPciRoot->uPciBiosMmio >> 16) & UINT32_C(0xffff0));

    /* Save values to compare later to. */
    uint32_t u32IoAddressBase = pPciRoot->uPciBiosIo;
    uint32_t u32MMIOAddressBase = pPciRoot->uPciBiosMmio;

    /* Init all devices behind the bridge (recursing to further buses). */
    ich9pciBiosInitAllDevicesOnBus(pDevIns, pPciRoot, pBus);

    /*
     * Set I/O limit register. If there is no device with I/O space behind the
     * bridge we set a lower value than in the base register.
     */
    if (u32IoAddressBase != pPciRoot->uPciBiosIo)
    {
        /* Need again alignment to a 4KB boundary. */
        pPciRoot->uPciBiosIo = RT_ALIGN_32(pPciRoot->uPciBiosIo, _4K);
        devpciR3SetByte(pDevIns, pBridge, VBOX_PCI_IO_LIMIT, ((pPciRoot->uPciBiosIo - 1) >> 8) & 0xf0);
    }
    else
    {
        devpciR3SetByte(pDevIns, pBridge, VBOX_PCI_IO_BASE, 0xf0);
        devpciR3SetByte(pDevIns, pBridge, VBOX_PCI_IO_LIMIT, 0x00);
    }

    /* Same with the MMIO limit register but with 1MB boundary here. */
    if (u32MMIOAddressBase != pPciRoot->uPciBiosMmio)
    {
        pPciRoot->uPciBiosMmio = RT_ALIGN_32(pPciRoot->uPciBiosMmio, _1M);
        devpciR3SetWord(pDevIns, pBridge, VBOX_PCI_MEMORY_LIMIT, ((pPciRoot->uPciBiosMmio - 1) >> 16) & UINT32_C(0xfff0));
    }
    else
    {
        devpciR3SetWord(pDevIns, pBridge, VBOX_PCI_MEMORY_BASE, 0xfff0);
        devpciR3SetWord(pDevIns, pBridge, VBOX_PCI_MEMORY_LIMIT, 0x0000);
    }

    /*
     * Set the prefetch base and limit registers. We currently have no device with a prefetchable region
     * which may be behind a bridge. That's why it is unconditionally disabled here atm by writing a higher value into
     * the base register than in the limit register.
     */
    devpciR3SetWord(pDevIns, pBridge, VBOX_PCI_PREF_MEMORY_BASE, 0xfff0);
    devpciR3SetWord(pDevIns, pBridge, VBOX_PCI_PREF_MEMORY_LIMIT, 0x0000);
    devpciR3SetDWord(pDevIns, pBridge, VBOX_PCI_PREF_BASE_UPPER32, 0x00000000);
    devpciR3SetDWord(pDevIns, pBridge, VBOX_PCI_PREF_LIMIT_UPPER32, 0x00000000);
}

static int ich9pciBiosInitDeviceGetRegions(PPDMPCIDEV pPciDev)
{
    uint8_t uHeaderType = devpciR3GetByte(pPciDev, VBOX_PCI_HEADER_TYPE) & 0x7f;
    if (uHeaderType == 0x00)
        /* Ignore ROM region here, which is included in VBOX_PCI_NUM_REGIONS. */
        return VBOX_PCI_NUM_REGIONS - 1;
    else if (uHeaderType == 0x01)
        /* PCI bridges have 2 BARs. */
        return 2;
    else
    {
        AssertMsgFailed(("invalid header type %#x\n", uHeaderType));
        return 0;
    }
}

static void ich9pciBiosInitDeviceBARs(PPDMDEVINS pDevIns, PDEVPCIROOT pPciRoot, PDEVPCIBUS pBus, PPDMPCIDEV pPciDev)
{
    int cRegions = ich9pciBiosInitDeviceGetRegions(pPciDev);
    bool fSuppressMem = false;
    bool fActiveMemRegion = false;
    bool fActiveIORegion = false;
    for (int iRegion = 0; iRegion < cRegions; iRegion++)
    {
        uint32_t u32Address = devpciGetRegionReg(iRegion);

        /* Calculate size - we write all 1s into the BAR, and then evaluate which bits
           are cleared. */
        uint8_t u8ResourceType = devpciR3GetByte(pPciDev, u32Address);

        bool fPrefetch =    (u8ResourceType & ((uint8_t)(PCI_ADDRESS_SPACE_MEM_PREFETCH | PCI_ADDRESS_SPACE_IO)))
                         == PCI_ADDRESS_SPACE_MEM_PREFETCH;
        bool f64Bit =    (u8ResourceType & ((uint8_t)(PCI_ADDRESS_SPACE_BAR64 | PCI_ADDRESS_SPACE_IO)))
                      == PCI_ADDRESS_SPACE_BAR64;
        bool fIsPio = ((u8ResourceType & PCI_ADDRESS_SPACE_IO) == PCI_ADDRESS_SPACE_IO);
        uint64_t cbRegSize64 = 0;

        /* Hack: initialize prefetchable BARs for devices on the root bus
         * early, but for all other prefetchable BARs do it after the
         * non-prefetchable BARs are initialized on all buses. */
        if (fPrefetch && pBus->iBus != 0)
        {
            fSuppressMem = true;
            if (f64Bit)
                iRegion++; /* skip next region */
            continue;
        }

        if (f64Bit)
        {
            devpciR3SetDWord(pDevIns, pPciDev, u32Address,   UINT32_C(0xffffffff));
            devpciR3SetDWord(pDevIns, pPciDev, u32Address+4, UINT32_C(0xffffffff));
            cbRegSize64 = RT_MAKE_U64(devpciR3GetDWord(pPciDev, u32Address),
                                      devpciR3GetDWord(pPciDev, u32Address+4));
            cbRegSize64 &= ~UINT64_C(0x0f);
            cbRegSize64 = (~cbRegSize64) + 1;

            /* No 64-bit PIO regions possible. */
#ifndef DEBUG_bird /* EFI triggers this for DevAHCI. */
            AssertMsg((u8ResourceType & PCI_ADDRESS_SPACE_IO) == 0, ("type=%#x rgn=%d\n", u8ResourceType, iRegion));
#endif
        }
        else
        {
            uint32_t cbRegSize32;
            devpciR3SetDWord(pDevIns, pPciDev, u32Address, UINT32_C(0xffffffff));
            cbRegSize32 = devpciR3GetDWord(pPciDev, u32Address);

            /* Clear resource information depending on resource type. */
            if (fIsPio) /* PIO */
                cbRegSize32 &= ~UINT32_C(0x01);
            else        /* MMIO */
                cbRegSize32 &= ~UINT32_C(0x0f);

            /*
             * Invert all bits and add 1 to get size of the region.
             * (From PCI implementation note)
             */
            if (fIsPio && (cbRegSize32 & UINT32_C(0xffff0000)) == 0)
                cbRegSize32 = (~(cbRegSize32 | UINT32_C(0xffff0000))) + 1;
            else
                cbRegSize32 = (~cbRegSize32) + 1;

            cbRegSize64 = cbRegSize32;
        }
        Log2Func(("Size of region %u for device %d on bus %d is %lld\n", iRegion, pPciDev->uDevFn, pBus->iBus, cbRegSize64));

        if (cbRegSize64)
        {
            /* Try 32-bit base first. */
            uint32_t* paddr = fIsPio ? &pPciRoot->uPciBiosIo : &pPciRoot->uPciBiosMmio;
            uint64_t  uNew = *paddr;
            /* Align starting address to region size. */
            uNew = (uNew + cbRegSize64 - 1) & ~(cbRegSize64 - 1);
            if (fIsPio)
                uNew &= UINT32_C(0xffff);
            /* Unconditionally exclude I/O-APIC/HPET/ROM. Pessimistic, but better than causing a mess. */
            if (   !uNew
                || (uNew <= UINT32_C(0xffffffff) && uNew + cbRegSize64 - 1 >= UINT32_C(0xfec00000))
                || uNew >= _4G)
            {
                /* Only prefetchable regions can be placed above 4GB, as the
                 * address decoder for non-prefetchable addresses in bridges
                 * is limited to 32 bits. */
                if (f64Bit && fPrefetch)
                {
                    /* Map a 64-bit region above 4GB. */
                    Assert(!fIsPio);
                    uNew = pPciRoot->uPciBiosMmio64;
                    /* Align starting address to region size. */
                    uNew = (uNew + cbRegSize64 - 1) & ~(cbRegSize64 - 1);
                    LogFunc(("Start address of 64-bit MMIO region %u/%u is %#llx\n", iRegion, iRegion + 1, uNew));
                    devpciR3BiosInitSetRegionAddress(pDevIns, pBus, pPciDev, iRegion, uNew);
                    fActiveMemRegion = true;
                    pPciRoot->uPciBiosMmio64 = uNew + cbRegSize64;
                    Log2Func(("New 64-bit address is %#llx\n", pPciRoot->uPciBiosMmio64));
                }
                else
                {
                    uint16_t uVendor = devpciR3GetWord(pPciDev, VBOX_PCI_VENDOR_ID);
                    uint16_t uDevice = devpciR3GetWord(pPciDev, VBOX_PCI_DEVICE_ID);
                    LogRel(("PCI: no space left for BAR%u of device %u/%u/%u (vendor=%#06x device=%#06x)\n",
                            iRegion, pBus->iBus, pPciDev->uDevFn >> 3, pPciDev->uDevFn & 7, uVendor, uDevice)); /** @todo make this a VM start failure later. */
                    /* Undo the mapping mess caused by the size probing. */
                    devpciR3SetDWord(pDevIns, pPciDev, u32Address, UINT32_C(0));
                }
            }
            else
            {
                LogFunc(("Start address of %s region %u is %#x\n", (fIsPio ? "I/O" : "MMIO"), iRegion, uNew));
                devpciR3BiosInitSetRegionAddress(pDevIns, pBus, pPciDev, iRegion, uNew);
                if (fIsPio)
                    fActiveIORegion = true;
                else
                    fActiveMemRegion = true;
                *paddr = uNew + cbRegSize64;
                Log2Func(("New 32-bit address is %#x\n", *paddr));
            }

            if (f64Bit)
                iRegion++; /* skip next region */
        }
    }

    /* Update the command word appropriately. */
    uint16_t uCmd = devpciR3GetWord(pPciDev, VBOX_PCI_COMMAND);
    if (fActiveMemRegion && !fSuppressMem)
        uCmd |= VBOX_PCI_COMMAND_MEMORY; /* Enable MMIO access. */
    if (fActiveIORegion)
        uCmd |= VBOX_PCI_COMMAND_IO; /* Enable I/O space access. */
    devpciR3SetWord(pDevIns, pPciDev, VBOX_PCI_COMMAND, uCmd);
}

static bool ich9pciBiosInitDevicePrefetchableBARs(PPDMDEVINS pDevIns, PDEVPCIROOT pPciRoot, PDEVPCIBUS pBus, PPDMPCIDEV pPciDev, bool fUse64Bit, bool fDryrun)
{
    int cRegions = ich9pciBiosInitDeviceGetRegions(pPciDev);
    bool fActiveMemRegion = false;
    for (int iRegion = 0; iRegion < cRegions; iRegion++)
    {
        uint32_t u32Address = devpciGetRegionReg(iRegion);
        uint8_t u8ResourceType = devpciR3GetByte(pPciDev, u32Address);
        bool fPrefetch =    (u8ResourceType & ((uint8_t)(PCI_ADDRESS_SPACE_MEM_PREFETCH | PCI_ADDRESS_SPACE_IO)))
                      == PCI_ADDRESS_SPACE_MEM_PREFETCH;
        bool f64Bit =    (u8ResourceType & ((uint8_t)(PCI_ADDRESS_SPACE_BAR64 | PCI_ADDRESS_SPACE_IO)))
                      == PCI_ADDRESS_SPACE_BAR64;
        uint64_t cbRegSize64 = 0;

        /* Everything besides prefetchable regions has been set up already. */
        if (!fPrefetch)
            continue;

        if (f64Bit)
        {
            devpciR3SetDWord(pDevIns, pPciDev, u32Address,   UINT32_C(0xffffffff));
            devpciR3SetDWord(pDevIns, pPciDev, u32Address+4, UINT32_C(0xffffffff));
            cbRegSize64 = RT_MAKE_U64(devpciR3GetDWord(pPciDev, u32Address),
                                      devpciR3GetDWord(pPciDev, u32Address+4));
            cbRegSize64 &= ~UINT64_C(0x0f);
            cbRegSize64 = (~cbRegSize64) + 1;
        }
        else
        {
            uint32_t cbRegSize32;
            devpciR3SetDWord(pDevIns, pPciDev, u32Address, UINT32_C(0xffffffff));
            cbRegSize32 = devpciR3GetDWord(pPciDev, u32Address);
            cbRegSize32 &= ~UINT32_C(0x0f);
            cbRegSize32 = (~cbRegSize32) + 1;

            cbRegSize64 = cbRegSize32;
        }
        Log2Func(("Size of region %u for device %d on bus %d is %lld\n", iRegion, pPciDev->uDevFn, pBus->iBus, cbRegSize64));

        if (cbRegSize64)
        {
            uint64_t uNew;
            if (!fUse64Bit)
            {
                uNew = pPciRoot->uPciBiosMmio;
                /* Align starting address to region size. */
                uNew = (uNew + cbRegSize64 - 1) & ~(cbRegSize64 - 1);
                /* Unconditionally exclude I/O-APIC/HPET/ROM. Pessimistic, but better than causing a mess. Okay for BIOS. */
                if (   !uNew
                    || (uNew <= UINT32_C(0xffffffff) && uNew + cbRegSize64 - 1 >= UINT32_C(0xfec00000))
                    || uNew >= _4G)
                {
                    Log2Func(("region #%u: Rejecting address range: %#x LB %#RX64\n", iRegion, uNew, cbRegSize64));
                    Assert(fDryrun);
                    return true;
                }
                if (!fDryrun)
                {
                    LogFunc(("Start address of MMIO region %u is %#x\n", iRegion, uNew));
                    devpciR3BiosInitSetRegionAddress(pDevIns, pBus, pPciDev, iRegion, uNew);
                    fActiveMemRegion = true;
                }
                pPciRoot->uPciBiosMmio = uNew + cbRegSize64;
            }
            else
            {
                /* Can't handle 32-bit BARs when forcing 64-bit allocs. */
                if (!f64Bit)
                {
                    Assert(fDryrun);
                    return true;
                }
                uNew = pPciRoot->uPciBiosMmio64;
                /* Align starting address to region size. */
                uNew = (uNew + cbRegSize64 - 1) & ~(cbRegSize64 - 1);
                pPciRoot->uPciBiosMmio64 = uNew + cbRegSize64;
                if (!fDryrun)
                {
                    LogFunc(("Start address of 64-bit MMIO region %u/%u is %#llx\n", iRegion, iRegion + 1, uNew));
                    devpciR3BiosInitSetRegionAddress(pDevIns, pBus, pPciDev, iRegion, uNew);
                    fActiveMemRegion = true;
                }
            }

            if (f64Bit)
                iRegion++; /* skip next region */
        }
    }

    if (!fDryrun)
    {
        /* Update the command word appropriately. */
        uint16_t uCmd = devpciR3GetWord(pPciDev, VBOX_PCI_COMMAND);
        if (fActiveMemRegion)
            uCmd |= VBOX_PCI_COMMAND_MEMORY; /* Enable MMIO access. */
        devpciR3SetWord(pDevIns, pPciDev, VBOX_PCI_COMMAND, uCmd);
    }
    else
        Assert(!fActiveMemRegion);

    return false;
}

static bool ich9pciBiosInitBridgePrefetchable(PPDMDEVINS pDevIns, PDEVPCIROOT pPciRoot, PDEVPCIBUS pBus, bool fUse64Bit, bool fDryrun)
{
    PPDMPCIDEV pBridge = pDevIns->apPciDevs[0];
    Log(("BIOS init bridge (prefetch): %02x:%02x.%d use64bit=%d dryrun=%d\n", pBus->iBus, pBridge->uDevFn >> 3, pBridge->uDevFn & 7, fUse64Bit, fDryrun));

    pPciRoot->uPciBiosMmio = RT_ALIGN_32(pPciRoot->uPciBiosMmio, _1M);
    pPciRoot->uPciBiosMmio64 = RT_ALIGN_64(pPciRoot->uPciBiosMmio64, _1M);

    /* Save values to compare later to. */
    uint32_t u32MMIOAddressBase = pPciRoot->uPciBiosMmio;
    uint64_t u64MMIOAddressBase = pPciRoot->uPciBiosMmio64;

    /* Init all devices behind the bridge (recursing to further buses). */
    bool fRes = ich9pciBiosInitAllDevicesPrefetchableOnBus(pDevIns, pPciRoot, pBus, fUse64Bit, fDryrun);
    if (fDryrun)
        return fRes;
    Assert(!fRes);

    /* Set prefetchable MMIO limit register with 1MB boundary. */
    uint64_t uBase, uLimit;
    if (fUse64Bit)
    {
        if (u64MMIOAddressBase == pPciRoot->uPciBiosMmio64)
            return false;
        uBase = u64MMIOAddressBase;
        uLimit = RT_ALIGN_64(pPciRoot->uPciBiosMmio64, _1M) - 1;
    }
    else
    {
        if (u32MMIOAddressBase == pPciRoot->uPciBiosMmio)
            return false;
        uBase = u32MMIOAddressBase;
        uLimit = RT_ALIGN_32(pPciRoot->uPciBiosMmio, _1M) - 1;
    }
    devpciR3SetDWord(pDevIns, pBridge, VBOX_PCI_PREF_BASE_UPPER32, uBase >> 32);
    devpciR3SetWord(pDevIns, pBridge, VBOX_PCI_PREF_MEMORY_BASE, (uint32_t)(uBase >> 16) & UINT32_C(0xfff0));
    devpciR3SetDWord(pDevIns, pBridge, VBOX_PCI_PREF_LIMIT_UPPER32, uLimit >> 32);
    devpciR3SetWord(pDevIns, pBridge, VBOX_PCI_PREF_MEMORY_LIMIT, (uint32_t)(uLimit >> 16) & UINT32_C(0xfff0));

    return false;
}

static bool ich9pciBiosInitAllDevicesPrefetchableOnBus(PPDMDEVINS pDevIns, PDEVPCIROOT pPciRoot, PDEVPCIBUS pBus,
                                                       bool fUse64Bit, bool fDryrun)
{
    /* First pass: assign resources to all devices. */
    for (uint32_t uDevFn = 0; uDevFn < RT_ELEMENTS(pBus->apDevices); uDevFn++)
    {
        PPDMPCIDEV pPciDev = pBus->apDevices[uDevFn];

        /* check if device is present */
        if (!pPciDev)
            continue;

        Log(("BIOS init device (prefetch): %02x:%02x.%d\n", pBus->iBus, uDevFn >> 3, uDevFn & 7));

        /* prefetchable memory mappings */
        bool fRes = ich9pciBiosInitDevicePrefetchableBARs(pDevIns, pPciRoot, pBus, pPciDev, fUse64Bit, fDryrun);
        if (fRes)
        {
            Assert(fDryrun);
            return fRes;
        }
    }

    /* Second pass: handle bridges recursively. */
    for (uint32_t iBridge = 0; iBridge < pBus->cBridges; iBridge++)
    {
        PPDMPCIDEV pBridge = pBus->papBridgesR3[iBridge];
        AssertMsg(pBridge && pciDevIsPci2PciBridge(pBridge),
                  ("Device is not a PCI bridge but on the list of PCI bridges\n"));
        PDEVPCIBUS pChildBus = PDMINS_2_DATA(pBridge->Int.s.CTX_SUFF(pDevIns), PDEVPCIBUS);

        bool fRes = ich9pciBiosInitBridgePrefetchable(pDevIns, pPciRoot, pChildBus, fUse64Bit, fDryrun);
        if (fRes)
        {
            Assert(fDryrun);
            return fRes;
        }
    }
    return false;
}

static void ich9pciBiosInitAllDevicesOnBus(PPDMDEVINS pDevIns, PDEVPCIROOT pPciRoot, PDEVPCIBUS pBus)
{
    PDEVPCIBUSCC pBusCC = PDMINS_2_DATA_CC(pDevIns, PDEVPCIBUSCC);

    /* First pass: assign resources to all devices and map the interrupt. */
    for (uint32_t uDevFn = 0; uDevFn < RT_ELEMENTS(pBus->apDevices); uDevFn++)
    {
        PPDMPCIDEV pPciDev = pBus->apDevices[uDevFn];

        /* check if device is present */
        if (!pPciDev)
            continue;

        Log(("BIOS init device: %02x:%02x.%d\n", pBus->iBus, uDevFn >> 3, uDevFn & 7));

        /* default memory mappings */
        ich9pciBiosInitDeviceBARs(pDevIns, pPciRoot, pBus, pPciDev);
        uint16_t uDevClass = devpciR3GetWord(pPciDev, VBOX_PCI_CLASS_DEVICE);
        switch (uDevClass)
        {
            case 0x0101:
                /* IDE controller */
                devpciR3SetWord(pDevIns, pPciDev, 0x40, 0x8000); /* enable IDE0 */
                devpciR3SetWord(pDevIns, pPciDev, 0x42, 0x8000); /* enable IDE1 */
                break;
            case 0x0300:
            {
                /* VGA controller */

                /* NB: Default Bochs VGA LFB address is 0xE0000000. Old guest
                 * software may break if the framebuffer isn't mapped there.
                 */

                /*
                 * Legacy VGA I/O ports are implicitly decoded by a VGA class device. But
                 * only the framebuffer (i.e., a memory region) is explicitly registered via
                 * ich9pciSetRegionAddress, so don't forget to enable I/O decoding.
                 */
                uint16_t uCmd = devpciR3GetWord(pPciDev, VBOX_PCI_COMMAND);
                devpciR3SetWord(pDevIns, pPciDev, VBOX_PCI_COMMAND, uCmd | VBOX_PCI_COMMAND_IO);
                break;
            }
#ifdef VBOX_WITH_IOMMU_AMD
            case 0x0806:
            {
                /* IOMMU. */
                uint16_t const uVendorId = devpciR3GetWord(pPciDev, VBOX_PCI_VENDOR_ID);
                if (uVendorId == IOMMU_PCI_VENDOR_ID)
                {
                    /* AMD. */
                    devpciR3SetDWord(pDevIns, pPciDev, IOMMU_PCI_OFF_BASE_ADDR_REG_LO,
                                     IOMMU_MMIO_BASE_ADDR | RT_BIT(0)); /* enable base address (bit 0). */
                }
                break;
            }
#endif
            default:
                break;
        }

        /* map the interrupt */
        uint8_t iPin = devpciR3GetByte(pPciDev, VBOX_PCI_INTERRUPT_PIN);
        if (iPin != 0)
        {
            iPin--;

            /* We need to go up to the host bus to see which irq pin this
               device will use there.  See logic in ich9pcibridgeSetIrq(). */
            uint32_t   idxPdmParentBus;
            PPDMDEVINS pDevInsParent = pDevIns;
            while ((idxPdmParentBus = pDevInsParent->apPciDevs[0]->Int.s.idxPdmBus) != 0)
            {
                /* Get the pin the device would assert on the bridge. */
                iPin = ((pDevInsParent->apPciDevs[0]->uDevFn >> 3) + iPin) & 3;

                pDevInsParent = pBusCC->pPciHlpR3->pfnGetBusByNo(pDevIns, idxPdmParentBus);
                AssertLogRelBreak(pDevInsParent);
            }

            int iIrq = aPciIrqs[ich9pciSlotGetPirq(pBus->iBus, uDevFn, iPin)];
            Log(("Using pin %d and IRQ %d for device %02x:%02x.%d\n",
                 iPin, iIrq, pBus->iBus, uDevFn>>3, uDevFn&7));
            devpciR3SetByte(pDevIns, pPciDev, VBOX_PCI_INTERRUPT_LINE, iIrq);
        }
    }

    /* Second pass: handle bridges recursively. */
    for (uint32_t iBridge = 0; iBridge < pBus->cBridges; iBridge++)
    {
        PPDMPCIDEV pBridge = pBus->papBridgesR3[iBridge];
        AssertMsg(pBridge && pciDevIsPci2PciBridge(pBridge),
                  ("Device is not a PCI bridge but on the list of PCI bridges\n"));
        PDEVPCIBUS pChildBus = PDMINS_2_DATA(pBridge->Int.s.CTX_SUFF(pDevIns), PDEVPCIBUS);

        ich9pciBiosInitBridge(pDevIns, pPciRoot, pChildBus);
    }

    /* Third pass (only for bus 0): set up prefetchable BARs recursively. */
    if (pBus->iBus == 0)
    {
        for (uint32_t iBridge = 0; iBridge < pBus->cBridges; iBridge++)
        {
            PPDMPCIDEV pBridge = pBus->papBridgesR3[iBridge];
            AssertMsg(pBridge && pciDevIsPci2PciBridge(pBridge),
                      ("Device is not a PCI bridge but on the list of PCI bridges\n"));
            PDEVPCIBUS pChildBus = PDMINS_2_DATA(pBridge->Int.s.CTX_SUFF(pDevIns), PDEVPCIBUS);

            Log(("BIOS init prefetchable memory behind bridge: %02x:%02x.%d\n", pChildBus->iBus, pBridge->uDevFn >> 3, pBridge->uDevFn & 7));
            /* Save values for the prefetchable dryruns. */
            uint32_t u32MMIOAddressBase = pPciRoot->uPciBiosMmio;
            uint64_t u64MMIOAddressBase = pPciRoot->uPciBiosMmio64;

            bool fProbe = ich9pciBiosInitBridgePrefetchable(pDevIns, pPciRoot, pChildBus, false /* fUse64Bit */, true /* fDryrun */);
            pPciRoot->uPciBiosMmio = u32MMIOAddressBase;
            pPciRoot->uPciBiosMmio64 = u64MMIOAddressBase;
            if (fProbe)
            {
                fProbe = ich9pciBiosInitBridgePrefetchable(pDevIns, pPciRoot, pChildBus, true /* fUse64Bit */, true /* fDryrun */);
                pPciRoot->uPciBiosMmio = u32MMIOAddressBase;
                pPciRoot->uPciBiosMmio64 = u64MMIOAddressBase;
                if (fProbe)
                    LogRel(("PCI: unresolvable prefetchable memory behind bridge %02x:%02x.%d\n", pChildBus->iBus, pBridge->uDevFn >> 3, pBridge->uDevFn & 7));
                else
                    ich9pciBiosInitBridgePrefetchable(pDevIns, pPciRoot, pChildBus, true /* fUse64Bit */, false /* fDryrun */);
            }
            else
                ich9pciBiosInitBridgePrefetchable(pDevIns, pPciRoot, pChildBus, false /* fUse64Bit */, false /* fDryrun */);
        }
    }
}

/**
 * Initializes bridges registers used for routing.
 *
 * We ASSUME PDM bus assignments are the same as the PCI bus assignments and
 * will complain if we find any conflicts.  This because it is just soo much
 * simpler to have the two numbers match one another by default.
 *
 * @returns Max subordinate bus number.
 * @param   pDevIns         The device instance of the bus.
 * @param   pPciRoot        Global device instance data used to generate unique bus numbers.
 * @param   pBus            The PCI bus to initialize.
 * @param   pbmUsed         Pointer to a 32-bit bitmap tracking which device
 *                          (ranges) has been used.
 * @param   uBusPrimary     The primary bus number the bus is connected to.
 */
static uint8_t ich9pciBiosInitBridgeTopology(PPDMDEVINS pDevIns, PDEVPCIROOT pPciRoot, PDEVPCIBUS pBus,
                                             uint32_t *pbmUsed, uint8_t uBusPrimary)
{
    PPDMPCIDEV pBridgeDev = pDevIns->apPciDevs[0];

    /* Check if the PDM bus assignment makes sense.    */
    AssertLogRelMsg(!(*pbmUsed & RT_BIT_32(pBus->iBus)),
                    ("PCIBIOS: Bad PCI bridge config! Conflict for bus %#x. Make sure to instantiate bridges for a sub-trees in sequence!\n",
                     pBus->iBus));
    *pbmUsed |= RT_BIT_32(pBus->iBus);

    /* Set only if we are not on the root bus, it has no primary bus attached. */
    if (pBus->iBus != 0)
    {
        devpciR3SetByte(pDevIns, pBridgeDev, VBOX_PCI_PRIMARY_BUS, uBusPrimary);
        devpciR3SetByte(pDevIns, pBridgeDev, VBOX_PCI_SECONDARY_BUS, pBus->iBus);
        /* Since the subordinate bus value can only be finalized once we
         * finished recursing through everything behind the bridge, the only
         * solution is temporarily configuring the subordinate to the maximum
         * possible value. This makes sure that the config space accesses work
         * (for our own sloppy emulation it apparently doesn't matter, but
         * this is vital for real PCI bridges/devices in passthrough mode). */
        devpciR3SetByte(pDevIns, pBridgeDev, VBOX_PCI_SUBORDINATE_BUS, 0xff);
    }

    uint8_t uMaxSubNum = pBus->iBus;
    for (uint32_t iBridge = 0; iBridge < pBus->cBridges; iBridge++)
    {
        PPDMPCIDEV pBridge = pBus->papBridgesR3[iBridge];
        AssertMsg(pBridge && pciDevIsPci2PciBridge(pBridge),
                  ("Device is not a PCI bridge but on the list of PCI bridges\n"));
        PDEVPCIBUS pChildBus = PDMINS_2_DATA(pBridge->Int.s.CTX_SUFF(pDevIns), PDEVPCIBUS);
        uint8_t uMaxChildSubBus = ich9pciBiosInitBridgeTopology(pDevIns, pPciRoot, pChildBus, pbmUsed, pBus->iBus);
        uMaxSubNum = RT_MAX(uMaxSubNum, uMaxChildSubBus);
    }

    if (pBus->iBus != 0)
        devpciR3SetByte(pDevIns, pBridgeDev, VBOX_PCI_SUBORDINATE_BUS, uMaxSubNum);
    for (uint32_t i = pBus->iBus; i <= uMaxSubNum; i++)
        *pbmUsed |= RT_BIT_32(i);

    /* Make sure that transactions are able to get through the bridge. Not
     * strictly speaking necessary this early (before any device is set up),
     * but on the other hand it can't hurt either. */
    if (pBus->iBus != 0)
        devpciR3SetWord(pDevIns, pBridgeDev, VBOX_PCI_COMMAND,
                          VBOX_PCI_COMMAND_IO
                        | VBOX_PCI_COMMAND_MEMORY
                        | VBOX_PCI_COMMAND_MASTER);

    /* safe, only needs to go to the config space array */
    Log2Func(("for bus %p: primary=%d secondary=%d subordinate=%d\n", pBus,
              PDMPciDevGetByte(pBridgeDev, VBOX_PCI_PRIMARY_BUS),
              PDMPciDevGetByte(pBridgeDev, VBOX_PCI_SECONDARY_BUS),
              PDMPciDevGetByte(pBridgeDev, VBOX_PCI_SUBORDINATE_BUS) ));

    return uMaxSubNum;
}


/**
 * Worker for Fake PCI BIOS config
 *
 * @returns VBox status code.
 *
 * @param   pDevIns     ICH9 device instance.
 */
static int ich9pciFakePCIBIOS(PPDMDEVINS pDevIns)
{
    PDEVPCIROOT     pPciRoot   = PDMINS_2_DATA(pDevIns, PDEVPCIROOT);
    uint32_t const  cbBelow4GB = PDMDevHlpMMPhysGetRamSizeBelow4GB(pDevIns);
    uint64_t const  cbAbove4GB = PDMDevHlpMMPhysGetRamSizeAbove4GB(pDevIns);

    LogRel(("PCI: setting up topology, resources and interrupts\n"));

    /** @todo r=klaus this needs to do the same elcr magic as DevPCI.cpp, as the BIOS can't be trusted to do the right thing. Of course it's more difficult than with the old code, as there are bridges to be handled. The interrupt routing needs to be taken into account. Also I highly suspect that the chipset has 8 interrupt lines which we might be able to use for handling things on the root bus better (by treating them as devices on the mainboard). */

    /*
     * Set the start addresses.
     */
    pPciRoot->uPciBiosBus    = 0;
    pPciRoot->uPciBiosIo     = 0xd000;
    pPciRoot->uPciBiosMmio   = cbBelow4GB;
    pPciRoot->uPciBiosMmio64 = cbAbove4GB + _4G;

    /* NB: Assume that if PCI controller MMIO range is enabled, it is below the beginning of the memory hole. */
    if (pPciRoot->u64PciConfigMMioAddress)
    {
        AssertRelease(pPciRoot->u64PciConfigMMioAddress >= cbBelow4GB);
        pPciRoot->uPciBiosMmio = pPciRoot->u64PciConfigMMioAddress + pPciRoot->u64PciConfigMMioLength;
    }
    Log(("cbBelow4GB: %#RX32, uPciBiosMmio: %#RX64, cbAbove4GB: %#RX64, uPciBiosMmio64=%#RX64\n",
         cbBelow4GB, pPciRoot->uPciBiosMmio, cbAbove4GB, pPciRoot->uPciBiosMmio64));

    /*
     * Assign bridge topology, for further routing to work.
     */
    PDEVPCIBUS pBus = &pPciRoot->PciBus;
    AssertLogRel(pBus->iBus == 0);
    uint32_t bmUsed = 0;
    ich9pciBiosInitBridgeTopology(pDevIns, pPciRoot, pBus, &bmUsed, 0);

    /*
     * Init all devices on bus 0 (recursing to further buses).
     */
    ich9pciBiosInitAllDevicesOnBus(pDevIns, pPciRoot, pBus);

    return VINF_SUCCESS;
}


/* -=-=-=-=-=- PCI Config Space -=-=-=-=-=- */


/**
 * Reads config space for a device, ignoring interceptors.
 */
DECLHIDDEN(VBOXSTRICTRC) devpciR3CommonConfigReadWorker(PPDMPCIDEV pPciDev, uint32_t uAddress, unsigned cb, uint32_t *pu32Value)
{
    uint32_t uValue;
    if (uAddress + cb <= RT_MIN(pPciDev->cbConfig, sizeof(pPciDev->abConfig)))
    {
        switch (cb)
        {
            case 1:
                /* safe, only needs to go to the config space array */
                uValue = PDMPciDevGetByte(pPciDev,  uAddress);
                break;
            case 2:
                /* safe, only needs to go to the config space array */
                uValue = PDMPciDevGetWord(pPciDev,  uAddress);
                break;
            case 4:
                /* safe, only needs to go to the config space array */
                uValue = PDMPciDevGetDWord(pPciDev, uAddress);
                break;
            default:
                AssertFailed();
                uValue = 0;
                break;
        }

#ifdef LOG_ENABLED
        if (   pciDevIsMsiCapable(pPciDev)
            && uAddress - (uint32_t)pPciDev->Int.s.u8MsiCapOffset < (uint32_t)pPciDev->Int.s.u8MsiCapSize )
            Log2Func(("MSI CAP: %#x LB %u -> %#x\n", uAddress - (uint32_t)pPciDev->Int.s.u8MsiCapOffset, cb, uValue));
        else if (   pciDevIsMsixCapable(pPciDev)
                 && uAddress - (uint32_t)pPciDev->Int.s.u8MsixCapOffset < (uint32_t)pPciDev->Int.s.u8MsixCapSize)
            Log2Func(("MSI-X CAP: %#x LB %u -> %#x\n", uAddress - (uint32_t)pPciDev->Int.s.u8MsiCapOffset, cb, uValue));
#endif
    }
    else
    {
        AssertMsgFailed(("Read after end of PCI config space: %#x LB %u\n", uAddress, cb));
        uValue = 0;
    }

    *pu32Value = uValue;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMPCIBUSREGR3,pfnConfigRead}
 */
DECLCALLBACK(VBOXSTRICTRC) devpciR3CommonConfigRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                    uint32_t uAddress, unsigned cb, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns);
    return devpciR3CommonConfigReadWorker(pPciDev, uAddress, cb, pu32Value);
}


/**
 * Worker for devpciR3ResetDevice and devpciR3UpdateMappings that unmaps a region.
 *
 * @returns VBox status code.
 * @param   pDev                The PCI device.
 * @param   iRegion             The region to unmap.
 */
static int devpciR3UnmapRegion(PPDMPCIDEV pDev, int iRegion)
{
    PPCIIOREGION pRegion = &pDev->Int.s.aIORegions[iRegion];
    AssertReturn(pRegion->size != 0, VINF_SUCCESS);

    int rc = VINF_SUCCESS;
    if (pRegion->addr != INVALID_PCI_ADDRESS)
    {
        /*
         * Do callout first (optional), then do the unmapping via handle if we've been handed one.
         */
        if (pRegion->pfnMap)
        {
            rc = pRegion->pfnMap(pDev->Int.s.pDevInsR3, pDev, iRegion,
                                 NIL_RTGCPHYS, pRegion->size, (PCIADDRESSSPACE)(pRegion->type));
            AssertRC(rc);
        }

        switch (pRegion->fFlags & PDMPCIDEV_IORGN_F_HANDLE_MASK)
        {
            case PDMPCIDEV_IORGN_F_IOPORT_HANDLE:
                rc = PDMDevHlpIoPortUnmap(pDev->Int.s.pDevInsR3, (IOMIOPORTHANDLE)pRegion->hHandle);
                AssertRC(rc);
                break;

            case PDMPCIDEV_IORGN_F_MMIO_HANDLE:
                rc = PDMDevHlpMmioUnmap(pDev->Int.s.pDevInsR3, (IOMMMIOHANDLE)pRegion->hHandle);
                AssertRC(rc);
                break;

            case PDMPCIDEV_IORGN_F_MMIO2_HANDLE:
                rc = PDMDevHlpMmio2Unmap(pDev->Int.s.pDevInsR3, (PGMMMIO2HANDLE)pRegion->hHandle);
                AssertRC(rc);
                break;

            case PDMPCIDEV_IORGN_F_NO_HANDLE:
                Assert(pRegion->fFlags & PDMPCIDEV_IORGN_F_NEW_STYLE);
                Assert(pRegion->hHandle == UINT64_MAX);
                break;

            default:
                AssertLogRelFailed();
        }
        pRegion->addr = INVALID_PCI_ADDRESS;
    }
    return rc;
}


/**
 * Worker for devpciR3CommonDefaultConfigWrite that updates BAR and ROM mappings.
 *
 * @returns VINF_SUCCESS of DBGFSTOP result.
 * @param   pPciDev             The PCI device to update the mappings for.
 * @param   fP2PBridge          Whether this is a PCI to PCI bridge or not.
 */
static VBOXSTRICTRC devpciR3UpdateMappings(PPDMPCIDEV pPciDev, bool fP2PBridge)
{
    /* safe, only needs to go to the config space array */
    uint16_t const u16Cmd = PDMPciDevGetWord(pPciDev, VBOX_PCI_COMMAND);
    Log4(("devpciR3UpdateMappings: dev %u/%u (%s): u16Cmd=%#x\n",
          pPciDev->uDevFn >> VBOX_PCI_DEVFN_DEV_SHIFT, pPciDev->uDevFn & VBOX_PCI_DEVFN_FUN_MASK, pPciDev->pszNameR3, u16Cmd));
    for (unsigned iRegion = 0; iRegion < VBOX_PCI_NUM_REGIONS; iRegion++)
    {
        /* Skip over BAR2..BAR5 for bridges, as they have a different meaning there. */
        if (fP2PBridge && iRegion >= 2 && iRegion <= 5)
            continue;
        PCIIOREGION   *pRegion  = &pPciDev->Int.s.aIORegions[iRegion];
        uint64_t const cbRegion = pRegion->size;
        if (cbRegion != 0)
        {
            uint32_t const offCfgReg = devpciGetRegionReg(iRegion);
            bool const     f64Bit    =    (pRegion->type & ((uint8_t)(PCI_ADDRESS_SPACE_BAR64 | PCI_ADDRESS_SPACE_IO)))
                                       == PCI_ADDRESS_SPACE_BAR64;
            uint64_t       uNew      = INVALID_PCI_ADDRESS;

            /*
             * Port I/O region. Check if mapped and within 1..65535 range.
             */
            if (pRegion->type & PCI_ADDRESS_SPACE_IO)
            {
                if (u16Cmd & VBOX_PCI_COMMAND_IO)
                {
                    /* safe, only needs to go to the config space array */
                    uint32_t uIoBase = PDMPciDevGetDWord(pPciDev, offCfgReg);
                    uIoBase &= ~(uint32_t)(cbRegion - 1);

                    uint64_t uLast = cbRegion - 1 + uIoBase;
                    if (   uLast < _64K
                        && uIoBase < uLast
                        && uIoBase > 0)
                        uNew = uIoBase;
                    else
                        Log4(("devpciR3UpdateMappings: dev %u/%u (%s): region #%u: Disregarding invalid I/O port range: %#RX32..%#RX64\n",
                              pPciDev->uDevFn >> VBOX_PCI_DEVFN_DEV_SHIFT, pPciDev->uDevFn & VBOX_PCI_DEVFN_FUN_MASK,
                              pPciDev->pszNameR3, iRegion, uIoBase, uLast));
                }
            }
            /*
             * MMIO or ROM.  Check ROM enable bit and range.
             *
             * Note! We exclude the I/O-APIC/HPET/ROM area at the end of the first 4GB to
             *       prevent the (fake) PCI BIOS and others from making a mess.  Pure paranoia.
             *       Additionally addresses with the top 32 bits all set are excluded, to
             *       catch silly OSes which probe 64-bit BARs without disabling the
             *       corresponding transactions.
             *
             * Update: The pure paranoia above broke NT 3.51, so it was changed to only
             *         exclude the 64KB BIOS mapping at the top.  NT 3.51 excludes the
             *         top 256KB, btw.
             */
            /** @todo Query upper boundrary from CPUM and PGMPhysRom instead of making
             *        incorrect assumptions. */
            else if (u16Cmd & VBOX_PCI_COMMAND_MEMORY)
            {
                /* safe, only needs to go to the config space array */
                uint64_t uMemBase = PDMPciDevGetDWord(pPciDev, offCfgReg);
                if (f64Bit)
                {
                    Assert(iRegion < VBOX_PCI_ROM_SLOT);
                    /* safe, only needs to go to the config space array */
                    uMemBase |= (uint64_t)PDMPciDevGetDWord(pPciDev, offCfgReg + 4) << 32;
                }
                if (   iRegion != PCI_ROM_SLOT
                    || (uMemBase & RT_BIT_32(0))) /* ROM enable bit. */
                {
                    uMemBase &= ~(cbRegion - 1);

                    uint64_t uLast = uMemBase + cbRegion - 1;
                    if (   uMemBase < uLast
                        && uMemBase > 0)
                    {
                        if (   (   uMemBase > UINT32_C(0xffffffff)
                                || uLast    < UINT32_C(0xffff0000) ) /* UINT32_C(0xfec00000) - breaks NT3.51! */
                            && uMemBase < UINT64_C(0xffffffff00000000) )
                            uNew = uMemBase;
                        else
                            Log(("devpciR3UpdateMappings: dev %u/%u (%s): region #%u: Rejecting address range: %#RX64..%#RX64!\n",
                                 pPciDev->uDevFn >> VBOX_PCI_DEVFN_DEV_SHIFT, pPciDev->uDevFn & VBOX_PCI_DEVFN_FUN_MASK,
                                 pPciDev->pszNameR3, iRegion, uMemBase, uLast));
                    }
                    else
                        Log2(("devpciR3UpdateMappings: dev %u/%u (%s): region #%u: Disregarding invalid address range: %#RX64..%#RX64\n",
                              pPciDev->uDevFn >> VBOX_PCI_DEVFN_DEV_SHIFT, pPciDev->uDevFn & VBOX_PCI_DEVFN_FUN_MASK,
                              pPciDev->pszNameR3, iRegion, uMemBase, uLast));
                }
            }

            /*
             * Do real unmapping and/or mapping if the address change.
             */
            Log4(("devpciR3UpdateMappings: dev %u/%u (%s): iRegion=%u addr=%#RX64 uNew=%#RX64\n",
                  pPciDev->uDevFn >> VBOX_PCI_DEVFN_DEV_SHIFT, pPciDev->uDevFn & VBOX_PCI_DEVFN_FUN_MASK, pPciDev->pszNameR3,
                  iRegion, pRegion->addr, uNew));
            if (uNew != pRegion->addr)
            {
                LogRel2(("PCI: config dev %u/%u (%s) BAR%i: %#RX64 -> %#RX64 (LB %RX64 (%RU64))\n",
                         pPciDev->uDevFn >> VBOX_PCI_DEVFN_DEV_SHIFT, pPciDev->uDevFn & VBOX_PCI_DEVFN_FUN_MASK,
                         pPciDev->pszNameR3, iRegion, pRegion->addr, uNew, cbRegion, cbRegion));

                int rc = devpciR3UnmapRegion(pPciDev, iRegion);
                AssertLogRelRC(rc);
                pRegion->addr = uNew;
                if (uNew != INVALID_PCI_ADDRESS)
                {
                    /* The callout is optional (typically not used): */
                    if (!pRegion->pfnMap)
                        rc = VINF_SUCCESS;
                    else
                    {
                        rc = pRegion->pfnMap(pPciDev->Int.s.pDevInsR3, pPciDev, iRegion,
                                             uNew, cbRegion, (PCIADDRESSSPACE)(pRegion->type));
                        AssertLogRelRC(rc);
                    }

                    /* We do the mapping for most devices: */
                    if (pRegion->hHandle != UINT64_MAX && rc != VINF_PCI_MAPPING_DONE)
                    {
                        switch (pRegion->fFlags & PDMPCIDEV_IORGN_F_HANDLE_MASK)
                        {
                            case PDMPCIDEV_IORGN_F_IOPORT_HANDLE:
                                rc = PDMDevHlpIoPortMap(pPciDev->Int.s.pDevInsR3, (IOMIOPORTHANDLE)pRegion->hHandle, (RTIOPORT)uNew);
                                AssertLogRelRC(rc);
                                break;

                            case PDMPCIDEV_IORGN_F_MMIO_HANDLE:
                                rc = PDMDevHlpMmioMap(pPciDev->Int.s.pDevInsR3, (IOMMMIOHANDLE)pRegion->hHandle, uNew);
                                AssertLogRelRC(rc);
                                break;

                            case PDMPCIDEV_IORGN_F_MMIO2_HANDLE:
                                rc = PDMDevHlpMmio2Map(pPciDev->Int.s.pDevInsR3, (PGMMMIO2HANDLE)pRegion->hHandle, uNew);
                                AssertRC(rc);
                                break;

                            default:
                                AssertLogRelFailed();
                        }
                    }
                }
            }

            if (f64Bit)
                iRegion++;
        }
        /* else: size == 0: unused region */
    }

    return VINF_SUCCESS;
}


/**
 * Worker for devpciR3CommonDefaultConfigWrite that write a byte to a BAR.
 *
 * @param   pPciDev             The PCI device.
 * @param   iRegion             The region.
 * @param   off                 The BAR offset.
 * @param   bVal                The byte to write.
 */
DECLINLINE(void) devpciR3WriteBarByte(PPDMPCIDEV pPciDev, uint32_t iRegion, uint32_t off, uint8_t bVal)
{
    PCIIOREGION *pRegion = &pPciDev->Int.s.aIORegions[iRegion];
    Log3Func(("region=%d off=%d val=%#x size=%#llx\n", iRegion, off, bVal, pRegion->size));
    Assert(off <= 3);

    /* Check if we're writing to upper part of 64-bit BAR. */
    if (pRegion->type == 0xff)
    {
        AssertLogRelReturnVoid(iRegion > 0 && iRegion < VBOX_PCI_ROM_SLOT);
        pRegion--;
        iRegion--;
        off += 4;
        Assert(pRegion->type & PCI_ADDRESS_SPACE_BAR64);
    }

    /* Ignore zero sized regions (they don't exist). */
    if (pRegion->size != 0)
    {
        uint32_t uAddr = devpciGetRegionReg(iRegion) + off;
        Assert((pRegion->size & (pRegion->size - 1)) == 0); /* Region size must be power of two. */
        uint8_t bMask = ( (pRegion->size - 1) >> (off * 8) ) & 0xff;
        if (off == 0)
            bMask |= (pRegion->type & PCI_ADDRESS_SPACE_IO)
                   ? (1 << 2) - 1 /* 2 lowest bits for IO region */ :
                     (1 << 4) - 1 /* 4 lowest bits for memory region, also ROM enable bit for ROM region */;

        /* safe, only needs to go to the config space array */
        uint8_t bOld = PDMPciDevGetByte(pPciDev, uAddr) & bMask;
        bVal = (bOld & bMask) | (bVal & ~bMask);

        Log3Func(("%x changed to  %x\n", bOld, bVal));

        /* safe, only needs to go to the config space array */
        PDMPciDevSetByte(pPciDev, uAddr, bVal);
    }
}


/**
 * Checks if the given configuration byte is writable.
 *
 * @returns true if writable, false if not
 * @param   uAddress            The config space byte byte.
 * @param   bHeaderType         The device header byte.
 */
DECLINLINE(bool) devpciR3IsConfigByteWritable(uint32_t uAddress, uint8_t bHeaderType)
{
    switch (bHeaderType)
    {
        case 0x00: /* normal device */
        case 0x80: /* multi-function device */
            switch (uAddress)
            {
                /* Read-only registers. */
                case VBOX_PCI_VENDOR_ID:
                case VBOX_PCI_VENDOR_ID+1:
                case VBOX_PCI_DEVICE_ID:
                case VBOX_PCI_DEVICE_ID+1:
                case VBOX_PCI_REVISION_ID:
                case VBOX_PCI_CLASS_PROG:
                case VBOX_PCI_CLASS_SUB:
                case VBOX_PCI_CLASS_BASE:
                case VBOX_PCI_HEADER_TYPE:
                case VBOX_PCI_SUBSYSTEM_VENDOR_ID:
                case VBOX_PCI_SUBSYSTEM_VENDOR_ID+1:
                case VBOX_PCI_SUBSYSTEM_ID:
                case VBOX_PCI_SUBSYSTEM_ID+1:
                case VBOX_PCI_ROM_ADDRESS:
                case VBOX_PCI_ROM_ADDRESS+1:
                case VBOX_PCI_ROM_ADDRESS+2:
                case VBOX_PCI_ROM_ADDRESS+3:
                case VBOX_PCI_CAPABILITY_LIST:
                case VBOX_PCI_INTERRUPT_PIN:
                    return false;
                /* Other registers can be written. */
                default:
                    return true;
            }
            break;
        case 0x01: /* PCI-PCI bridge */
            switch (uAddress)
            {
                /* Read-only registers. */
                case VBOX_PCI_VENDOR_ID:
                case VBOX_PCI_VENDOR_ID+1:
                case VBOX_PCI_DEVICE_ID:
                case VBOX_PCI_DEVICE_ID+1:
                case VBOX_PCI_REVISION_ID:
                case VBOX_PCI_CLASS_PROG:
                case VBOX_PCI_CLASS_SUB:
                case VBOX_PCI_CLASS_BASE:
                case VBOX_PCI_HEADER_TYPE:
                case VBOX_PCI_ROM_ADDRESS_BR:
                case VBOX_PCI_ROM_ADDRESS_BR+1:
                case VBOX_PCI_ROM_ADDRESS_BR+2:
                case VBOX_PCI_ROM_ADDRESS_BR+3:
                case VBOX_PCI_INTERRUPT_PIN:
                    return false;
                /* Other registers can be written. */
                default:
                    return true;
            }
            break;
        default:
            AssertMsgFailed(("Unknown header type %#x\n", bHeaderType));
            return false;
    }
}


/**
 * Writes config space for a device, ignoring interceptors.
 *
 * See paragraph 7.5 of PCI Express specification (p. 349) for
 * definition of registers and their writability policy.
 */
DECLHIDDEN(VBOXSTRICTRC) devpciR3CommonConfigWriteWorker(PPDMDEVINS pDevIns, PDEVPCIBUSCC pBusCC,
                                                         PPDMPCIDEV pPciDev, uint32_t uAddress, unsigned cb, uint32_t u32Value)
{
    Assert(cb <= 4 && cb != 3);
    VBOXSTRICTRC rcStrict = VINF_SUCCESS;

    if (uAddress + cb <= RT_MIN(pPciDev->cbConfig, sizeof(pPciDev->abConfig)))
    {
        /*
         * MSI and MSI-X capabilites needs to be handled separately.
         */
        if (   pciDevIsMsiCapable(pPciDev)
            && uAddress - (uint32_t)pPciDev->Int.s.u8MsiCapOffset < (uint32_t)pPciDev->Int.s.u8MsiCapSize)
            MsiR3PciConfigWrite(pDevIns, pBusCC->CTX_SUFF(pPciHlp), pPciDev, uAddress, u32Value, cb);
        else if (   pciDevIsMsixCapable(pPciDev)
                 && uAddress - (uint32_t)pPciDev->Int.s.u8MsixCapOffset < (uint32_t)pPciDev->Int.s.u8MsixCapSize)
            MsixR3PciConfigWrite(pDevIns, pBusCC->CTX_SUFF(pPciHlp), pPciDev, uAddress, u32Value, cb);
        else
        {
            /*
             * Handle the writes byte-by-byte to catch all possible cases.
             *
             * Note! Real hardware may not necessarily handle non-dword writes like
             *       we do here and even produce erratic behavior.  We don't (yet)
             *       try emulate that.
             */
            uint8_t const   bHeaderType     = devpciR3GetByte(pPciDev, VBOX_PCI_HEADER_TYPE);
            bool const      fP2PBridge      = bHeaderType == 0x01; /* PCI-PCI bridge */
            bool            fUpdateMappings = false;
            while (cb-- > 0)
            {
                bool    fWritable = devpciR3IsConfigByteWritable(uAddress, bHeaderType);
                uint8_t bVal = (uint8_t)u32Value;
                bool    fRom = false;
                switch (uAddress)
                {
                    case VBOX_PCI_COMMAND: /* Command register, bits 0-7. */
                        if (fWritable)
                        {
                            /* safe, only needs to go to the config space array */
                            PDMPciDevSetByte(pPciDev, uAddress, bVal);
                            fUpdateMappings = true;
                        }
                        break;

                    case VBOX_PCI_COMMAND+1: /* Command register, bits 8-15. */
                        if (fWritable)
                        {
                            /* don't change reserved bits (11-15) */
                            bVal &= ~UINT8_C(0xf8);
                            /* safe, only needs to go to the config space array */
                            PDMPciDevSetByte(pPciDev, uAddress, bVal);
                            fUpdateMappings = true;
                        }
                        break;

                    case VBOX_PCI_STATUS:  /* Status register, bits 0-7. */
                        /* don't change read-only bits => actually all lower bits are read-only */
                        bVal &= ~UINT8_C(0xff);
                        /* status register, low part: clear bits by writing a '1' to the corresponding bit */
                        pPciDev->abConfig[uAddress] &= ~bVal;
                        break;

                    case VBOX_PCI_STATUS+1:  /* Status register, bits 8-15. */
                        /* don't change read-only bits */
                        bVal &= ~UINT8_C(0x06);
                        /* status register, high part: clear bits by writing a '1' to the corresponding bit */
                        pPciDev->abConfig[uAddress] &= ~bVal;
                        break;

                    case VBOX_PCI_ROM_ADDRESS:    case VBOX_PCI_ROM_ADDRESS   +1: case VBOX_PCI_ROM_ADDRESS   +2: case VBOX_PCI_ROM_ADDRESS   +3:
                        fRom = true;
                        RT_FALL_THRU();
                    case VBOX_PCI_BASE_ADDRESS_0: case VBOX_PCI_BASE_ADDRESS_0+1: case VBOX_PCI_BASE_ADDRESS_0+2: case VBOX_PCI_BASE_ADDRESS_0+3:
                    case VBOX_PCI_BASE_ADDRESS_1: case VBOX_PCI_BASE_ADDRESS_1+1: case VBOX_PCI_BASE_ADDRESS_1+2: case VBOX_PCI_BASE_ADDRESS_1+3:
                    case VBOX_PCI_BASE_ADDRESS_2: case VBOX_PCI_BASE_ADDRESS_2+1: case VBOX_PCI_BASE_ADDRESS_2+2: case VBOX_PCI_BASE_ADDRESS_2+3:
                    case VBOX_PCI_BASE_ADDRESS_3: case VBOX_PCI_BASE_ADDRESS_3+1: case VBOX_PCI_BASE_ADDRESS_3+2: case VBOX_PCI_BASE_ADDRESS_3+3:
                    case VBOX_PCI_BASE_ADDRESS_4: case VBOX_PCI_BASE_ADDRESS_4+1: case VBOX_PCI_BASE_ADDRESS_4+2: case VBOX_PCI_BASE_ADDRESS_4+3:
                    case VBOX_PCI_BASE_ADDRESS_5: case VBOX_PCI_BASE_ADDRESS_5+1: case VBOX_PCI_BASE_ADDRESS_5+2: case VBOX_PCI_BASE_ADDRESS_5+3:
                        /* We check that, as same PCI register numbers as BARs may mean different registers for bridges */
                        if (!fP2PBridge)
                        {
                            uint32_t iRegion = fRom ? VBOX_PCI_ROM_SLOT : (uAddress - VBOX_PCI_BASE_ADDRESS_0) >> 2;
                            devpciR3WriteBarByte(pPciDev, iRegion, uAddress & 0x3, bVal);
                            fUpdateMappings = true;
                            break;
                        }
                        if (uAddress < VBOX_PCI_BASE_ADDRESS_2 || uAddress > VBOX_PCI_BASE_ADDRESS_5+3)
                        {
                            /* PCI bridges have only BAR0, BAR1 and ROM */
                            uint32_t iRegion = fRom ? VBOX_PCI_ROM_SLOT : (uAddress - VBOX_PCI_BASE_ADDRESS_0) >> 2;
                            devpciR3WriteBarByte(pPciDev, iRegion, uAddress & 0x3, bVal);
                            fUpdateMappings = true;
                            break;
                        }
                        if (   uAddress == VBOX_PCI_IO_BASE
                            || uAddress == VBOX_PCI_IO_LIMIT
                            || uAddress == VBOX_PCI_MEMORY_BASE
                            || uAddress == VBOX_PCI_MEMORY_LIMIT
                            || uAddress == VBOX_PCI_PREF_MEMORY_BASE
                            || uAddress == VBOX_PCI_PREF_MEMORY_LIMIT)
                        {
                            /* All bridge address decoders have the low 4 bits
                             * as readonly, and all but the prefetchable ones
                             * have the low 4 bits as 0 (the prefetchable have
                             * it as 1 to show the 64-bit decoder support. */
                            bVal &= 0xf0;
                            if (   uAddress == VBOX_PCI_PREF_MEMORY_BASE
                                || uAddress == VBOX_PCI_PREF_MEMORY_LIMIT)
                                bVal |= 0x01;
                        }
                        /* (bridge config space which isn't a BAR) */
                        RT_FALL_THRU();
                    default:
                        if (fWritable)
                            /* safe, only needs to go to the config space array */
                            PDMPciDevSetByte(pPciDev, uAddress, bVal);
                        break;
                }
                uAddress++;
                u32Value >>= 8;
            }

            /*
             * Update the region mappings if anything changed related to them (command, BARs, ROM).
             */
            if (fUpdateMappings)
                rcStrict = devpciR3UpdateMappings(pPciDev, fP2PBridge);
        }
    }
    else
        AssertMsgFailed(("Write after end of PCI config space: %#x LB %u\n", uAddress, cb));

    return rcStrict;
}


/**
 * @interface_method_impl{PDMPCIBUSREGR3,pfnConfigWrite}
 */
DECLCALLBACK(VBOXSTRICTRC) devpciR3CommonConfigWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                     uint32_t uAddress, unsigned cb, uint32_t u32Value)
{
    PDEVPCIBUSCC pBusCC = PDMINS_2_DATA_CC(pDevIns, PDEVPCIBUSCC);
    return devpciR3CommonConfigWriteWorker(pDevIns, pBusCC, pPciDev, uAddress, cb, u32Value);
}


/* -=-=-=-=-=- Debug Info Handlers -=-=-=-=-=- */

/**
 * Indents an info line.
 * @param   pHlp                The info helper.
 * @param   iIndentLvl          The desired indentation level.
 */
static void devpciR3InfoIndent(PCDBGFINFOHLP pHlp, unsigned iIndentLvl)
{
    for (unsigned i = 0; i < iIndentLvl; i++)
        pHlp->pfnPrintf(pHlp, "    ");
}

static const char *devpciR3InInfoPciBusClassName(uint8_t iBaseClass)
{
    static const char *s_szBaseClass[] =
    {
        /* 00h */ "unknown",
        /* 01h */ "mass storage controller",
        /* 02h */ "network controller",
        /* 03h */ "display controller",
        /* 04h */ "multimedia controller",
        /* 05h */ "memory controller",
        /* 06h */ "bridge device",
        /* 07h */ "simple communication controllers",
        /* 08h */ "base system peripherals",
        /* 09h */ "input devices",
        /* 0Ah */ "docking stations",
        /* 0Bh */ "processors",
        /* 0Ch */ "serial bus controllers",
        /* 0Dh */ "wireless controller",
        /* 0Eh */ "intelligent I/O controllers",
        /* 0Fh */ "satellite communication controllers",
        /* 10h */ "encryption/decryption controllers",
        /* 11h */ "data acquisition and signal processing controllers"
    };
    if (iBaseClass < RT_ELEMENTS(s_szBaseClass))
        return s_szBaseClass[iBaseClass];
    if (iBaseClass < 0xFF)
        return "reserved";
    return "device does not fit in any defined classes";
}

static const char *devpciR3InInfoPciBusType(DEVPCIBUSTYPE enmType)
{
    static const char *s_szBusType[] =
    {
        /* 00h */ "INVALID",
        /* 01h */ "PIIX3",
        /* 02h */ "ICH9",
        /* 03h */ "GenericEcam",
        /* 04h */ "?32-bit hack?",
    };

    if (enmType < RT_ELEMENTS(s_szBusType))
        return s_szBusType[enmType];
    return "?type?";
}

/**
 * Recursive worker for devpciR3InfoPci.
 *
 * @param   pBus                The bus to show info for.
 * @param   pHlp                The info helpers.
 * @param   iIndentLvl          The indentation level.
 * @param   fRegisters          Whether to show device registers or not.
 */
static void devpciR3InfoPciBus(PDEVPCIBUS pBus, PCDBGFINFOHLP pHlp, unsigned iIndentLvl, bool fRegisters)
{
    /* This has to use the callbacks for accuracy reasons. Otherwise it can get
     * confusing in the passthrough case or when the callbacks for some device
     * are doing something non-trivial (like implementing an indirect
     * passthrough approach), because then the abConfig array is an imprecise
     * cache needed for efficiency (so that certain reads can be done from
     * R0/RC), but far from authoritative or what the guest would see. */

    for (uint32_t uDevFn = 0; uDevFn < RT_ELEMENTS(pBus->apDevices); uDevFn++)
    {
        PPDMPCIDEV pPciDev = pBus->apDevices[uDevFn];
        if (pPciDev != NULL)
        {
            devpciR3InfoIndent(pHlp, iIndentLvl);

            /*
             * For passthrough devices MSI/MSI-X mostly reflects the way interrupts delivered to the guest,
             * as host driver handles real devices interrupts.
             */
            pHlp->pfnPrintf(pHlp, "%02x:%02x.%d %s%s: %04x-%04x %s%s%s",
                            pBus->iBus, (uDevFn >> 3) & 0xff, uDevFn & 0x7,
                            pPciDev->pszNameR3,
                            pciDevIsPassthrough(pPciDev) ? " (PASSTHROUGH)" : "",
                            devpciR3GetWord(pPciDev, VBOX_PCI_VENDOR_ID), devpciR3GetWord(pPciDev, VBOX_PCI_DEVICE_ID),
                            devpciR3InInfoPciBusType(pBus->enmType),
                            pciDevIsMsiCapable(pPciDev)  ? " MSI" : "",
                            pciDevIsMsixCapable(pPciDev) ? " MSI-X" : ""
                            );
            if (devpciR3GetByte(pPciDev, VBOX_PCI_INTERRUPT_PIN) != 0)
            {
                pHlp->pfnPrintf(pHlp, " IRQ%d", devpciR3GetByte(pPciDev, VBOX_PCI_INTERRUPT_LINE));
                pHlp->pfnPrintf(pHlp, " (INTA#->IRQ%d)", 0x10 + ich9pciSlot2ApicIrq(uDevFn >> 3, 0));
            }
            pHlp->pfnPrintf(pHlp, "\n");
            devpciR3InfoIndent(pHlp, iIndentLvl + 2);
            uint8_t uClassBase = devpciR3GetByte(pPciDev, VBOX_PCI_CLASS_BASE);
            uint8_t uClassSub  = devpciR3GetByte(pPciDev, VBOX_PCI_CLASS_SUB);
            pHlp->pfnPrintf(pHlp, "Class base/sub: %02x%02x (%s)\n",
                            uClassBase, uClassSub, devpciR3InInfoPciBusClassName(uClassBase));

            if (pciDevIsMsiCapable(pPciDev) || pciDevIsMsixCapable(pPciDev))
            {
                devpciR3InfoIndent(pHlp, iIndentLvl + 2);

                if (pciDevIsMsiCapable(pPciDev))
                    pHlp->pfnPrintf(pHlp, "MSI: %s ", MsiIsEnabled(pPciDev) ? "on" : "off");

                if (pciDevIsMsixCapable(pPciDev))
                    pHlp->pfnPrintf(pHlp, "MSI-X: %s ", MsixIsEnabled(pPciDev) ? "on" : "off");

                pHlp->pfnPrintf(pHlp, "\n");
            }

            for (unsigned iRegion = 0; iRegion < VBOX_PCI_NUM_REGIONS; iRegion++)
            {
                PCIIOREGION const *pRegion  = &pPciDev->Int.s.aIORegions[iRegion];
                uint64_t const     cbRegion = pRegion->size;

                if (cbRegion == 0)
                    continue;

                uint32_t uAddr = devpciR3GetDWord(pPciDev, devpciGetRegionReg(iRegion));
                const char * pszDesc;
                char szDescBuf[128];

                bool f64Bit =    (pRegion->type & ((uint8_t)(PCI_ADDRESS_SPACE_BAR64 | PCI_ADDRESS_SPACE_IO)))
                              == PCI_ADDRESS_SPACE_BAR64;
                if (pRegion->type & PCI_ADDRESS_SPACE_IO)
                {
                    pszDesc = "IO";
                    uAddr &= ~0x3;
                }
                else
                {
                    RTStrPrintf(szDescBuf, sizeof(szDescBuf), "MMIO%s%s",
                                f64Bit ? "64" : "32",
                                pRegion->type & PCI_ADDRESS_SPACE_MEM_PREFETCH ? " PREFETCH" : "");
                    pszDesc = szDescBuf;
                    uAddr &= ~0xf;
                }

                devpciR3InfoIndent(pHlp, iIndentLvl + 2);
                pHlp->pfnPrintf(pHlp, "%s region #%u: ", pszDesc, iRegion);
                if (f64Bit)
                {
                    uint32_t u32High = devpciR3GetDWord(pPciDev, devpciGetRegionReg(iRegion+1));
                    uint64_t u64Addr = RT_MAKE_U64(uAddr, u32High);
                    pHlp->pfnPrintf(pHlp, "%RX64..%RX64\n", u64Addr, u64Addr + cbRegion - 1);
                    iRegion++;
                }
                else
                    pHlp->pfnPrintf(pHlp, "%x..%x\n", uAddr, uAddr + (uint32_t)cbRegion - 1);
            }

            devpciR3InfoIndent(pHlp, iIndentLvl + 2);
            uint16_t iCmd = devpciR3GetWord(pPciDev, VBOX_PCI_COMMAND);
            uint16_t iStatus = devpciR3GetWord(pPciDev, VBOX_PCI_STATUS);
            pHlp->pfnPrintf(pHlp, "Command: %04x, Status: %04x\n", iCmd, iStatus);
            devpciR3InfoIndent(pHlp, iIndentLvl + 2);
            pHlp->pfnPrintf(pHlp, "Bus master: %s\n", iCmd & VBOX_PCI_COMMAND_MASTER ? "Yes" : "No");
            if (iCmd != PDMPciDevGetCommand(pPciDev))
            {
                devpciR3InfoIndent(pHlp, iIndentLvl + 2);
                pHlp->pfnPrintf(pHlp, "CACHE INCONSISTENCY: Command: %04x\n", PDMPciDevGetCommand(pPciDev));
            }

            if (fRegisters)
            {
                devpciR3InfoIndent(pHlp, iIndentLvl + 2);
                pHlp->pfnPrintf(pHlp, "PCI registers:\n");
                for (unsigned iReg = 0; iReg < 0x100; )
                {
                    unsigned iPerLine = 0x10;
                    Assert(0x100 % iPerLine == 0);
                    devpciR3InfoIndent(pHlp, iIndentLvl + 3);

                    while (iPerLine-- > 0)
                        pHlp->pfnPrintf(pHlp, "%02x ", devpciR3GetByte(pPciDev, iReg++));
                    pHlp->pfnPrintf(pHlp, "\n");
                }
            }
        }
    }

    if (pBus->cBridges > 0)
    {
        devpciR3InfoIndent(pHlp, iIndentLvl);
        pHlp->pfnPrintf(pHlp, "Registered %d bridges, subordinate buses info follows\n", pBus->cBridges);
        for (uint32_t iBridge = 0; iBridge < pBus->cBridges; iBridge++)
        {
            PPDMDEVINS pDevInsSub = pBus->papBridgesR3[iBridge]->Int.s.CTX_SUFF(pDevIns);
            PPDMPCIDEV pPciDevSub = pDevInsSub->apPciDevs[0];
            PDEVPCIBUS pBusSub    = PDMINS_2_DATA(pDevInsSub, PDEVPCIBUS);
            uint8_t uPrimary = devpciR3GetByte(pPciDevSub, VBOX_PCI_PRIMARY_BUS);
            uint8_t uSecondary = devpciR3GetByte(pPciDevSub, VBOX_PCI_SECONDARY_BUS);
            uint8_t uSubordinate = devpciR3GetByte(pPciDevSub, VBOX_PCI_SUBORDINATE_BUS);
            devpciR3InfoIndent(pHlp, iIndentLvl);
            pHlp->pfnPrintf(pHlp, "%02x:%02x.%d: bridge topology: primary=%d secondary=%d subordinate=%d\n",
                            uPrimary, pPciDevSub->uDevFn >> 3, pPciDevSub->uDevFn & 7,
                            uPrimary, uSecondary, uSubordinate);
            if (   uPrimary != PDMPciDevGetByte(pPciDevSub, VBOX_PCI_PRIMARY_BUS)
                || uSecondary != PDMPciDevGetByte(pPciDevSub, VBOX_PCI_SECONDARY_BUS)
                || uSubordinate != PDMPciDevGetByte(pPciDevSub, VBOX_PCI_SUBORDINATE_BUS))
            {
                devpciR3InfoIndent(pHlp, iIndentLvl);
                pHlp->pfnPrintf(pHlp, "CACHE INCONSISTENCY: primary=%d secondary=%d subordinate=%d\n",
                                PDMPciDevGetByte(pPciDevSub, VBOX_PCI_PRIMARY_BUS),
                                PDMPciDevGetByte(pPciDevSub, VBOX_PCI_SECONDARY_BUS),
                                PDMPciDevGetByte(pPciDevSub, VBOX_PCI_SUBORDINATE_BUS));
            }
            devpciR3InfoIndent(pHlp, iIndentLvl);
            pHlp->pfnPrintf(pHlp, "behind bridge: ");
            uint8_t uIoBase  = devpciR3GetByte(pPciDevSub, VBOX_PCI_IO_BASE);
            uint8_t uIoLimit = devpciR3GetByte(pPciDevSub, VBOX_PCI_IO_LIMIT);
            pHlp->pfnPrintf(pHlp, "I/O %#06x..%#06x",
                            (uIoBase & 0xf0) << 8,
                            (uIoLimit & 0xf0) << 8 | 0xfff);
            if (uIoBase > uIoLimit)
                pHlp->pfnPrintf(pHlp, " (IGNORED)");
            pHlp->pfnPrintf(pHlp, "\n");
            devpciR3InfoIndent(pHlp, iIndentLvl);
            pHlp->pfnPrintf(pHlp, "behind bridge: ");
            uint32_t uMemoryBase  = devpciR3GetWord(pPciDevSub, VBOX_PCI_MEMORY_BASE);
            uint32_t uMemoryLimit = devpciR3GetWord(pPciDevSub, VBOX_PCI_MEMORY_LIMIT);
            pHlp->pfnPrintf(pHlp, "memory %#010x..%#010x",
                            (uMemoryBase & 0xfff0) << 16,
                            (uMemoryLimit & 0xfff0) << 16 | 0xfffff);
            if (uMemoryBase > uMemoryLimit)
                pHlp->pfnPrintf(pHlp, " (IGNORED)");
            pHlp->pfnPrintf(pHlp, "\n");
            devpciR3InfoIndent(pHlp, iIndentLvl);
            pHlp->pfnPrintf(pHlp, "behind bridge: ");
            uint32_t uPrefMemoryRegBase  = devpciR3GetWord(pPciDevSub, VBOX_PCI_PREF_MEMORY_BASE);
            uint32_t uPrefMemoryRegLimit = devpciR3GetWord(pPciDevSub, VBOX_PCI_PREF_MEMORY_LIMIT);
            uint64_t uPrefMemoryBase = (uPrefMemoryRegBase & 0xfff0) << 16;
            uint64_t uPrefMemoryLimit = (uPrefMemoryRegLimit & 0xfff0) << 16 | 0xfffff;
            if (   (uPrefMemoryRegBase & 0xf) == 1
                && (uPrefMemoryRegLimit & 0xf) == 1)
            {
                uPrefMemoryBase |= (uint64_t)devpciR3GetDWord(pPciDevSub, VBOX_PCI_PREF_BASE_UPPER32) << 32;
                uPrefMemoryLimit |= (uint64_t)devpciR3GetDWord(pPciDevSub, VBOX_PCI_PREF_LIMIT_UPPER32) << 32;
                pHlp->pfnPrintf(pHlp, "64-bit ");
            }
            else
                pHlp->pfnPrintf(pHlp, "32-bit ");
            pHlp->pfnPrintf(pHlp, "prefetch memory %#018llx..%#018llx", uPrefMemoryBase, uPrefMemoryLimit);
            if (uPrefMemoryBase > uPrefMemoryLimit)
                pHlp->pfnPrintf(pHlp, " (IGNORED)");
            pHlp->pfnPrintf(pHlp, "\n");
            devpciR3InfoPciBus(pBusSub, pHlp, iIndentLvl + 1, fRegisters);
        }
    }
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV, 'pci'}
 */
DECLCALLBACK(void) devpciR3InfoPci(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PDEVPCIBUS pBus = DEVINS_2_DEVPCIBUS(pDevIns);

    if (pszArgs == NULL || !*pszArgs || !strcmp(pszArgs, "basic"))
        devpciR3InfoPciBus(pBus, pHlp, 0 /*iIndentLvl*/, false /*fRegisters*/);
    else if (!strcmp(pszArgs, "verbose"))
        devpciR3InfoPciBus(pBus, pHlp, 0 /*iIndentLvl*/, true /*fRegisters*/);
    else
        pHlp->pfnPrintf(pHlp, "Invalid argument. Recognized arguments are 'basic', 'verbose'.\n");
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV, 'pciirq'}
 */
DECLCALLBACK(void) devpciR3InfoPciIrq(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PDEVPCIROOT pPciRoot = PDMINS_2_DATA(pDevIns, PDEVPCIROOT);
    NOREF(pszArgs);

    pHlp->pfnPrintf(pHlp, "PCI I/O APIC IRQ levels:\n");
    for (int i = 0; i < DEVPCI_APIC_IRQ_PINS; ++i)
        pHlp->pfnPrintf(pHlp, "  IRQ%02d: %u\n", 0x10 + i, pPciRoot->auPciApicIrqLevels[i]);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) ich9pciR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE  pCfg)
{
    RT_NOREF1(iInstance);
    Assert(iInstance == 0);
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);

    PDEVPCIBUSCC    pBusCC   = PDMINS_2_DATA_CC(pDevIns, PDEVPCIBUSCC);
    PDEVPCIROOT     pPciRoot = PDMINS_2_DATA(pDevIns, PDEVPCIROOT);
    PCPDMDEVHLPR3   pHlp     = pDevIns->pHlpR3;
    PDEVPCIBUS      pBus     = &pPciRoot->PciBus;
    Assert(ASMMemIsZero(pPciRoot, sizeof(*pPciRoot))); /* code used to memset it for some funny reason. just temp insurance. */

    /*
     * Validate and read configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "IOAPIC|McfgBase|McfgLength", "");

    /* query whether we got an IOAPIC */
    int rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "IOAPIC", &pPciRoot->fUseIoApic, false /** @todo default to true? */);
    AssertRCReturn(rc, PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to query boolean value \"IOAPIC\"")));

    if (!pPciRoot->fUseIoApic)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Must use IO-APIC with ICH9 chipset"));

    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "McfgBase", &pPciRoot->u64PciConfigMMioAddress, 0);
    AssertRCReturn(rc, PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"McfgBase\"")));

    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "McfgLength", &pPciRoot->u64PciConfigMMioLength, 0);
    AssertRCReturn(rc, PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"McfgLength\"")));

    Log(("PCI: fUseIoApic=%RTbool McfgBase=%#RX64 McfgLength=%#RX64 fR0Enabled=%RTbool fRCEnabled=%RTbool\n", pPciRoot->fUseIoApic,
         pPciRoot->u64PciConfigMMioAddress, pPciRoot->u64PciConfigMMioLength, pDevIns->fR0Enabled, pDevIns->fRCEnabled));

    /*
     * Init data.
     */
    /* And fill values */
    pBusCC->pDevInsR3               = pDevIns;
    pPciRoot->hIoPortAddress        = NIL_IOMIOPORTHANDLE;
    pPciRoot->hIoPortData           = NIL_IOMIOPORTHANDLE;
    pPciRoot->hIoPortMagic          = NIL_IOMIOPORTHANDLE;
    pPciRoot->hMmioMcfg             = NIL_IOMMMIOHANDLE;
    pPciRoot->PciBus.enmType        = DEVPCIBUSTYPE_ICH9;
    pPciRoot->PciBus.fPureBridge    = false;
    pPciRoot->PciBus.papBridgesR3   = (PPDMPCIDEV *)PDMDevHlpMMHeapAllocZ(pDevIns, sizeof(PPDMPCIDEV) * RT_ELEMENTS(pPciRoot->PciBus.apDevices));
    AssertLogRelReturn(pPciRoot->PciBus.papBridgesR3, VERR_NO_MEMORY);

    /*
     * Disable default device locking.
     */
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /*
     * Register bus
     */
    PDMPCIBUSREGCC PciBusReg;
    PciBusReg.u32Version                 = PDM_PCIBUSREGCC_VERSION;
    PciBusReg.pfnRegisterR3              = devpciR3CommonRegisterDevice;
    PciBusReg.pfnRegisterMsiR3           = ich9pciRegisterMsi;
    PciBusReg.pfnIORegionRegisterR3      = devpciR3CommonIORegionRegister;
    PciBusReg.pfnInterceptConfigAccesses = devpciR3CommonInterceptConfigAccesses;
    PciBusReg.pfnConfigRead              = devpciR3CommonConfigRead;
    PciBusReg.pfnConfigWrite             = devpciR3CommonConfigWrite;
    PciBusReg.pfnSetIrqR3                = ich9pciSetIrq;
    PciBusReg.u32EndVersion              = PDM_PCIBUSREGCC_VERSION;
    rc = PDMDevHlpPCIBusRegister(pDevIns, &PciBusReg, &pBusCC->pPciHlpR3, &pBus->iBus);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to register ourselves as a PCI Bus"));
    Assert(pBus->iBus == 0);
    if (pBusCC->pPciHlpR3->u32Version != PDM_PCIHLPR3_VERSION)
        return PDMDevHlpVMSetError(pDevIns, VERR_VERSION_MISMATCH, RT_SRC_POS,
                                   N_("PCI helper version mismatch; got %#x expected %#x"),
                                   pBusCC->pPciHlpR3->u32Version, PDM_PCIHLPR3_VERSION);

    /*
     * Fill in PCI configs and add them to the bus.
     */
    /** @todo Disabled for now because this causes error messages with Linux guests.
     *         The guest loads the x38_edac device which tries to map a memory region
     *         using an address given at place 0x48 - 0x4f in the PCI config space.
     *         This fails. because we don't register such a region.
     */
#if 0
    /* Host bridge device */
    PDMPciDevSetVendorId(  &pBus->PciDev, 0x8086); /* Intel */
    PDMPciDevSetDeviceId(  &pBus->PciDev, 0x29e0); /* Desktop */
    PDMPciDevSetRevisionId(&pBus->PciDev,   0x01); /* rev. 01 */
    PDMPciDevSetClassBase( &pBus->PciDev,   0x06); /* bridge */
    PDMPciDevSetClassSub(  &pBus->PciDev,   0x00); /* Host/PCI bridge */
    PDMPciDevSetClassProg( &pBus->PciDev,   0x00); /* Host/PCI bridge */
    PDMPciDevSetHeaderType(&pBus->PciDev,   0x00); /* bridge */
    PDMPciDevSetWord(&pBus->PciDev,  VBOX_PCI_SEC_STATUS, 0x0280);  /* secondary status */

    pBus->PciDev.pDevIns               = pDevIns;
    /* We register Host<->PCI controller on the bus */
    ich9pciRegisterInternal(pBus, 0, &pBus->PciDev, "dram");
#endif

    /*
     * Register I/O ports.
     */
    static const IOMIOPORTDESC s_aAddrDesc[] = { { "PCI address", "PCI address", NULL, NULL }, { NULL, NULL, NULL, NULL } };
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, 0x0cf8, 1, ich9pciIOPortAddressWrite, ich9pciIOPortAddressRead,
                                     "ICH9 (PCI)", s_aAddrDesc, &pPciRoot->hIoPortAddress);
    AssertLogRelRCReturn(rc, rc);

    static const IOMIOPORTDESC s_aDataDesc[] = { { "PCI data", "PCI data", NULL, NULL }, { NULL, NULL, NULL, NULL } };
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, 0x0cfc, 4, ich9pciIOPortDataWrite, ich9pciIOPortDataRead,
                                     "ICH9 (PCI)", s_aDataDesc, &pPciRoot->hIoPortData);
    AssertLogRelRCReturn(rc, rc);

    static const IOMIOPORTDESC s_aMagicDesc[] = { { "PCI magic", NULL, NULL, NULL }, { NULL, NULL, NULL, NULL } };
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, 0x0410, 1, ich9pciR3IOPortMagicPCIWrite, ich9pciR3IOPortMagicPCIRead,
                                     "ICH9 (Fake PCI BIOS trigger)", s_aMagicDesc, &pPciRoot->hIoPortMagic);
    AssertLogRelRCReturn(rc, rc);

    /*
     * MMIO handlers.
     */
    if (pPciRoot->u64PciConfigMMioAddress != 0)
    {
        rc = PDMDevHlpMmioCreateAndMap(pDevIns, pPciRoot->u64PciConfigMMioAddress, pPciRoot->u64PciConfigMMioLength,
                                       devpciCommonMcfgMmioWrite, devpciCommonMcfgMmioRead,
                                       IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU,
                                       "MCFG ranges", &pPciRoot->hMmioMcfg);
        AssertMsgRCReturn(rc, ("rc=%Rrc %#RX64/%#RX64\n", rc,  pPciRoot->u64PciConfigMMioAddress, pPciRoot->u64PciConfigMMioLength), rc);
    }

    /*
     * Saved state and info handlers.
     */
    rc = PDMDevHlpSSMRegisterEx(pDevIns, VBOX_ICH9PCI_SAVED_STATE_VERSION,
                                sizeof(*pBus) + 16*128, "pgm",
                                NULL, NULL, NULL,
                                NULL, devpciR3CommonSaveExec, NULL,
                                NULL, devpciR3CommonLoadExec, NULL);
    AssertRCReturn(rc, rc);

    /** @todo other chipset devices shall be registered too */

    PDMDevHlpDBGFInfoRegister(pDevIns, "pci",
                              "Display PCI bus status. Recognizes 'basic' or 'verbose' as arguments, defaults to 'basic'.",
                              devpciR3InfoPci);
    PDMDevHlpDBGFInfoRegister(pDevIns, "pciirq", "Display PCI IRQ state. (no arguments)", devpciR3InfoPciIrq);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) ich9pciR3Destruct(PPDMDEVINS pDevIns)
{
    PDEVPCIROOT pPciRoot = PDMINS_2_DATA(pDevIns, PDEVPCIROOT);
    if (pPciRoot->PciBus.papBridgesR3)
    {
        PDMDevHlpMMHeapFree(pDevIns, pPciRoot->PciBus.papBridgesR3);
        pPciRoot->PciBus.papBridgesR3 = NULL;
    }
    return VINF_SUCCESS;
}


/**
 * @param   pDevIns             The PCI bus device instance.
 * @param   pDev                The PCI device to reset.
 */
void devpciR3ResetDevice(PPDMDEVINS pDevIns, PPDMPCIDEV pDev)
{
    /* Clear regions */
    for (int iRegion = 0; iRegion < VBOX_PCI_NUM_REGIONS; iRegion++)
    {
        PCIIOREGION *pRegion = &pDev->Int.s.aIORegions[iRegion];
        if (pRegion->size == 0)
            continue;
        bool const f64Bit =    (pRegion->type & ((uint8_t)(PCI_ADDRESS_SPACE_BAR64 | PCI_ADDRESS_SPACE_IO)))
                            == PCI_ADDRESS_SPACE_BAR64;

        devpciR3UnmapRegion(pDev, iRegion);

        if (f64Bit)
            iRegion++;
    }

    if (pciDevIsPassthrough(pDev))
    {
        // no reset handler - we can do what we need in PDM reset handler
        /// @todo is it correct?
    }
    else
    {
        devpciR3SetWord(pDevIns, pDev, VBOX_PCI_COMMAND,
                          devpciR3GetWord(pDev, VBOX_PCI_COMMAND)
                        & ~(VBOX_PCI_COMMAND_IO | VBOX_PCI_COMMAND_MEMORY |
                            VBOX_PCI_COMMAND_MASTER | VBOX_PCI_COMMAND_SPECIAL |
                            VBOX_PCI_COMMAND_PARITY | VBOX_PCI_COMMAND_SERR |
                            VBOX_PCI_COMMAND_FAST_BACK | VBOX_PCI_COMMAND_INTX_DISABLE));

        /* Bridge device reset handlers processed later */
        if (!pciDevIsPci2PciBridge(pDev))
        {
            devpciR3SetByte(pDevIns, pDev, VBOX_PCI_CACHE_LINE_SIZE, 0x0);
            devpciR3SetByte(pDevIns, pDev, VBOX_PCI_INTERRUPT_LINE, 0x0);
        }

        /* Reset MSI message control. */
        if (pciDevIsMsiCapable(pDev))
            devpciR3SetWord(pDevIns, pDev, pDev->Int.s.u8MsiCapOffset + VBOX_MSI_CAP_MESSAGE_CONTROL,
                            devpciR3GetWord(pDev, pDev->Int.s.u8MsiCapOffset + VBOX_MSI_CAP_MESSAGE_CONTROL) & 0xff8e);

        /* Reset MSI-X message control. */
        if (pciDevIsMsixCapable(pDev))
            devpciR3SetWord(pDevIns, pDev, pDev->Int.s.u8MsixCapOffset + VBOX_MSIX_CAP_MESSAGE_CONTROL,
                            devpciR3GetWord(pDev, pDev->Int.s.u8MsixCapOffset + VBOX_MSIX_CAP_MESSAGE_CONTROL) & 0x3fff);
    }
}

/**
 * Returns the PCI express encoding for the given PCI Express Device/Port type string.
 *
 * @returns PCI express encoding.
 * @param   pszExpressPortType    The string identifier for the port/device type.
 */
DECLHIDDEN(uint8_t) devpciR3BridgeCommonGetExpressPortTypeFromString(const char *pszExpressPortType)
{
    if (!RTStrCmp(pszExpressPortType, "EndPtDev"))
        return VBOX_PCI_EXP_TYPE_ENDPOINT;
    if (!RTStrCmp(pszExpressPortType, "LegEndPtDev"))
        return VBOX_PCI_EXP_TYPE_LEG_END;
    if (!RTStrCmp(pszExpressPortType, "RootCmplxRootPort"))
        return VBOX_PCI_EXP_TYPE_ROOT_PORT;
    if (!RTStrCmp(pszExpressPortType, "ExpressSwUpstream"))
        return VBOX_PCI_EXP_TYPE_UPSTREAM;
    if (!RTStrCmp(pszExpressPortType, "ExpressSwDownstream"))
        return VBOX_PCI_EXP_TYPE_DOWNSTREAM;
    if (!RTStrCmp(pszExpressPortType, "Express2PciBridge"))
        return VBOX_PCI_EXP_TYPE_PCI_BRIDGE;
    if (!RTStrCmp(pszExpressPortType, "Pci2ExpressBridge"))
        return VBOX_PCI_EXP_TYPE_PCIE_BRIDGE;
    if (!RTStrCmp(pszExpressPortType, "RootCmplxIntEp"))
        return VBOX_PCI_EXP_TYPE_ROOT_INT_EP;
    if (!RTStrCmp(pszExpressPortType, "RootCmplxEc"))
        return VBOX_PCI_EXP_TYPE_ROOT_EC;

    AssertLogRelMsgFailedReturn(("Unknown express port type specified"), VBOX_PCI_EXP_TYPE_ROOT_INT_EP);
}

/**
 * Recursive worker for ich9pciReset.
 *
 * @param   pDevIns     ICH9 bridge (root or PCI-to-PCI) instance.
 */
DECLHIDDEN(void) devpciR3CommonResetBridge(PPDMDEVINS pDevIns)
{
    PDEVPCIBUS pBus = PDMINS_2_DATA(pDevIns, PDEVPCIBUS);

    /* PCI-specific reset for each device. */
    for (uint32_t uDevFn = 0; uDevFn < RT_ELEMENTS(pBus->apDevices); uDevFn++)
    {
        if (pBus->apDevices[uDevFn])
            devpciR3ResetDevice(pDevIns, pBus->apDevices[uDevFn]);
    }

    for (uint32_t iBridge = 0; iBridge < pBus->cBridges; iBridge++)
    {
        if (pBus->papBridgesR3[iBridge])
            devpciR3CommonResetBridge(pBus->papBridgesR3[iBridge]->Int.s.CTX_SUFF(pDevIns));
    }

    /* Reset topology config for non-root bridge. Last thing to do, otherwise
     * the secondary and subordinate are instantly unreachable. */
    if (pBus->iBus != 0)
    {
        PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];

        devpciR3SetByte(pDevIns, pPciDev, VBOX_PCI_PRIMARY_BUS, 0);
        devpciR3SetByte(pDevIns, pPciDev, VBOX_PCI_SECONDARY_BUS, 0);
        devpciR3SetByte(pDevIns, pPciDev, VBOX_PCI_SUBORDINATE_BUS, 0);
        /* Not resetting the address decoders of the bridge to 0, since the
         * PCI-to-PCI Bridge spec says that there is no default value. */
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) ich9pciReset(PPDMDEVINS pDevIns)
{
    /* Reset everything under the root bridge. */
    devpciR3CommonResetBridge(pDevIns);
}


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) ich9pcibridgeQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDEVINS pDevIns = RT_FROM_MEMBER(pInterface, PDMDEVINS, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDevIns->IBase);

    /* HACK ALERT! Special access to the PDMPCIDEV structure of an ich9pcibridge
       instance (see PDMIICH9BRIDGEPDMPCIDEV_IID for details). */
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIICH9BRIDGEPDMPCIDEV, pDevIns->apPciDevs[0]);
    return NULL;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) ich9pcibridgeR3Destruct(PPDMDEVINS pDevIns)
{
    PDEVPCIBUS pBus = PDMINS_2_DATA(pDevIns, PDEVPCIBUS);
    if (pBus->papBridgesR3)
    {
        PDMDevHlpMMHeapFree(pDevIns, pBus->papBridgesR3);
        pBus->papBridgesR3 = NULL;
    }
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) ich9pcibridgeR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;

    /*
     * Validate and read configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "ExpressEnabled|ExpressPortType", "");

    /* check if we're supposed to implement a PCIe bridge. */
    bool fExpress;
    int rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "ExpressEnabled", &fExpress, false);
    AssertRCReturn(rc, PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to query boolean value \"ExpressEnabled\"")));

    char szExpressPortType[80];
    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "ExpressPortType", szExpressPortType, sizeof(szExpressPortType), "RootCmplxIntEp");
    AssertRCReturn(rc, PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: failed to read \"ExpressPortType\" as string")));

    uint8_t const uExpressPortType = devpciR3BridgeCommonGetExpressPortTypeFromString(szExpressPortType);
    Log(("PCI/bridge#%u: fR0Enabled=%RTbool fRCEnabled=%RTbool fExpress=%RTbool uExpressPortType=%u (%s)\n",
         iInstance, pDevIns->fR0Enabled, pDevIns->fRCEnabled, fExpress, uExpressPortType, szExpressPortType));

    /*
     * Init data and register the PCI bus.
     */
    pDevIns->IBase.pfnQueryInterface = ich9pcibridgeQueryInterface;

    PDEVPCIBUSCC pBusCC = PDMINS_2_DATA_CC(pDevIns, PDEVPCIBUSCC);
    PDEVPCIBUS   pBus   = PDMINS_2_DATA(pDevIns, PDEVPCIBUS);

    pBus->enmType     = DEVPCIBUSTYPE_ICH9;
    pBus->fPureBridge = true;
    pBusCC->pDevInsR3 = pDevIns;
    pBus->papBridgesR3 = (PPDMPCIDEV *)PDMDevHlpMMHeapAllocZ(pDevIns, sizeof(PPDMPCIDEV) * RT_ELEMENTS(pBus->apDevices));
    AssertLogRelReturn(pBus->papBridgesR3, VERR_NO_MEMORY);

    PDMPCIBUSREGCC PciBusReg;
    PciBusReg.u32Version                 = PDM_PCIBUSREGCC_VERSION;
    PciBusReg.pfnRegisterR3              = devpcibridgeR3CommonRegisterDevice;
    PciBusReg.pfnRegisterMsiR3           = ich9pciRegisterMsi;
    PciBusReg.pfnIORegionRegisterR3      = devpciR3CommonIORegionRegister;
    PciBusReg.pfnInterceptConfigAccesses = devpciR3CommonInterceptConfigAccesses;
    PciBusReg.pfnConfigWrite             = devpciR3CommonConfigWrite;
    PciBusReg.pfnConfigRead              = devpciR3CommonConfigRead;
    PciBusReg.pfnSetIrqR3                = ich9pcibridgeSetIrq;
    PciBusReg.u32EndVersion              = PDM_PCIBUSREGCC_VERSION;
    rc = PDMDevHlpPCIBusRegister(pDevIns, &PciBusReg, &pBusCC->pPciHlpR3, &pBus->iBus);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to register ourselves as a PCI Bus"));
    Assert(pBus->iBus == (uint32_t)iInstance + 1); /* Can be removed when adding support for multiple bridge implementations. */
    if (pBusCC->pPciHlpR3->u32Version != PDM_PCIHLPR3_VERSION)
        return PDMDevHlpVMSetError(pDevIns, VERR_VERSION_MISMATCH, RT_SRC_POS,
                                   N_("PCI helper version mismatch; got %#x expected %#x"),
                                   pBusCC->pPciHlpR3->u32Version, PDM_PCIHLPR3_VERSION);

    LogRel(("PCI: Registered bridge instance #%u as PDM bus no %u.\n", iInstance, pBus->iBus));


    /* Disable default device locking. */
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /*
     * Fill in PCI configs and add them to the bus.
     */
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    PDMPciDevSetVendorId(  pPciDev, 0x8086); /* Intel */
    if (fExpress)
    {
        PDMPciDevSetDeviceId(pPciDev, 0x29e1); /* 82X38/X48 Express Host-Primary PCI Express Bridge. */
        PDMPciDevSetRevisionId(pPciDev, 0x01);
    }
    else
    {
        PDMPciDevSetDeviceId(pPciDev, 0x2448); /* 82801 Mobile PCI bridge. */
        PDMPciDevSetRevisionId(pPciDev, 0xf2);
    }
    PDMPciDevSetClassSub(  pPciDev,   0x04); /* pci2pci */
    PDMPciDevSetClassBase( pPciDev,   0x06); /* PCI_bridge */
    if (fExpress)
        PDMPciDevSetClassProg(pPciDev, 0x00); /* Normal decoding. */
    else
        PDMPciDevSetClassProg(pPciDev, 0x01); /* Supports subtractive decoding. */
    PDMPciDevSetHeaderType(pPciDev,   0x01); /* Single function device which adheres to the PCI-to-PCI bridge spec. */
    if (fExpress)
    {
        PDMPciDevSetCommand(pPciDev, VBOX_PCI_COMMAND_SERR);
        PDMPciDevSetStatus(pPciDev, VBOX_PCI_STATUS_CAP_LIST); /* Has capabilities. */
        PDMPciDevSetByte(pPciDev, VBOX_PCI_CACHE_LINE_SIZE, 8); /* 32 bytes */
        /* PCI Express */
        PDMPciDevSetByte(pPciDev, 0xa0 + 0, VBOX_PCI_CAP_ID_EXP); /* PCI_Express */
        PDMPciDevSetByte(pPciDev, 0xa0 + 1, 0); /* next */
        PDMPciDevSetWord(pPciDev, 0xa0 + 2,
                        /* version */ 0x2
                      | (uExpressPortType << 4));
        PDMPciDevSetDWord(pPciDev, 0xa0 + 4, VBOX_PCI_EXP_DEVCAP_RBE); /* Device capabilities. */
        PDMPciDevSetWord(pPciDev, 0xa0 + 8, 0x0000); /* Device control. */
        PDMPciDevSetWord(pPciDev, 0xa0 + 10, 0x0000); /* Device status. */
        PDMPciDevSetDWord(pPciDev, 0xa0 + 12,
                         /* Max Link Speed */ 2
                       | /* Maximum Link Width */ (16 << 4)
                       | /* Active State Power Management (ASPM) Sopport */ (0 << 10)
                       | VBOX_PCI_EXP_LNKCAP_LBNC
                       | /* Port Number */ ((2 + iInstance) << 24)); /* Link capabilities. */
        PDMPciDevSetWord(pPciDev, 0xa0 + 16, VBOX_PCI_EXP_LNKCTL_CLOCK); /* Link control. */
        PDMPciDevSetWord(pPciDev, 0xa0 + 18,
                        /* Current Link Speed */ 2
                      | /* Negotiated Link Width */ (16 << 4)
                      | VBOX_PCI_EXP_LNKSTA_SL_CLK); /* Link status. */
        PDMPciDevSetDWord(pPciDev, 0xa0 + 20,
                         /* Slot Power Limit Value */ (75 << 7)
                       | /* Physical Slot Number */ (0 << 19)); /* Slot capabilities. */
        PDMPciDevSetWord(pPciDev, 0xa0 + 24, 0x0000); /* Slot control. */
        PDMPciDevSetWord(pPciDev, 0xa0 + 26, 0x0000); /* Slot status. */
        PDMPciDevSetWord(pPciDev, 0xa0 + 28, 0x0000); /* Root control. */
        PDMPciDevSetWord(pPciDev, 0xa0 + 30, 0x0000); /* Root capabilities. */
        PDMPciDevSetDWord(pPciDev, 0xa0 + 32, 0x00000000); /* Root status. */
        PDMPciDevSetDWord(pPciDev, 0xa0 + 36, 0x00000000); /* Device capabilities 2. */
        PDMPciDevSetWord(pPciDev, 0xa0 + 40, 0x0000); /* Device control 2. */
        PDMPciDevSetWord(pPciDev, 0xa0 + 42, 0x0000); /* Device status 2. */
        PDMPciDevSetDWord(pPciDev, 0xa0 + 44,
                         /* Supported Link Speeds Vector */ (2 << 1)); /* Link capabilities 2. */
        PDMPciDevSetWord(pPciDev, 0xa0 + 48,
                        /* Target Link Speed */ 2); /* Link control 2. */
        PDMPciDevSetWord(pPciDev, 0xa0 + 50, 0x0000); /* Link status 2. */
        PDMPciDevSetDWord(pPciDev, 0xa0 + 52, 0x00000000); /* Slot capabilities 2. */
        PDMPciDevSetWord(pPciDev, 0xa0 + 56, 0x0000); /* Slot control 2. */
        PDMPciDevSetWord(pPciDev, 0xa0 + 58, 0x0000); /* Slot status 2. */
        PDMPciDevSetCapabilityList(pPciDev, 0xa0);
    }
    else
    {
        PDMPciDevSetCommand(pPciDev, 0x00);
        PDMPciDevSetStatus(pPciDev, 0x20); /* 66MHz Capable. */
    }
    PDMPciDevSetInterruptLine(pPciDev, 0x00); /* This device does not assert interrupts. */

    /*
     * This device does not generate interrupts. Interrupt delivery from
     * devices attached to the bus is unaffected.
     */
    PDMPciDevSetInterruptPin (pPciDev, 0x00);

    if (fExpress)
    {
        /** @todo r=klaus set up the PCIe config space beyond the old 256 byte
         * limit, containing additional capability descriptors. */
    }

    /*
     * Register this PCI bridge. The called function will take care on which bus we will get registered.
     */
    rc = PDMDevHlpPCIRegisterEx(pDevIns, pPciDev, PDMPCIDEVREG_F_PCI_BRIDGE, PDMPCIDEVREG_DEV_NO_FIRST_UNUSED,
                                PDMPCIDEVREG_FUN_NO_FIRST_UNUSED, "ich9pcibridge");
    AssertLogRelRCReturn(rc, rc);

    pPciDev->Int.s.pfnBridgeConfigRead  = devpciR3BridgeCommonConfigRead;
    pPciDev->Int.s.pfnBridgeConfigWrite = devpciR3BridgeCommonConfigWrite;

    /*
     * Register SSM handlers. We use the same saved state version as for the host bridge
     * to make changes easier.
     */
    rc = PDMDevHlpSSMRegisterEx(pDevIns, VBOX_ICH9PCI_SAVED_STATE_VERSION,
                                sizeof(*pBus) + 16*128,
                                "pgm" /* before */,
                                NULL, NULL, NULL,
                                NULL, devpciR3BridgeCommonSaveExec, NULL,
                                NULL, devpciR3BridgeCommonLoadExec, NULL);
    AssertLogRelRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @interface_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) ich9pciRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVPCIROOT  pPciRoot = PDMINS_2_DATA(pDevIns, PDEVPCIROOT);
    PDEVPCIBUSCC pBusCC   = PDMINS_2_DATA_CC(pDevIns, PDEVPCIBUSCC);

    /* Mirror the ring-3 device lock disabling: */
    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /* Set up the RZ PCI bus callbacks: */
    PDMPCIBUSREGCC PciBusReg;
    PciBusReg.u32Version    = PDM_PCIBUSREGCC_VERSION;
    PciBusReg.iBus          = pPciRoot->PciBus.iBus;
    PciBusReg.pfnSetIrq     = ich9pciSetIrq;
    PciBusReg.u32EndVersion = PDM_PCIBUSREGCC_VERSION;
    rc = PDMDevHlpPCIBusSetUpContext(pDevIns, &PciBusReg, &pBusCC->CTX_SUFF(pPciHlp));
    AssertRCReturn(rc, rc);

    /* Set up I/O port callbacks, except for the magic port: */
    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pPciRoot->hIoPortAddress, ich9pciIOPortAddressWrite, ich9pciIOPortAddressRead, NULL);
    AssertLogRelRCReturn(rc, rc);

    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pPciRoot->hIoPortData, ich9pciIOPortDataWrite, ich9pciIOPortDataRead, NULL);
    AssertLogRelRCReturn(rc, rc);

    /* Set up MMIO callbacks: */
    if (pPciRoot->hMmioMcfg != NIL_IOMMMIOHANDLE)
    {
        rc = PDMDevHlpMmioSetUpContext(pDevIns, pPciRoot->hMmioMcfg, devpciCommonMcfgMmioWrite, devpciCommonMcfgMmioRead, NULL /*pvUser*/);
        AssertLogRelRCReturn(rc, rc);
    }

    return rc;
}


/**
 * @interface_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) ich9pcibridgeRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVPCIBUS   pBus   = PDMINS_2_DATA(pDevIns, PDEVPCIBUS);
    PDEVPCIBUSCC pBusCC = PDMINS_2_DATA_CC(pDevIns, PDEVPCIBUSCC);

    /* Mirror the ring-3 device lock disabling: */
    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /* Set up the RZ PCI bus callbacks: */
    PDMPCIBUSREGCC PciBusReg;
    PciBusReg.u32Version    = PDM_PCIBUSREGCC_VERSION;
    PciBusReg.iBus          = pBus->iBus;
    PciBusReg.pfnSetIrq     = ich9pcibridgeSetIrq;
    PciBusReg.u32EndVersion = PDM_PCIBUSREGCC_VERSION;
    rc = PDMDevHlpPCIBusSetUpContext(pDevIns, &PciBusReg, &pBusCC->CTX_SUFF(pPciHlp));
    AssertRCReturn(rc, rc);

    return rc;
}

#endif /* !IN_RING3 */

/**
 * The PCI bus device registration structure.
 */
const PDMDEVREG g_DevicePciIch9 =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "ich9pci",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_BUS_PCI,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(DEVPCIROOT),
    /* .cbInstanceCC = */           sizeof(CTX_SUFF(DEVPCIBUS)),
    /* .cbInstanceRC = */           sizeof(DEVPCIBUSRC),
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "ICH9 PCI bridge",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           ich9pciR3Construct,
    /* .pfnDestruct = */            ich9pciR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               ich9pciReset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            NULL,
    /* .pfnSoftReset = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RING0)
    /* .pfnEarlyConstruct = */      NULL,
    /* .pfnConstruct = */           ich9pciRZConstruct,
    /* .pfnDestruct = */            NULL,
    /* .pfnFinalDestruct = */       NULL,
    /* .pfnRequest = */             NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RC)
    /* .pfnConstruct = */           ich9pciRZConstruct,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3, IN_RING0 or IN_RC!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

/**
 * The device registration structure
 * for the PCI-to-PCI bridge.
 */
const PDMDEVREG g_DevicePciIch9Bridge =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "ich9pcibridge",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_BUS_PCI,
    /* .cMaxInstances = */          ~0U,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(DEVPCIBUS),
    /* .cbInstanceCC = */           sizeof(CTX_SUFF(DEVPCIBUS)),
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "ICH9 PCI to PCI bridge",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           ich9pcibridgeR3Construct,
    /* .pfnDestruct = */            ich9pcibridgeR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               NULL, /* Must be NULL, to make sure only bus driver handles reset */
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            NULL,
    /* .pfnSoftReset = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RING0)
    /* .pfnEarlyConstruct = */      NULL,
    /* .pfnConstruct = */           ich9pcibridgeRZConstruct,
    /* .pfnDestruct = */            NULL,
    /* .pfnFinalDestruct = */       NULL,
    /* .pfnRequest = */             NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RC)
    /* .pfnConstruct = */           ich9pcibridgeRZConstruct,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3, IN_RING0 or IN_RC!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};
