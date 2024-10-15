/* $Id: pdmpcidevint.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevPCI - PDM PCI Internal header - Only for hiding bits of PDMPCIDEV.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef VBOX_INCLUDED_vmm_pdmpcidevint_h
#define VBOX_INCLUDED_vmm_pdmpcidevint_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vmm/pdmdev.h>

/** @defgroup grp_pdm_pcidev_int    The PDM PCI Device Internals
 * @ingroup grp_pdm_pcidev
 *
 * @remarks The PDM PCI device internals are visible to both PDM and the PCI Bus
 *          implementation, thus it lives among the the public headers despite
 *          being rather private and internal.
 *
 * @{
 */


/**
 * PCI I/O region.
 */
typedef struct PCIIOREGION
{
    /** Current PCI mapping address, INVALID_PCI_ADDRESS (0xffffffff) means not mapped. */
    uint64_t                        addr;
    /** The region size.  Power of 2. */
    uint64_t                        size;
    /** Handle or UINT64_MAX (see PDMPCIDEV_IORGN_F_HANDLE_MASK in fFlags). */
    uint64_t                        hHandle;
    /** PDMPCIDEV_IORGN_F_XXXX. */
    uint32_t                        fFlags;
    /** PCIADDRESSSPACE */
    uint8_t                         type;
    uint8_t                         abPadding0[3];
    /** Callback called when the region is mapped or unmapped (new style devs). */
    R3PTRTYPE(PFNPCIIOREGIONMAP)    pfnMap;
#if R3_ARCH_BITS == 32
    uint32_t                        u32Padding2;
#endif
} PCIIOREGION;
AssertCompileSize(PCIIOREGION, 5*8);
/** Pointer to a PCI I/O region. */
typedef PCIIOREGION *PPCIIOREGION;
/** Pointer to a const PCI I/O region. */
typedef PCIIOREGION const *PCPCIIOREGION;

/**
 * Callback function for reading from the PCI configuration space.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns         Pointer to the device instance of the PCI bus.
 * @param   iBus            The bus number this device is on.
 * @param   iDevice         The number of the device on the bus.
 * @param   u32Address      The configuration space register address. [0..255]
 * @param   cb              The register size. [1,2,4]
 * @param   pu32Value       Where to return the register value.
 */
typedef DECLCALLBACKTYPE(VBOXSTRICTRC, FNPCIBRIDGECONFIGREAD,(PPDMDEVINSR3 pDevIns, uint8_t iBus, uint8_t iDevice,
                                                              uint32_t u32Address, unsigned cb, uint32_t *pu32Value));
/** Pointer to a FNPCICONFIGREAD() function. */
typedef FNPCIBRIDGECONFIGREAD *PFNPCIBRIDGECONFIGREAD;
#if !RT_CLANG_PREREQ(11, 0) /* Clang 11 (at least) has trouble with nothrow and pointers to function pointers. */
/** Pointer to a PFNPCICONFIGREAD. */
typedef PFNPCIBRIDGECONFIGREAD *PPFNPCIBRIDGECONFIGREAD;
#endif

/**
 * Callback function for writing to the PCI configuration space.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns         Pointer to the device instance of the PCI bus.
 * @param   iBus            The bus number this device is on.
 * @param   iDevice         The number of the device on the bus.
 * @param   u32Address      The configuration space register address. [0..255]
 * @param   cb              The register size. [1,2,4]
 * @param   u32Value        The value that's being written. The number of bits actually used from
 *                          this value is determined by the cb parameter.
 */
typedef DECLCALLBACKTYPE(VBOXSTRICTRC, FNPCIBRIDGECONFIGWRITE,(PPDMDEVINSR3 pDevIns, uint8_t iBus, uint8_t iDevice,
                                                               uint32_t u32Address, unsigned cb, uint32_t u32Value));
/** Pointer to a FNPCICONFIGWRITE() function. */
typedef FNPCIBRIDGECONFIGWRITE *PFNPCIBRIDGECONFIGWRITE;
#if !RT_CLANG_PREREQ(11, 0) /* Clang 11 (at least) has trouble with nothrow and pointers to function pointers. */
/** Pointer to a PFNPCICONFIGWRITE. */
typedef PFNPCIBRIDGECONFIGWRITE *PPFNPCIBRIDGECONFIGWRITE;
#endif

/* Forward declaration */
struct DEVPCIBUS;

