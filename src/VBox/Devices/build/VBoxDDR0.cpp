/* $Id: VBoxDDR0.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxDDR0 - Built-in drivers & devices (part 1), ring-0 module.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV
#include <VBox/log.h>
#include <VBox/sup.h>
#include <VBox/vmm/pdmdev.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#if defined(RT_OS_SOLARIS) && defined(IN_RING0)
/* Dependency information for the native solaris loader. */
extern "C" { char _depends_on[] = "vboxdrv VMMR0.r0"; }
#endif

/**
 * Pointer to the ring-0 device registrations for VBoxDDR0.
 */
static PCPDMDEVREGR0 g_apVBoxDDR0DevRegs[] =
{
    &g_DevicePCI,
    &g_DevicePciIch9,
    &g_DeviceIOAPIC,
    &g_DevicePS2KeyboardMouse,
    &g_DevicePIIX3IDE,
    &g_DeviceI8254,
    &g_DeviceI8259,
    &g_DeviceHPET,
    &g_DeviceSmc,
    &g_DeviceFlash,
    &g_DeviceMC146818,
    &g_DeviceVga,
    &g_DeviceVMMDev,
    &g_DevicePCNet,
#ifdef VBOX_WITH_E1000
    &g_DeviceE1000,
#endif
#ifdef VBOX_WITH_VIRTIO
    &g_DeviceVirtioNet,
#endif
    &g_DeviceDP8390,
    &g_Device3C501,
    &g_DeviceICHAC97,
    &g_DeviceHDA,
#ifdef VBOX_WITH_VUSB
    &g_DeviceOHCI,
#endif
#ifdef VBOX_WITH_EHCI_IMPL
    &g_DeviceEHCI,
#endif
#ifdef VBOX_WITH_XHCI_IMPL
    &g_DeviceXHCI,
#endif
    &g_DeviceACPI,
    &g_DeviceDMA,
    &g_DeviceSerialPort,
    &g_DeviceOxPcie958,
    &g_DeviceParallelPort,
#ifdef VBOX_WITH_AHCI
    &g_DeviceAHCI,
#endif
#ifdef VBOX_WITH_BUSLOGIC
    &g_DeviceBusLogic,
#endif
    &g_DevicePCIBridge,
    &g_DevicePciIch9Bridge,
#ifdef VBOX_WITH_LSILOGIC
    &g_DeviceLsiLogicSCSI,
    &g_DeviceLsiLogicSAS,
#endif
#ifdef VBOX_WITH_NVME_IMPL
    &g_DeviceNVMe,
#endif
#ifdef VBOX_WITH_EFI
    &g_DeviceEFI,
#endif
#ifdef VBOX_WITH_VIRTIO_SCSI
    &g_DeviceVirtioSCSI,
#endif
#ifdef VBOX_WITH_PCI_PASSTHROUGH_IMPL
    &g_DevicePciRaw,
#endif
    &g_DeviceGIMDev,
#ifdef VBOX_WITH_NEW_LPC_DEVICE
    &g_DeviceLPC,
#endif
#ifdef VBOX_WITH_IOMMU_AMD
    &g_DeviceIommuAmd,
#endif
#ifdef VBOX_WITH_IOMMU_INTEL
    &g_DeviceIommuIntel,
#endif
#ifdef VBOX_WITH_TPM
    &g_DeviceTpm,
#endif
};

/**
 * Module device registration record for VBoxDDR0.
 */
static PDMDEVMODREGR0 g_VBoxDDR0ModDevReg =
{
    /* .u32Version = */ PDM_DEVMODREGR0_VERSION,
    /* .cDevRegs = */   RT_ELEMENTS(g_apVBoxDDR0DevRegs),
    /* .papDevRegs = */ &g_apVBoxDDR0DevRegs[0],
    /* .hMod = */       NULL,
    /* .ListEntry = */  { NULL, NULL },
};


DECLEXPORT(int)  ModuleInit(void *hMod)
{
    LogFlow(("VBoxDDR0/ModuleInit: %p\n", hMod));
    return PDMR0DeviceRegisterModule(hMod, &g_VBoxDDR0ModDevReg);
}


DECLEXPORT(void) ModuleTerm(void *hMod)
{
    LogFlow(("VBoxDDR0/ModuleTerm: %p\n", hMod));
    PDMR0DeviceDeregisterModule(hMod, &g_VBoxDDR0ModDevReg);
}

