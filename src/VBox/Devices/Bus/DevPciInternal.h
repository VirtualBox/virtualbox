/* $Id: DevPciInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevPCI - Common Internal Header.
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

#ifndef VBOX_INCLUDED_SRC_Bus_DevPciInternal_h
#define VBOX_INCLUDED_SRC_Bus_DevPciInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef PDMPCIDEV_INCLUDE_PRIVATE
# define PDMPCIDEV_INCLUDE_PRIVATE /* Hack to get pdmpcidevint.h included at the right point. */
#endif
#include <VBox/vmm/pdmdev.h>

#include "PciInline.h"


/**
 * Supported PCI bus types.
 */
typedef enum DEVPCIBUSTYPE
{
    /** The usual invalid type. */
    DEVPCIBUSTYPE_INVALID = 0,
    /** PIIX3 PCI bus type. */
    DEVPCIBUSTYPE_PIIX3,
    /** ICH9 PCI bus type. */
    DEVPCIBUSTYPE_ICH9,
    /** Generic ECAM PCI bus type. */
    DEVPCIBUSTYPE_GENERIC_ECAM,
    /** 32bit blowup. */
    DEVPCIBUSTYPE_32BIT_HACK = 0x7fffffff
} DEVPCIBUSTYPE;


/**
 * PCI bus shared instance data (common to both PCI buses).
 *
 * The PCI device for the bus is always the first one (PDMDEVINSR3::apPciDevs[0]).
 */
typedef struct DEVPCIBUS
{
    /** Bus number. */
    uint32_t                iBus;
    /** Number of bridges attached to the bus. */
    uint32_t                cBridges;
    /** Start device number - always zero (only for DevPCI source compat). */
    uint32_t                iDevSearch;
    /** PCI Bus type. */
    DEVPCIBUSTYPE           enmType;
    /** Set if this is a pure bridge, i.e. not part of DEVPCIGLOBALS struct. */
    uint32_t                fPureBridge : 1;
    /** Reserved for future config flags. */
    uint32_t                uReservedConfigFlags : 31;

    /** Array of bridges attached to the bus. */
    R3PTRTYPE(PPDMPCIDEV *) papBridgesR3;
    /** Cache line align apDevices. */
    uint32_t                au32Alignment1[HC_ARCH_BITS == 32 ? 2 + 8 : 8];
    /** Array of PCI devices. We assume 32 slots, each with 8 functions. */
    R3PTRTYPE(PPDMPCIDEV)   apDevices[256];
} DEVPCIBUS;
/** Pointer to PCI bus shared instance data. */
typedef DEVPCIBUS *PDEVPCIBUS;
AssertCompileMemberAlignment(DEVPCIBUS, apDevices, 64);


/**
 * PCI bus ring-3 instance data (common to both PCI buses).
 */
typedef struct DEVPCIBUSR3
{
    /** R3 pointer to the device instance. */
    PPDMDEVINSR3            pDevInsR3;
    /** Pointer to the PCI R3  helpers. */
    PCPDMPCIHLPR3           pPciHlpR3;
} DEVPCIBUSR3;
/** Pointer to PCI bus ring-3 instance data. */
typedef DEVPCIBUSR3 *PDEVPCIBUSR3;

/**
 * PCI bus ring-0 instance data (common to both PCI buses).
 */
typedef struct DEVPCIBUSR0
{
    /** R0 pointer to the device instance. */
    PPDMDEVINSR0            pDevInsR0;
    /** Pointer to the PCI R0 helpers. */
    PCPDMPCIHLPR0           pPciHlpR0;
} DEVPCIBUSR0;
/** Pointer to PCI bus ring-0 instance data. */
typedef DEVPCIBUSR0 *PDEVPCIBUSR0;

/**
 * PCI bus raw-mode instance data (common to both PCI buses).
 */
typedef struct DEVPCIBUSRC
{
    /** R0 pointer to the device instance. */
    PPDMDEVINSRC            pDevInsRC;
    /** Pointer to the PCI raw-mode helpers. */
    PCPDMPCIHLPRC           pPciHlpRC;
} DEVPCIBUSRC;
/** Pointer to PCI bus raw-mode instance data. */
typedef DEVPCIBUSRC *PDEVPCIBUSRC;