enum {
    /** Flag whether the device is a pci-to-pci bridge.
     * This is set prior to device registration.  */
    PCIDEV_FLAG_PCI_TO_PCI_BRIDGE  = RT_BIT_32(1),
    /** Flag whether the device is a PCI Express device.
     * This is set prior to device registration.  */
    PCIDEV_FLAG_PCI_EXPRESS_DEVICE = RT_BIT_32(2),
    /** Flag whether the device is capable of MSI.
     * This one is set by MsiInit().  */
    PCIDEV_FLAG_MSI_CAPABLE        = RT_BIT_32(3),
    /** Flag whether the device is capable of MSI-X.
     * This one is set by MsixInit().  */
    PCIDEV_FLAG_MSIX_CAPABLE       = RT_BIT_32(4),
    /** Flag if device represents real physical device in passthrough mode. */
    PCIDEV_FLAG_PASSTHROUGH        = RT_BIT_32(5),
    /** Flag whether the device is capable of MSI using 64-bit address.  */
    PCIDEV_FLAG_MSI64_CAPABLE      = RT_BIT_32(6)

};


/**
 * PDM PCI Device - Internal data.
 *
 * @sa PDMPCIDEV
 */
typedef struct PDMPCIDEVINT
{
    /** @name Owned by PDM.
     * @remarks The bus may use the device instance pointers.
     * @{
     */
    /** Pointer to the PDM device the PCI device belongs to. (R3 ptr)  */
    PPDMDEVINSR3                    pDevInsR3;
    /** The CFGM device configuration index (default, PciDev1..255).
     * This also works as the internal sub-device ordinal with MMIOEx.
     * @note Same value as idxSubDev, can therefore be removed later. */
    uint8_t                         idxDevCfg;
    /** Set if the it can be reassigned to a different PCI device number. */
    bool                            fReassignableDevNo;
    /** Set if the it can be reassigned to a different PCI function number. */
    bool                            fReassignableFunNo;
    /** Alignment padding - used by ICH9 for region swapping (DevVGA hack).   */
    uint8_t                         bPadding0;
    /** Index into the PDM internal bus array (PDM::aPciBuses). */
    uint8_t                         idxPdmBus;
    /** Set if this device has been registered. */
    bool                            fRegistered;
    /** Index into PDMDEVINSR3::apPciDevs (same as PDMPCIDEV::idxSubDev). */
    uint16_t                        idxSubDev;
    /** @} */

    /** @name Owned by the PCI Bus
     * @remarks PDM will not touch anything here (includes not relocating anything).
     * @{
     */
    /** Pointer to the PCI bus of the device. (R3 ptr) */
    R3PTRTYPE(struct DEVPCIBUS *)   pBusR3;
    /** Read config callback. */
    R3PTRTYPE(PFNPCICONFIGREAD)     pfnConfigRead;
    /** Write config callback. */
    R3PTRTYPE(PFNPCICONFIGWRITE)    pfnConfigWrite;
    /** Read config callback for PCI bridges to pass requests
     * to devices on another bus. */
    R3PTRTYPE(PFNPCIBRIDGECONFIGREAD) pfnBridgeConfigRead;
    /** Write config callback for PCI bridges to pass requests
     * to devices on another bus. */
    R3PTRTYPE(PFNPCIBRIDGECONFIGWRITE) pfnBridgeConfigWrite;

    /** Flags of this PCI device, see PCIDEV_FLAG_XXX constants. */
    uint32_t                        fFlags;
    /** Current state of the IRQ pin of the device. */
    int32_t                         uIrqPinState;

    /** Offset of MSI PCI capability in config space, or 0.
     * @todo fix non-standard naming.  */
    uint8_t                         u8MsiCapOffset;
    /** Size of MSI PCI capability in config space, or 0.
     * @todo fix non-standard naming.  */
    uint8_t                         u8MsiCapSize;
    /** Offset of MSI-X PCI capability in config space, or 0.
     * @todo fix non-standard naming.  */
    uint8_t                         u8MsixCapOffset;
    /** Size of MSI-X PCI capability in config space, or 0.
     * @todo fix non-standard naming.  */
    uint8_t                         u8MsixCapSize;
    /** Size of the MSI-X region. */
    uint16_t                        cbMsixRegion;
    /** Offset to the PBA for MSI-X.   */
    uint16_t                        offMsixPba;
    /** Add padding to align aIORegions to an 16 byte boundary. */
    uint8_t                         abPadding2[HC_ARCH_BITS == 32 ? 12 : 8];
    /** The MMIO handle for the MSI-X MMIO bar. */
    IOMMMIOHANDLE                   hMmioMsix;

    /** Pointer to bus specific data. (R3 ptr) */
    R3PTRTYPE(const void *)         pvPciBusPtrR3;
    /** I/O regions. */
    PCIIOREGION                     aIORegions[VBOX_PCI_NUM_REGIONS];
    /** @}  */
} PDMPCIDEVINT;
AssertCompileMemberAlignment(PDMPCIDEVINT, aIORegions, 8);
AssertCompileSize(PDMPCIDEVINT, HC_ARCH_BITS == 32 ? 0x98 : 0x178);

/** Indicate that PDMPCIDEV::Int.s can be declared. */
#define PDMPCIDEVINT_DECLARED

/** @} */

#endif /* !VBOX_INCLUDED_vmm_pdmpcidevint_h */