/** DEVPCIBUSR3, DEVPCIBUSR0 or DEVPCIBUSRC depending on context.  */
typedef CTX_SUFF(DEVPCIBUS)  DEVPCIBUSCC;
/** PDEVPCIBUSR3, PDEVPCIBUSR0 or PDEVPCIBUSRC depending on context.  */
typedef CTX_SUFF(PDEVPCIBUS) PDEVPCIBUSCC;


/** @def DEVPCI_APIC_IRQ_PINS
 * Number of pins for interrupts if the APIC is used.
 */
#define DEVPCI_APIC_IRQ_PINS    8
/** @def DEVPCI_LEGACY_IRQ_PINS
 * Number of pins for interrupts (PIRQ#0...PIRQ#3).
 * @remarks Labling this "legacy" might be a bit off...
 */
#define DEVPCI_LEGACY_IRQ_PINS  4


/**
 * PCI Globals - This is the host-to-pci bridge and the root bus, shared data.
 *
 * @note Only used by the root bus, not the bridges.
 */
typedef struct DEVPCIROOT
{
    /** PCI bus which is attached to the host-to-PCI bridge.
     * @note This must come first so we can share more code with the bridges!  */
    DEVPCIBUS           PciBus;

    /** I/O APIC usage flag (always true of ICH9, see constructor). */
    bool                fUseIoApic;
    /** Reserved for future config flags. */
    bool                afFutureFlags[3+4+8];
    /** Physical address of PCI config space MMIO region. */
    uint64_t            u64PciConfigMMioAddress;
    /** Length of PCI config space MMIO region. */
    uint64_t            u64PciConfigMMioLength;
    /** Physical address of PCI PIO emulation MMIO region. */
    RTGCPHYS            GCPhysMmioPioEmuBase;
    /** Length of PCI PIO emulation MMIO region. */
    RTGCPHYS            GCPhysMmioPioEmuSize;


    /** I/O APIC irq levels */
    volatile uint32_t   auPciApicIrqLevels[DEVPCI_APIC_IRQ_PINS];
    /** Value latched in Configuration Address Port (0CF8h) */
    uint32_t            uConfigReg;
    /** Alignment padding.   */
    uint32_t            u32Alignment1;
    /** PCI bus dependent data. */
    union
    {
        /** Members only used by the PIIX3 code variant.
         * (The PCI device for the PCI-to-ISA bridge is PDMDEVINSR3::apPciDevs[1].) */
        struct
        {
            /** ACPI IRQ level */
            uint32_t            iAcpiIrqLevel;
            /** ACPI PIC IRQ */
            int32_t             iAcpiIrq;
            /** Irq levels for the four PCI Irqs.
             * These count how many devices asserted the IRQ line.  If greater 0 an IRQ
             * is sent to the guest.  If it drops to 0 the IRQ is deasserted.
             * @remarks Labling this "legacy" might be a bit off...
             */
            volatile uint32_t   auPciLegacyIrqLevels[DEVPCI_LEGACY_IRQ_PINS];
        } Piix3;
        /** Members only used by the generic ECAM variant. */
        struct
        {
            /** The interrupt config for INT#A ... INT#D. */
            uint32_t            auPciIrqNr[DEVPCI_LEGACY_IRQ_PINS];
            /** Irq levels for the four PCI Irqs.
             * These count how many devices asserted the IRQ line.  If greater 0 an IRQ
             * is sent to the guest.  If it drops to 0 the IRQ is deasserted.
             */
            volatile uint32_t   auPciIrqLevels[DEVPCI_LEGACY_IRQ_PINS];
        } GenericEcam;
    } u;

    /** The address I/O port handle. */
    IOMIOPORTHANDLE         hIoPortAddress;
    /** The data I/O port handle. */
    IOMIOPORTHANDLE         hIoPortData;
    /** The magic I/O port handle. */
    IOMIOPORTHANDLE         hIoPortMagic;
    /** The MCFG MMIO region. */
    IOMMMIOHANDLE           hMmioMcfg;
    /** The PIO emulation MMIO region. */
    IOMMMIOHANDLE           hMmioPioEmu;

#if 1 /* Will be moved into the BIOS "soon". */
    /** Current bus number - obsolete (still used by DevPCI, but merge will fix that). */
    uint8_t             uPciBiosBus;
    uint8_t             abAlignment2[7];
    /** The next I/O port address which the PCI BIOS will use. */
    uint32_t            uPciBiosIo;
    /** The next MMIO address which the PCI BIOS will use. */
    uint32_t            uPciBiosMmio;
    /** The next 64-bit MMIO address which the PCI BIOS will use. */
    uint64_t            uPciBiosMmio64;
#endif

} DEVPCIROOT;
/** Pointer to PCI device globals. */
typedef DEVPCIROOT *PDEVPCIROOT;
/** Converts a PCI bus device instance pointer to a DEVPCIBUS pointer. */
#define DEVINS_2_DEVPCIBUS(pDevIns)     (&PDMINS_2_DATA(pDevIns, PDEVPCIROOT)->PciBus)
/** Converts a pointer to a PCI bus instance to a DEVPCIROOT pointer. */
#define DEVPCIBUS_2_DEVPCIROOT(pPciBus) RT_FROM_MEMBER(pPciBus, DEVPCIROOT, PciBus)


/** @def PCI_LOCK_RET
 * Acquires the PDM lock. This is a NOP if locking is disabled. */
#define PCI_LOCK_RET(pDevIns, rcBusy) \
    do { \
        int const rcLock = PDMINS_2_DATA_CC(pDevIns, PDEVPCIBUSCC)->CTX_SUFF(pPciHlp)->pfnLock((pDevIns), rcBusy); \
        if (rcLock == VINF_SUCCESS) \
        { /* likely */ } \
        else \
            return rcLock; \
    } while (0)
/** @def PCI_UNLOCK
 * Releases the PDM lock. This is a NOP if locking is disabled. */
#define PCI_UNLOCK(pDevIns) \
    PDMINS_2_DATA_CC(pDevIns, PDEVPCIBUSCC)->CTX_SUFF(pPciHlp)->pfnUnlock(pDevIns)


DECLHIDDEN(PPDMDEVINS)     devpcibridgeCommonSetIrqRootWalk(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq,
                                                            PDEVPCIBUS *ppBus, uint8_t *puDevFnBridge, int *piIrqPinBridge);

DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) devpciCommonMcfgMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb);
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) devpciCommonMcfgMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb);


#ifdef IN_RING3

# ifndef VBOX_DEVICE_STRUCT_TESTCASE
DECLCALLBACK(void) devpciR3InfoPci(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs);
DECLCALLBACK(void) devpciR3InfoPciIrq(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs);
DECLCALLBACK(int)  devpciR3CommonRegisterDevice(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t fFlags,
                                                uint8_t uPciDevNo, uint8_t uPciFunNo, const char *pszName);
DECLCALLBACK(int)  devpcibridgeR3CommonRegisterDevice(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t fFlags,
                                                      uint8_t uPciDevNo, uint8_t uPciFunNo, const char *pszName);
DECLCALLBACK(int)  devpciR3CommonIORegionRegister(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                                  RTGCPHYS cbRegion, PCIADDRESSSPACE enmType, uint32_t fFlags,
                                                  uint64_t hHandle, PFNPCIIOREGIONMAP pfnMapUnmap);
DECLCALLBACK(void) devpciR3CommonInterceptConfigAccesses(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                         PFNPCICONFIGREAD pfnRead, PFNPCICONFIGWRITE pfnWrite);
DECLCALLBACK(VBOXSTRICTRC) devpciR3CommonConfigRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                    uint32_t uAddress, unsigned cb, uint32_t *pu32Value);
DECLHIDDEN(VBOXSTRICTRC)   devpciR3CommonConfigReadWorker(PPDMPCIDEV pPciDev, uint32_t uAddress, unsigned cb, uint32_t *pu32Value);
DECLCALLBACK(VBOXSTRICTRC) devpciR3CommonConfigWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                     uint32_t uAddress, unsigned cb, uint32_t u32Value);
DECLHIDDEN(VBOXSTRICTRC)   devpciR3CommonConfigWriteWorker(PPDMDEVINS pDevIns, PDEVPCIBUSCC pBusCC,
                                                           PPDMPCIDEV pPciDev, uint32_t uAddress, unsigned cb, uint32_t u32Value);
void devpciR3CommonRestoreConfig(PPDMDEVINS pDevIns, PPDMPCIDEV pDev, uint8_t const *pbSrcConfig);
int  devpciR3CommonRestoreRegions(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM, PPDMPCIDEV pPciDev, PPCIIOREGION paIoRegions, bool fNewState);
void devpciR3ResetDevice(PPDMDEVINS pDevIns, PPDMPCIDEV pDev);
void devpciR3BiosInitSetRegionAddress(PPDMDEVINS pDevIns, PDEVPCIBUS pBus, PPDMPCIDEV pPciDev, int iRegion, uint64_t addr);
uint32_t devpciR3GetCfg(PPDMPCIDEV pPciDev, int32_t iRegister, int cb);
void devpciR3SetCfg(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int32_t iRegister, uint32_t u32, int cb);
DECLHIDDEN(void) devpciR3CommonResetBridge(PPDMDEVINS pDevIns);
DECL_HIDDEN_CALLBACK(int) devpciR3CommonSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM);
DECL_HIDDEN_CALLBACK(int) devpciR3CommonLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass);

DECL_HIDDEN_CALLBACK(int) devpciR3BridgeCommonSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM);
DECL_HIDDEN_CALLBACK(int) devpciR3BridgeCommonLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass);

DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) devpciR3BridgeCommonConfigWrite(PPDMDEVINSR3 pDevIns, uint8_t iBus, uint8_t iDevice,
                                                                   uint32_t u32Address, unsigned cb, uint32_t u32Value);
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) devpciR3BridgeCommonConfigRead(PPDMDEVINSR3 pDevIns, uint8_t iBus, uint8_t iDevice,
                                                                  uint32_t u32Address, unsigned cb, uint32_t *pu32Value);

DECLHIDDEN(uint8_t) devpciR3BridgeCommonGetExpressPortTypeFromString(const char *pszExpressPortType);


DECLINLINE(uint8_t) devpciR3GetByte(PPDMPCIDEV pPciDev, int32_t iRegister)
{
    return (uint8_t)devpciR3GetCfg(pPciDev, iRegister, 1);
}

DECLINLINE(uint16_t) devpciR3GetWord(PPDMPCIDEV pPciDev, int32_t iRegister)
{
    return (uint16_t)devpciR3GetCfg(pPciDev, iRegister, 2);
}

DECLINLINE(uint32_t) devpciR3GetDWord(PPDMPCIDEV pPciDev, int32_t iRegister)
{
    return (uint32_t)devpciR3GetCfg(pPciDev, iRegister, 4);
}

DECLINLINE(void) devpciR3SetByte(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int32_t iRegister, uint8_t u8)
{
    devpciR3SetCfg(pDevIns, pPciDev, iRegister, u8, 1);
}

DECLINLINE(void) devpciR3SetWord(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int32_t iRegister, uint16_t u16)
{
    devpciR3SetCfg(pDevIns, pPciDev, iRegister, u16, 2);
}

DECLINLINE(void) devpciR3SetDWord(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int32_t iRegister, uint32_t u32)
{
    devpciR3SetCfg(pDevIns, pPciDev, iRegister, u32, 4);
}


DECLINLINE(PPDMPCIDEV) devpciR3FindBridge(PDEVPCIBUS pBus, uint8_t uBus)
{
    /* Search for a fitting bridge. */
    for (uint32_t iBridge = 0; iBridge < pBus->cBridges; iBridge++)
    {
        /*
         * Examine secondary and subordinate bus number.
         * If the target bus is in the range we pass the request on to the bridge.
         */
        PPDMPCIDEV pBridge = pBus->papBridgesR3[iBridge];
        AssertMsg(pBridge && pciDevIsPci2PciBridge(pBridge),
                  ("Device is not a PCI bridge but on the list of PCI bridges\n"));
        /* safe, only needs to go to the config space array */
        uint32_t uSecondary   = PDMPciDevGetByte(pBridge, VBOX_PCI_SECONDARY_BUS);
        /* safe, only needs to go to the config space array */
        uint32_t uSubordinate = PDMPciDevGetByte(pBridge, VBOX_PCI_SUBORDINATE_BUS);
        Log3Func(("bus %p, bridge %d: %d in %d..%d\n", pBus, iBridge, uBus, uSecondary, uSubordinate));
        if (uBus >= uSecondary && uBus <= uSubordinate)
            return pBridge;
    }

    /* Nothing found. */
    return NULL;
}
# endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

#endif /* IN_RING3 */

#endif /* !VBOX_INCLUDED_SRC_Bus_DevPciInternal_h */

