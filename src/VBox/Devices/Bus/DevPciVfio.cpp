/* $Id: DevPciVfio.cpp 114193 2026-05-28 11:33:35Z alexander.eichner@oracle.com $ */
/** @file
 * PCI passthrough device emulation using VFIO/IOMMUFD.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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


/** @page   pg_dev_vfio     DevPciVfio - VFIO based PCI passthrough
 *
 * This device emulation utilizes the VFIO framework on Linux and allows passing
 * through real PCI Express devices to the guest, including support for Graphics
 * Processing Units (GPUs).
 *
 * Not that this in early stages and has a few shortcomings currently.
 *
 * @section sec_dev_vfio_shortcomings
 *
 * The current emulation only supports MSI based interrupts. The legacy INTx style
 * doesn't work currently because VFIO auto masks those after they fired to prevent
 * an interrupt storm on the host and requires explicit unmasking after the guest
 * handled it but we currently lack a callback mechanism from the I/O APIC to get notified
 * about EOI events from the guest for the device.
 * MSI-X interrupt support is not implemented right now, so the passed through device needs
 * to support MSI in order to be supported.
 *
 * Saved states are not supported, and will never be as we can't capture the PCIe device state
 * and replay it when resuming.
 *
 * @section sec_dev_vfio_requirements
 *
 * The following requirements have to be met in order to support PCIe passthrough:
 *     - Recent enough hardware with IOMMU support
 *     - Linux host with at least kernel 6.12
 *     - The VM needs to have ICH9 configured
 *     - The VM needs to have UEFI configured
 *
 * @section sec_dev_vfio_configuration
 *
 * In order to be able to pass through a device it needs to be bound to the vfio-pci kernel module.
 * Depending on the complexity of the device this can be done after the original driver was loaded
 * or needs to be done before the driver initialized the device because unloading the driver will leave
 * it in a broken state only a host reset can cure. An example is the AMD Radeon 5700 XT which will serve
 * as the example throughout this section.
 *
 * First identify the device's bus, device and function number for every device you want to pass through using lspci:
 *     lspci
 *     [...]
 *     0a:00.0 VGA compatible controller: Advanced Micro Devices, Inc. [AMD/ATI] Navi 10 [Radeon RX 5600 OEM/5600 XT / 5700/5700 XT] (rev c1)
 *     0a:00.1 Audio device: Advanced Micro Devices, Inc. [AMD/ATI] Navi 10 HDMI Audio
 *     [...]
 *
 * Also retrieve the PCI IDs for the devices:
 *     lspci -n
 *     [...]
 *     0a:00.0 0300: 1002:731f (rev c1)
 *     0a:00.1 0403: 1002:ab38
 *     [...]
 *
 * As this is a multi function device all functions need to be passed through. Also ensure that the device being passed through
 * is in a single IOMMU group.
 *
 * In order to override the default driver directly on boot get at the modalias for all devices using
 *     cat /sys/bus/pci/devices/0000\\:0a\\:00.0/modalias
 *     pci:v00001002d0000731Fsv00001682sd00005701bc03sc00i00
 *     cat /sys/bus/pci/devices/0000\\:0a\\:00.1/modalias
 *     pci:v00001002d0000AB38sv00001002sd0000AB38bc04sc03i00
 *
 * Then edit /etc/modprobe.d/local.conf and add the following lines to override the kernel module
 * for the devices (insert your modalias output for the devices)
 *     alias pci:v00001002d0000AB38sv00001002sd0000AB38bc04sc03i00 vfio-pci
 *     alias pci:v00001002d0000731Fsv00001682sd00005701bc03sc00i00 vfio-pci
 *
 * Also add the PCI IDs from the devices as an option when loading vfio-pci
 *     options vfio-pci ids=1002:731f,1002:ab38
 *
 * On the next host reboot the devices should be bound to vfio-pci and /dev/vfio/devices should
 * have vfio0 and vfio1.
 *
 * In case the device to be passed through to the guest doesn't suffer from broken state after a driver unload the following
 * commands will unbind the driver from the device and bind it to vfio-pci
 *    echo 0000:09:00.0 > /sys/bus/pci/devices/0000\\:09\\:00.0/driver/unbind (the bus, device and function number is dependent on the device)
 *    modprobe vfio-pci
 *    echo 10ec 8126 > /sys/bus/pci/drivers/vfio-pci/new_id (the two hex numbers are the PCI vendor and device ID of the device being passed through)
 *
 * Make /dev/vfio/devices/vfio0, /dev/vfio/devices/vfio1 and /dev/iommu read/write accessible to the user running the VM.
 *
 * Create a VM and configure it to make use of the ICH9 chipset and enable EFI. Then add the following extradata to the VM
 *    VBoxManage setextradata vmname "VBoxInternal/Devices/ich9pcibridge/0/Config/ExpressEnabled" 1
 *    VBoxManage setextradata vmname "VBoxInternal/Devices/ich9pcibridge/0/Config/ExpressPortType" "RootCmplxRootPort"
 *    VBoxManage setextradata vmname "VBoxInternal/Devices/pci-vfio/0/Config/Fun0/ExposeVga" 1
 *    VBoxManage setextradata vmname "VBoxInternal/Devices/pci-vfio/0/Config/Fun0/VfioPath" /dev/vfio/devices/vfio0
 *    VBoxManage setextradata vmname "VBoxInternal/Devices/pci-vfio/0/Config/Fun1/VfioPath" /dev/vfio/devices/vfio1
 *    VBoxManage setextradata vmname "VBoxInternal/Devices/pci-vfio/0/Config/IommuPath" /dev/iommu
 *    VBoxManage setextradata vmname "VBoxInternal/Devices/pci-vfio/0/PCIBusNo" 1
 *    VBoxManage setextradata vmname "VBoxInternal/Devices/pci-vfio/0/PCIDeviceNo" 0
 *    VBoxManage setextradata vmname "VBoxInternal/Devices/pci-vfio/0/PCIFunctionNo" 0
 *    VBoxManage setextradata vmname "VBoxInternal/Devices/pci-vfio/0/Trusted" 1
 *
 * If you intend to pass through multiple devices you have to change the pci-vfio instance number to 1, 2, etc.
 * and adjust the PCI device number accordingly
 *    VBoxManage setextradata vmname "VBoxInternal/Devices/pci-vfio/instance/..."
 *    VBoxManage setextradata vmname "VBoxInternal/Devices/pci-vfio/instance/PCIDeviceNo" instance
 *
 * The ExposeVga setting is only required for graphics card devices which expose the legacy VGA I/O ranges to the guest
 * so it can output early during boot. However you need to disable the VirtualBox emulated graphics controller or starting the VM
 * fails because both devices want to register those ranges. Disable the emulated graphics controller with
 *    VBoxManage modifyvm vmname --graphicscontroller none
 *
 * After that you can start the VM with either the usual GUI or VBoxHeadless.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_PCI_RAW
#define PDMPCIDEV_INCLUDE_PRIVATE  /* Hack to get pdmpcidevint.h included at the right point. */
#include <VBox/pci.h>
#include <VBox/log.h>
#include <VBox/msi.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/pdmpci.h>
#include <VBox/vmm/pdmpcidev.h>
#include <iprt/assert.h>
#include <iprt/dir.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/errcore.h>

#include <iprt/linux/sysfs.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

# include "DevPciInternal.h"

#if 0 /* Allow building on older hosts. */
# include <linux/vfio.h>
# include <linux/iommufd.h>
#else
# include "DevPciVfio.h"
#endif


#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** eventfd2() syscall for the interrupt handling. */
#define LNX_SYSCALL_EVENTFD2          290

/** Invalid access. */
#define VFIO_PCI_CFG_SPACE_ACCESS_INVALID     0
/** Passthrough the access to VFIO. */
#define VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH 1
/** Just do the default action for the access. */
#define VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT  2
/** Emulate the access using a special handler. */
#define VFIO_PCI_CFG_SPACE_ACCESS_EMULATE     3


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * MSI-X table entry.
 */
typedef struct MSIXTBLENTRY
{
    volatile uint32_t u32MsgAddr;
    volatile uint32_t u32MsgAddrUpper;
    volatile uint32_t u32MsgData;
    volatile uint32_t u32VecCtrl;
} MSIXTBLENTRY;
typedef MSIXTBLENTRY *PMSIXTBLENTRY;


/**
 * A single PCI BAR.
 */
typedef struct VFIOPCIBAR
{
    /** The VFIO PCI function this BAR belongs to. */
    uint8_t             iPciFun;
    /** Region type, 0 - disabled, 1 - PIO, 2 - MMIO. */
    uint8_t             bType;
    /** Flag whether MMIO region intercepts are enabled. */
    bool                fInterceptMmio;
    /** The address space flags for an MMIO region. */
    PCIADDRESSSPACE     enmAddrSpace;
    /** Size of the region. */
    uint64_t            cbRegion;
    /** Type dependent data. */
    union
    {
        /** Start offset of the PIO region. */
        uint64_t        offPio;
        /** Start of the MMIO mapping. */
        volatile void   *pvMmio;
    } u;
    /** Type dependent handle. */
    union
    {
        /** I/O port region. */
        IOMIOPORTHANDLE hIoPort;
        /** MMIO region. */
        IOMMMIOHANDLE   hMmio;
        /** MMIO2 region. */
        PGMMMIO2HANDLE  hMmio2;
    } hnd;
} VFIOPCIBAR;
typedef VFIOPCIBAR *PVFIOPCIBAR;
typedef const VFIOPCIBAR *PCVFIOPCIBAR;


/**
 * VFIO PCI function.
 */
typedef struct VFIOPCIFUN
{
    /** The function name. */
    char                 szName[16];
    /** The function index. */
    uint32_t             uPciFun;
    /** The vfio cdev file descriptor .*/
    int                  iFdVfio;

    /** The start offset of the PCI config space. */
    uint64_t             offPciCfg;
    /** Size of the PCI config space. */
    size_t               cbPciCfg;
    /** The access table indicating how to treat
     * config space accesses for each byte.. Each config space byte
     * requires 4bits (2bit each for read write) and the config space can be 4096 bytes large. */
    uint8_t              abPciCfgIntercept[(4096 * 4) / 8];
    /** The PCI BAR information. */
    VFIOPCIBAR           aBars[VBOX_PCI_NUM_REGIONS];
    /** The BAR containing MSI-X state if available. */
    uint8_t              iBarMsix;

    /** Flag whether MMIO regions are intercepted and handled through
     * regular MMIO handlers or are mapped into the guest. */
    bool                 fInterceptMmio;
    /** Flag whether VGA capabilities are exposed. */
    bool                 fVga;
    /** The start offset of the VGA region. */
    uint64_t             offVga;
    /** The legacy I/O port range from 0x3b0 - 0x3bb. */
    IOMIOPORTHANDLE      hVgaIoPort1;
    /** The legacy I/O port range from 0x3c0 - 0x3df. */
    IOMIOPORTHANDLE      hVgaIoPort2;
    /** The legacy MMIO range from 0xa0000 - 0xbffff*/
    IOMMMIOHANDLE        hVgaMmio;

    /** ROM region start offset. */
    uint64_t             offRom;
    /** Size of the ROM region. */
    size_t               cbRom;
    /** Pointer to the ROM region memory. */
    void                 *pvRom;
    /** ROM region handle. */
    PGMMMIO2HANDLE       hRom;
    /** The write protection layer for the ROM MMIO2 region. */
    PGMPHYSHANDLERTYPE   hRomAccHndType;
    /** The physical guest address the ROM is currently mapped at. */
    RTGCPHYS             GCPhysRom;

    /** The number of interrupt vectors for each interrupt type. */
    AssertCompile(   VFIO_PCI_INTX_IRQ_INDEX == 0
                  && VFIO_PCI_MSI_IRQ_INDEX  == 1
                  && VFIO_PCI_MSIX_IRQ_INDEX == 2);
    uint32_t             acIrqVectors[VFIO_PCI_MSIX_IRQ_INDEX + 1];

    /** The eventfd to wakeup the IRQ poller. */
    int                  iFdWakeup;

    /** The eventfds being pre-created. */
    int                  afdEvts[VBOX_MSI_MAX_ENTRIES];
    /** The current IRQ mode. */
    uint8_t              uIrqModeCur;
    /** The new confogured IRQ mode. */
    volatile uint8_t     uIrqModeNew;

    /** The interrupt polling thread. */
    PPDMTHREAD           pThrdIrq;

    /** The MSI capability offset if enabled. */
    uint8_t              offMsiCtrl;

    /** The MSI-X capability offset if enabled. */
    uint8_t              offMsixCtrl;
    /** Size of the MSI-X region. */
    size_t               cbMsix;
    /** Pointer to the MSI-X region memory. */
    uint8_t              *pbMsix;
    /** Pointer to the MSI-X table entries. */
    PMSIXTBLENTRY        paMsixTbl;
    /** Pointer to the PBA array. */
    volatile uint64_t    *pbmMsixPba;
    /** MSI-X region handle. */
    PGMMMIO2HANDLE       hMsix;
    /** MSI-X intercept handle for the non aligned case. */
    IOMMMIOHANDLE        hMsixMmio;
#if 0
    /** The write protection layer for the MSI-X MMIO2 region. */
    PGMPHYSHANDLERTYPE   hMsixWrHndType;
    /** The physical guest address the MSI-X region is currently mapped at. */
    RTGCPHYS             GCPhysMsix;
#endif

} VFIOPCIFUN;
/** Pointer to the a VFIO PCI function. */
typedef VFIOPCIFUN *PVFIOPCIFUN;
/** Pointer to the a constant VFIO PCI function. */
typedef const VFIOPCIFUN *PCVFIOPCIFUN;


/**
 * Passed through VFIO PCI device instance.
 */
typedef struct VFIOPCI
{
    /** Pointer to the device instance. */
    PPDMDEVINSR3         pDevIns;
    /** The device instance. */
    int                  iInstance;

    /** Flag whether the legacy VFIO container API is used. */
    bool                 fVfioLegacy;

    /** Type dependent data. */
    union
    {
        /** IOMMUFD based data. */
        struct
        {
            /** The IOMMU file descriptor. */
            int          iFdIommu;
            /** The IOMMU page table object id. */
            uint32_t     idIommuHwpt;
        } IommuFd;
        /** VFIO container based data. */
        struct
        {
            /** The VFIO container file descriptor. */
            int          iFdVfioContainer;
            /** The VFIO group filedescriptor. */
            int          iFdVfioGroup;
        } VfioGroup;
    };

    /** The VFIO PCI functions. */
    VFIOPCIFUN           aPciFuns[8];

    /** Flag whether the guest RAM was mapped to the IOMMU. */
    bool                 fGuestRamMapped;
} VFIOPCI;
/** Pointer to the raw PCI instance data. */
typedef VFIOPCI *PVFIOPCI;


#ifndef VBOX_DEVICE_STRUCT_TESTCASE

/**
 * eventfd2() syscall wrapper.
 *
 * @returns IPRT status code.
 * @param   uValInit            The initial value of the maintained counter.
 * @param   fFlags              Flags controlling the eventfd behavior.
 * @param   piFdEvt             Where to store the file descriptor of the eventfd object on success.
 */
DECLINLINE(int) pciVfioLnxEventfd2(uint32_t uValInit, uint32_t fFlags, int *piFdEvt)
{
    int rcLnx = syscall(LNX_SYSCALL_EVENTFD2, uValInit, fFlags);
    if (RT_UNLIKELY(rcLnx == -1))
        return RTErrConvertFromErrno(errno);

    *piFdEvt = rcLnx;
    return VINF_SUCCESS;
}


DECLINLINE(int) pciVfioCfgSpaceReadU8(PCVFIOPCIFUN pFun, uint32_t offReg, uint8_t *pb)
{
    ssize_t cb = pread(pFun->iFdVfio, pb, 1, pFun->offPciCfg + offReg);
    if (cb != 1)
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}


DECLINLINE(int) pciVfioCfgSpaceReadU16(PCVFIOPCIFUN pFun, uint32_t offReg, uint16_t *pu16)
{
    ssize_t cb = pread(pFun->iFdVfio, pu16, 2, pFun->offPciCfg + offReg);
    if (cb != 2)
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}


DECLINLINE(int) pciVfioCfgSpaceReadU32(PCVFIOPCIFUN pFun, uint32_t offReg, uint32_t *pu32)
{
    ssize_t cb = pread(pFun->iFdVfio, pu32, 4, pFun->offPciCfg + offReg);
    if (cb != 4)
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}


#if 0 /* unused */
DECLINLINE(int) pciVfioCfgSpaceReadU64(PCVFIOPCIFUN pFun, uint32_t offReg, uint64_t *pu64)
{
    ssize_t cb = pread(pFun->iFdVfio, pu64, 8, pFun->offPciCfg + offReg);
    if (cb != 8)
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}
#endif


DECLINLINE(int) pciVfioCfgSpaceWriteU8(PCVFIOPCIFUN pFun, uint32_t offReg, uint8_t u8)
{
    ssize_t cb = pwrite(pFun->iFdVfio, &u8, 1, pFun->offPciCfg + offReg);
    if (cb != 1)
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}


DECLINLINE(int) pciVfioCfgSpaceWriteU16(PCVFIOPCIFUN pFun, uint32_t offReg, uint16_t u16)
{
    ssize_t cb = pwrite(pFun->iFdVfio, &u16, 2, pFun->offPciCfg + offReg);
    if (cb != 2)
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}

DECLINLINE(int) pciVfioCfgSpaceWriteU32(PCVFIOPCIFUN pFun, uint32_t offReg, uint32_t u32)
{
    ssize_t cb = pwrite(pFun->iFdVfio, &u32, 4, pFun->offPciCfg + offReg);
    if (cb != 4)
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}


#if 0 /* unused */
DECLINLINE(int) pciVfioCfgSpaceWriteU64(PCVFIOPCIFUN pFun, uint32_t offReg, uint64_t u64)
{
    ssize_t cb = pwrite(pFun->iFdVfio, &u64, 8, pFun->offPciCfg + offReg);
    if (cb != 8)
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}
#endif


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC) pciVfioPioWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PVFIOPCI pThis = PDMDEVINS_2_DATA(pDevIns, PVFIOPCI);
    PCVFIOPCIBAR pBar = (PCVFIOPCIBAR)pvUser;
    PCVFIOPCIFUN pFun = &pThis->aPciFuns[pBar->iPciFun];
    ssize_t cbWritten = pwrite(pFun->iFdVfio, &u32, cb, pBar->u.offPio + offPort);
    if (cbWritten != cb)
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN}
 */
static DECLCALLBACK(VBOXSTRICTRC) pciVfioPioRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PVFIOPCI pThis = PDMDEVINS_2_DATA(pDevIns, PVFIOPCI);
    PCVFIOPCIBAR pBar = (PCVFIOPCIBAR)pvUser;
    PCVFIOPCIFUN pFun = &pThis->aPciFuns[pBar->iPciFun];
    ssize_t cbRead = pread(pFun->iFdVfio, pu32, cb, pBar->u.offPio + offPort);
    if (cbRead != cb)
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) pciVfioMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    RT_NOREF(pDevIns);
    PCVFIOPCIBAR pBar = (PCVFIOPCIBAR)pvUser;

    Assert(pBar->bType == 2);
    volatile uint8_t *pb = (volatile uint8_t *)pBar->u.pvMmio + off;
    switch (cb)
    {
        case 1: *(uint8_t *)pv = *pb; break;
        case 2: *(uint16_t *)pv = *(volatile uint16_t *)pb; break;
        case 4: *(uint32_t *)pv = *(volatile uint32_t *)pb; break;
        case 8: *(uint64_t *)pv = *(volatile uint64_t *)pb; break;
        default:
            memcpy(pv, (const void *)pb, cb); /** @todo Not correct as memcpy and volatile doesn't mix well */
            break;
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) pciVfioMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    RT_NOREF(pDevIns);
    PCVFIOPCIBAR pBar = (PCVFIOPCIBAR)pvUser;

    Assert(pBar->bType == 2);
    volatile uint8_t *pb = (volatile uint8_t *)pBar->u.pvMmio + off;
    switch (cb)
    {
        case 1: *(volatile uint8_t *)pb  = *(uint8_t const *)pv;  break;
        case 2: *(volatile uint16_t *)pb = *(uint16_t const *)pv; break;
        case 4: *(volatile uint32_t *)pb = *(uint32_t const *)pv; break;
        case 8: *(volatile uint64_t *)pb = *(uint64_t const *)pv; break;
        default:
            memcpy((void *)pb, pv, cb); /** @todo Not correct as memcpy and volatile doesn't mix well */
            break;
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) pciVfioMsixMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    RT_NOREF(pDevIns);
    PCVFIOPCIFUN pFun = (PCVFIOPCIFUN)pvUser;

    volatile uint8_t *pb = (volatile uint8_t *)pFun->pbMsix + off;
    switch (cb)
    {
        case 1: *(uint8_t *)pv = *pb; break;
        case 2: *(uint16_t *)pv = *(volatile uint16_t *)pb; break;
        case 4: *(uint32_t *)pv = *(volatile uint32_t *)pb; break;
        case 8: *(uint64_t *)pv = *(volatile uint64_t *)pb; break;
        default:
            memcpy(pv, (const void *)pb, cb); /** @todo Not correct as memcpy and volatile doesn't mix well */
            break;
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) pciVfioMsixMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    RT_NOREF(pDevIns);
    PCVFIOPCIFUN pFun = (PCVFIOPCIFUN)pvUser;

    volatile uint8_t *pb = (volatile uint8_t *)pFun->pbMsix + off;
    switch (cb)
    {
        case 1: *(volatile uint8_t *)pb  = *(uint8_t const *)pv;  break;
        case 2: *(volatile uint16_t *)pb = *(uint16_t const *)pv; break;
        case 4: *(volatile uint32_t *)pb = *(uint32_t const *)pv; break;
        case 8: *(volatile uint64_t *)pb = *(uint64_t const *)pv; break;
        default:
            memcpy((void *)pb, pv, cb); /** @todo Not correct as memcpy and volatile doesn't mix well */
            break;
    }

    return VINF_SUCCESS;
}


DECLINLINE(int) pciVfioQueryRegionInfo(PVFIOPCI pThis, PVFIOPCIFUN pFun, uint32_t uRegion, struct vfio_region_info *pRegionInfo)
{
    RT_ZERO(*pRegionInfo);
    pRegionInfo->argsz = sizeof(*pRegionInfo);
    pRegionInfo->index = uRegion;

    int rcLnx = ioctl(pFun->iFdVfio, VFIO_DEVICE_GET_REGION_INFO, pRegionInfo);
    if (rcLnx == -1)
        return PDMDevHlpVMSetError(pThis->pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                N_("Getting information for region %u of opened VFIO device failed with %d"), uRegion, errno);

    const int iInstance = pThis->iInstance;
    LogRel(("VFIO#%d.%u: Region %u:\n"
            "VFIO#%d.%u:     flags:       %#RX32\n"
            "VFIO#%d.%u:     index:       %#RU32\n"
            "VFIO#%d.%u:     cap_offset:  %#RX32\n"
            "VFIO#%d.%u:     size:        %#RX64\n"
            "VFIO#%d.%u:     offset:      %#RX64\n",
            iInstance, pFun->uPciFun, uRegion,
            iInstance, pFun->uPciFun, pRegionInfo->flags,
            iInstance, pFun->uPciFun, pRegionInfo->index,
            iInstance, pFun->uPciFun, pRegionInfo->cap_offset,
            iInstance, pFun->uPciFun, pRegionInfo->size,
            iInstance, pFun->uPciFun, pRegionInfo->offset));

    return VINF_SUCCESS;
}


static int pciVfioBarContainsMsix(PVFIOPCI pThis, PVFIOPCIFUN pFun, uint32_t uRegion, uint32_t cbInfo, bool *pfMsix)
{
    struct vfio_region_info *pRegionInfo = (struct vfio_region_info *)RTMemTmpAllocZ(cbInfo);
    if (!pRegionInfo)
        return VERR_NO_TMP_MEMORY;

    pRegionInfo->argsz = cbInfo;
    pRegionInfo->index = uRegion;

    int rcLnx = ioctl(pFun->iFdVfio, VFIO_DEVICE_GET_REGION_INFO, pRegionInfo);
    if (rcLnx == -1)
        return PDMDevHlpVMSetError(pThis->pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                N_("Getting information for region %u of opened VFIO device failed with %d"), uRegion, errno);

    AssertReturnStmt(pRegionInfo->cap_offset != 0, RTMemTmpFree(pRegionInfo), VERR_INVALID_STATE);
    uint32_t offCap = pRegionInfo->cap_offset;
    *pfMsix = false;
    while (offCap)
    {
        struct vfio_info_cap_header *pHdr = (struct vfio_info_cap_header *)((uint8_t *)pRegionInfo + offCap);
        if (pHdr->id == VFIO_REGION_INFO_CAP_MSIX_MAPPABLE)
        {
            *pfMsix = true;
            break;
        }

        offCap = pHdr->next;
    }

    RTMemTmpFree(pRegionInfo);
    return VINF_SUCCESS;
}


static int pciVfioSetupBar(PVFIOPCI pThis, PVFIOPCIFUN pFun, PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t uRegion, uint32_t uVfioRegion)
{
    struct vfio_region_info RegionInfo; RT_ZERO(RegionInfo);
    int rc = pciVfioQueryRegionInfo(pThis, pFun, uVfioRegion, &RegionInfo);
    if (RT_FAILURE(rc))
        return rc;

    pFun->aBars[uRegion].iPciFun = pFun->uPciFun;

    if (   RegionInfo.flags
        && RegionInfo.size)
    {
        uint32_t u32PciBar;
        rc = pciVfioCfgSpaceReadU32(pFun, VBOX_PCI_BASE_ADDRESS_0 + (uRegion * sizeof(uint32_t)), &u32PciBar);
        if (RT_FAILURE(rc))
            return rc;

        pFun->aBars[uRegion].cbRegion  = RegionInfo.size;
        if (u32PciBar & RT_BIT_32(0))
        {
            /* PIO. */
            pFun->aBars[uRegion].bType    = 1;
            pFun->aBars[uRegion].u.offPio = RegionInfo.offset;

            rc = PDMDevHlpPCIIORegionCreateIoEx(pDevIns, pPciDev, uRegion, RegionInfo.size,
                                                pciVfioPioWrite, pciVfioPioRead, &pFun->aBars[uRegion],
                                                "PIO", NULL /*paExtDescs*/, &pFun->aBars[uRegion].hnd.hIoPort);
            AssertRCReturn(rc, PDMDEV_SET_ERROR(pDevIns, rc, N_("Cannot register PCI I/O region")));
        }
        else
        {
            /* Check whether this might be a region containing MSI-X state. */
            if (RegionInfo.flags & VFIO_REGION_INFO_FLAG_CAPS)
            {
                bool fMsix = false;

                rc = pciVfioBarContainsMsix(pThis, pFun, uVfioRegion, RegionInfo.argsz, &fMsix);
                if (RT_FAILURE(rc))
                    return rc;

                if (fMsix)
                    pFun->iBarMsix = uRegion;
            }

            Assert(RegionInfo.flags & VFIO_REGION_INFO_FLAG_MMAP);
            int fProt =   ((RegionInfo.flags & VFIO_REGION_INFO_FLAG_READ)  ? PROT_READ  : 0)
                        | ((RegionInfo.flags & VFIO_REGION_INFO_FLAG_WRITE) ? PROT_WRITE : 0);
            pFun->aBars[uRegion].bType    = 2;
            pFun->aBars[uRegion].u.pvMmio = mmap(NULL, RegionInfo.size, fProt, MAP_FILE | MAP_SHARED, pFun->iFdVfio, RegionInfo.offset);
            if (pFun->aBars[uRegion].u.pvMmio == MAP_FAILED)
                return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                           N_("Mapping BAR%u at offset %#RX64 with size %RX64 failed with %d"),
                                           uRegion, RegionInfo.offset, RegionInfo.size, errno);

            uint32_t enmAddrSpace = PCI_ADDRESS_SPACE_MEM;
            if ((u32PciBar & (RT_BIT_32(2) | RT_BIT_32(1))) == PCI_ADDRESS_SPACE_BAR64)
                enmAddrSpace |= PCI_ADDRESS_SPACE_BAR64;
            if (u32PciBar & PCI_ADDRESS_SPACE_MEM_PREFETCH)
                enmAddrSpace |= PCI_ADDRESS_SPACE_MEM_PREFETCH;

            pFun->aBars[uRegion].fInterceptMmio = pFun->fInterceptMmio;
            pFun->aBars[uRegion].enmAddrSpace   = (PCIADDRESSSPACE)enmAddrSpace;
            /* MMIO mapping happens at a later stage. */
        }
    }
    else /* Not available. */
        Assert(RegionInfo.size == 0);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC) pciVfioVgaPioWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(pDevIns);
    PCVFIOPCIFUN pFun = (PCVFIOPCIFUN)pvUser;

    ssize_t cbRead = pwrite(pFun->iFdVfio, &u32, cb, pFun->offVga + offPort);
    if (cbRead != cb)
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN}
 */
static DECLCALLBACK(VBOXSTRICTRC) pciVfioVgaPioRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(pDevIns);
    PCVFIOPCIFUN pFun = (PCVFIOPCIFUN)pvUser;

    ssize_t cbRead = pread(pFun->iFdVfio, pu32, cb, pFun->offVga + offPort);
    if (cbRead != cb)
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) pciVfioVgaMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    RT_NOREF(pDevIns);
    PCVFIOPCIFUN pFun = (PCVFIOPCIFUN)pvUser;

    ssize_t cbRead = pread(pFun->iFdVfio, pv, cb, pFun->offVga + off);
    if (cbRead != cb)
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) pciVfioVgaMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    RT_NOREF(pDevIns);
    PCVFIOPCIFUN pFun = (PCVFIOPCIFUN)pvUser;

    ssize_t cbWritten = pwrite(pFun->iFdVfio, pv, cb, pFun->offVga + off);
    if (cbWritten != cb)
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMMMIONEWFILL}
 */
static DECLCALLBACK(VBOXSTRICTRC) pciVfioVgaMmioFill(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, uint32_t u32Item, unsigned cbItem, unsigned cItems)
{
    RT_NOREF(pDevIns);
    PCVFIOPCIFUN pFun = (PCVFIOPCIFUN)pvUser;

    uint8_t abVal[4] = { 0 };
    for (uint8_t i = 0; i < RT_ELEMENTS(abVal); i++)
    {
        abVal[i] = u32Item & 0xff;
        u32Item >>= 8;
    }

    ssize_t const cb = (ssize_t)cbItem * cItems;
    uint8_t *pb = (uint8_t *)RTMemTmpAlloc(cb);
    if (!pb)
        return VERR_NO_MEMORY;

    uint8_t *pbCur = pb;

    switch (cbItem)
    {
        case 1:
            for (uint32_t i = 0; i < cItems; i++)
                *pbCur++ = abVal[0];
            break;
        case 2:
            for (uint32_t i = 0; i < cItems; i++)
            {
                pbCur[0] = abVal[0];
                pbCur[1] = abVal[1];
                pbCur += 2;
            }
            break;
        case 4:
            for (uint32_t i = 0; i < cItems; i++)
            {
                pbCur[0] = abVal[0];
                pbCur[1] = abVal[1];
                pbCur[2] = abVal[2];
                pbCur[3] = abVal[3];
                pbCur += 4;
            }
            break;
        default:
            AssertFailedReturn(VERR_NOT_SUPPORTED);
    }

    ssize_t cbWritten = pwrite(pFun->iFdVfio, pb, cb, pFun->offVga + off);
    RTMemTmpFree(pb);

    if (cbWritten != cb)
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}


static int pciVfioSetupVga(PVFIOPCI pThis, PVFIOPCIFUN pFun, PPDMDEVINS pDevIns)
{
    struct vfio_region_info RegionInfo; RT_ZERO(RegionInfo);
    int rc = pciVfioQueryRegionInfo(pThis, pFun, VFIO_PCI_VGA_REGION_INDEX, &RegionInfo);
    if (RT_FAILURE(rc))
        return rc;

    AssertLogRelMsgReturn(   RegionInfo.flags
                          && RegionInfo.size,
                          ("VGA/GPU does not support VFIO_PCI_VGA_REGION_INDEX\n"),
                          VERR_NOT_SUPPORTED);

    pFun->offVga = RegionInfo.offset;

    /* Register the legacy VGA I/O port and MMIO ranges. */
    rc = PDMDevHlpIoPortCreateExAndMap(pDevIns, 0x3b0, 0x3bb - 0x3b0 + 1, IOM_IOPORT_F_ABS,
                                       pciVfioVgaPioWrite, pciVfioVgaPioRead, NULL, NULL, pFun, /** @todo String I/O */
                                       "VFIO VGA #1", NULL /*paExtDescs*/, &pFun->hVgaIoPort1);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, "Mapping legacy VGA ports 0x3b0 - 0x3bb failed");

    rc = PDMDevHlpIoPortCreateExAndMap(pDevIns, 0x3c0, 0x3df - 0x3c0 + 1, IOM_IOPORT_F_ABS,
                                       pciVfioVgaPioWrite, pciVfioVgaPioRead, NULL, NULL, pFun, /** @todo String I/O */
                                       "VFIO VGA #2", NULL /*paExtDescs*/, &pFun->hVgaIoPort2);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, "Mapping legacy VGA ports 0x3b0 - 0x3bb failed");

    /*
     * The MDA/CGA/EGA/VGA/whatever fixed MMIO area.
     */
    rc = PDMDevHlpMmioCreateExAndMap(pDevIns, 0x000a0000, 0x00020000,
                                     IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU | IOMMMIO_FLAGS_ABS,
                                     NULL /*pPciDev*/, UINT32_MAX /*iPciRegion*/,
                                     pciVfioVgaMmioWrite, pciVfioVgaMmioRead, pciVfioVgaMmioFill, pFun,
                                     "VFIO VGA - VGA Video Buffer", &pFun->hVgaMmio);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}


/**
 * HC access handler for the ROM.
 *
 * @returns VINF_SUCCESS if the handler have carried out the operation.
 * @returns VINF_PGM_HANDLER_DO_DEFAULT if the caller should carry out the access operation.
 * @param   pVM             VM Handle.
 * @param   pVCpu           The cross context CPU structure for the calling EMT.
 * @param   GCPhys          The physical address the guest is writing to.
 * @param   pvPhys          The HC mapping of that address.
 * @param   pvBuf           What the guest is reading/writing.
 * @param   cbBuf           How much it's reading/writing.
 * @param   enmAccessType   The access type.
 * @param   enmOrigin       Who is making the access.
 * @param   uUser           User argument.
 */
static DECLCALLBACK(VBOXSTRICTRC)
pciVfioRomWriteHandler(PVM pVM, PVMCPU pVCpu, RTGCPHYS GCPhys, void *pvPhys, void *pvBuf, size_t cbBuf,
                          PGMACCESSTYPE enmAccessType, PGMACCESSORIGIN enmOrigin, uint64_t uUser)
{
    RT_NOREF(pVM, pVCpu, GCPhys, pvPhys, pvBuf, cbBuf, enmAccessType, enmOrigin, uUser);

    LogRelMax(10, ("VFIO: Attempted write access to ROM region\n"));
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNPCIIOREGIONMAP}
 */
DECLCALLBACK(int) pciVfioRomRegionMapUnmap(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                           RTGCPHYS GCPhysAddress, RTGCPHYS cb, PCIADDRESSSPACE enmType)
{
    RT_NOREF(iRegion, enmType);
    PVFIOPCI pThis = PDMDEVINS_2_DATA(pDevIns, PVFIOPCI);
    int      rc;
    Assert(pPciDev == pDevIns->apPciDevs[pPciDev->uDevFn & 0x7]);
    PVFIOPCIFUN pFun = &pThis->aPciFuns[pPciDev->uDevFn & 0x7];

    Log(("pciVfioRomRegionMapUnmap: iRegion=%d GCPhysAddress=%RGp cb=%RGp enmType=%d\n", iRegion, GCPhysAddress, cb, enmType));
    if (GCPhysAddress != NIL_RTGCPHYS)
    {
        /* Map the ROM. */
        rc = PDMDevHlpMmio2Map(pDevIns, pFun->hRom, GCPhysAddress);
        AssertRC(rc);

        if (RT_SUCCESS(rc))
        {
            rc = PDMDevHlpPGMHandlerPhysicalRegister(pDevIns, GCPhysAddress, GCPhysAddress + cb - 1,
                                                     pFun->hRomAccHndType, "VFIO ROM");
            AssertRC(rc);
        }

        if (RT_SUCCESS(rc))
        {
            pFun->GCPhysRom = GCPhysAddress;
            Log(("pciVfioRomRegionMapUnmap: GCPhysRom=%RGp cb=%RGp\n", GCPhysAddress, cb));
        }
        rc = VINF_PCI_MAPPING_DONE; /* caller only cares about this status, so it is okay that we overwrite errors here. */
    }
    else
    {
        rc = PDMDevHlpPGMHandlerPhysicalDeregister(pDevIns, pFun->GCPhysRom);
        AssertRC(rc);
        pFun->GCPhysRom = NIL_RTGCPHYS;
    }
    return rc;
}


static int pciVfioSetupRom(PVFIOPCI pThis, PVFIOPCIFUN pFun, PPDMPCIDEV pPciDev, PPDMDEVINS pDevIns)
{
    struct vfio_region_info RegionInfo; RT_ZERO(RegionInfo);
    int rc = pciVfioQueryRegionInfo(pThis, pFun, VFIO_PCI_ROM_REGION_INDEX, &RegionInfo);
    if (RT_FAILURE(rc))
        return rc;

    /* No ROM, nothing to do. */
    if (   RegionInfo.flags == 0
        && RegionInfo.size == 0)
        return VINF_SUCCESS;

    /** @todo Currently we will map the ROM as MMIO2 region as we lack the necessary
     * infrastructure to register ROMs for PCI BARs. This is wrong because MMIO2 regions
     * are mapped read/write. To protect against writes we put an access handler over the region,
     * which will discard (and log) all writes.
     */

    pFun->offRom = RegionInfo.offset;
    pFun->cbRom  = RegionInfo.size;

    rc = PDMDevHlpPCIIORegionCreateMmio2Ex(pDevIns, pPciDev, VBOX_PCI_ROM_SLOT, pFun->cbRom,
                                           PCI_ADDRESS_SPACE_MEM_PREFETCH, 0 /*fFlags*/,
                                           pciVfioRomRegionMapUnmap, "ROM",
                                           &pFun->pvRom, &pFun->hRom);
    AssertLogRelRCReturn(rc, PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                                 N_("Failed to allocate %zu bytes of ROM"), pFun->cbRom));

    ssize_t cbRead = pread(pFun->iFdVfio, pFun->pvRom, pFun->cbRom, pFun->offRom);
    if (cbRead != (ssize_t)pFun->cbRom)
        return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                   N_("Failed to read %zu bytes of ROM from the device"), pFun->cbRom);

    rc = PDMDevHlpPGMHandlerPhysicalTypeRegister(pThis->pDevIns, PGMPHYSHANDLERKIND_WRITE,
                                                 pciVfioRomWriteHandler, "VFIO ROM", &pFun->hRomAccHndType);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNPCIIOREGIONMAP}
 */
DECLCALLBACK(int) pciVfioMmioMapUnmap(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                      RTGCPHYS GCPhysAddress, RTGCPHYS cb, PCIADDRESSSPACE enmType)
{
    RT_NOREF(enmType, cb);
    PVFIOPCI pThis = PDMDEVINS_2_DATA(pDevIns, PVFIOPCI);
    int      rc;
    Assert(pPciDev == pDevIns->apPciDevs[pPciDev->uDevFn & 0x7]);
    PVFIOPCIFUN pFun = &pThis->aPciFuns[pPciDev->uDevFn & 0x7];
    PCVFIOPCIBAR pBar = &pFun->aBars[iRegion];

    Log(("pciVfioMmioMapUnmap: iRegion=%d GCPhysAddress=%RGp cb=%RGp enmType=%d\n", iRegion, GCPhysAddress, cb, enmType));

    if (pBar->fInterceptMmio)
    {
        if (GCPhysAddress != NIL_RTGCPHYS)
        {
            /* Map the region. */
            rc = PDMDevHlpMmioMap(pDevIns, pBar->hnd.hMmio, GCPhysAddress);
            AssertRC(rc);

            /* Map the MSI-X state if this is the BAR. */
            if (pFun->iBarMsix == iRegion)
            {
                rc = PDMDevHlpMmioMap(pDevIns, pFun->hMsixMmio, GCPhysAddress + pBar->cbRegion);
                AssertRC(rc);
            }
        }
        else
        {
            rc = PDMDevHlpMmioUnmap(pDevIns, pBar->hnd.hMmio);
            AssertRC(rc);

            /* Unmap the MSI-X state if this is the BAR. */
            if (pFun->iBarMsix == iRegion)
            {
                rc = PDMDevHlpMmioUnmap(pDevIns, pFun->hMsixMmio);
                AssertRC(rc);
            }
        }
    }
    else
    {
        if (GCPhysAddress != NIL_RTGCPHYS)
        {
            /* Map the region. */
            rc = PDMDevHlpMmio2Map(pDevIns, pBar->hnd.hMmio2, GCPhysAddress);
            AssertRC(rc);

            /* Map the MSI-X state if this is the BAR. */
            if (pFun->iBarMsix == iRegion)
            {
                rc = PDMDevHlpMmio2Map(pDevIns, pFun->hMsix, GCPhysAddress + pBar->cbRegion);
                AssertRC(rc);
            }
        }
        else
        {
            rc = PDMDevHlpMmio2Unmap(pDevIns, pBar->hnd.hMmio2);
            AssertRC(rc);

            /* Unmap the MSI-X state if this is the BAR. */
            if (pFun->iBarMsix == iRegion)
            {
                rc = PDMDevHlpMmio2Unmap(pDevIns, pFun->hMsix);
                AssertRC(rc);
            }
        }
    }

    return VINF_PCI_MAPPING_DONE;
}


/**
 * The IRQ poller thread.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThread     The command thread.
 */
static DECLCALLBACK(int) pciVfioIrqPoller(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    PVFIOPCI    pThis = PDMDEVINS_2_DATA(pDevIns, PVFIOPCI);
    PVFIOPCIFUN pFun = (PVFIOPCIFUN)pThread->pvUser;
    PPDMPCIDEV  pPciDev = pDevIns->apPciDevs[pFun->uPciFun];

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    /* The poll structure for the interrupts. */
    uint32_t        cEntriesAlloc = 2;
    struct pollfd   *paIrqFds = (struct pollfd *)RTMemAllocZ(cEntriesAlloc * sizeof(*paIrqFds));
    if (!paIrqFds)
    {
        LogRel(("VFIO#%d.%u: Failed to allocate memory for %u interrupt polling entries",
                pThis->iInstance, pFun->uPciFun, cEntriesAlloc));
        return VERR_NO_MEMORY;
    }

    paIrqFds[0].fd = pFun->iFdWakeup;
    paIrqFds[0].events = POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI | POLLERR;

    uint32_t cIrqs = 0;
    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        uint8_t uIrqModeNew = ASMAtomicReadU8(&pFun->uIrqModeNew);
        if (pFun->uIrqModeCur != uIrqModeNew)
        {
            switch (uIrqModeNew)
            {
                case UINT8_MAX:
                {
                    cIrqs = 0;
                    break;
                }
                default:
                {
                    cIrqs = pFun->acIrqVectors[uIrqModeNew];
                    break;
                }
            }

            /* Resize the pollfd array if necessary. */
            uint32_t const cNeeded = cIrqs + 1;
            if (cNeeded > cEntriesAlloc)
            {
                struct pollfd *paIrqFdsNew = (struct pollfd *)RTMemRealloc(paIrqFds, cNeeded * sizeof(*paIrqFds));
                if (!paIrqFdsNew)
                {
                    /** @todo This is quite wrong, we could allocate all possible entries up front and potentially waste memory... */
                    LogRel(("VFIO#%d.%u: Failed to allocate memory for %u interrupt polling entries",
                            pThis->iInstance, pFun->uPciFun, cEntriesAlloc));
                    RTMemFree(paIrqFds);
                    return VERR_NO_MEMORY;
                }

                paIrqFds      = paIrqFdsNew;
                cEntriesAlloc = cNeeded;
            }

            for (uint32_t i = 0; i < cIrqs; i++)
            {
                paIrqFds[i + 1].fd = pFun->afdEvts[i];
                paIrqFds[i + 1].events = POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI | POLLERR;
            }

            ASMAtomicWriteU8(&pFun->uIrqModeCur, uIrqModeNew);

            /* Inform the initiator of the mode switch .*/
            RTThreadUserSignal(pThread->Thread);
        }

        int rcPsx = poll(&paIrqFds[0], 1 + cIrqs, -1);
        if (rcPsx > 0)
        {
            if (paIrqFds[0].revents)
            {
                /* We got woken up externally. */
                paIrqFds[0].revents = 0;
                uint64_t u64;
                ssize_t cb = read(paIrqFds[0].fd, &u64, sizeof(u64));
                Assert(cb == sizeof(u64)); RT_NOREF(cb);
                if (pThread->enmState != PDMTHREADSTATE_RUNNING)
                    break;
            }

            for (uint32_t i = 1; i < cIrqs + 1; i++)
            {
                if (paIrqFds[i].revents)
                {
                    paIrqFds[i].revents = 0;
                    uint64_t u64;
                    ssize_t cb = read(paIrqFds[i].fd, &u64, sizeof(u64));
                    Assert(cb == sizeof(u64)); RT_NOREF(cb);

                    uint16_t const uVec = i - 1;
                    if (pFun->uIrqModeCur == VFIO_PCI_MSIX_IRQ_INDEX)
                    {
                        /* Can't use PDMDevHlpPCISetIrqEx here as the PCI stuff is not aware of MSI-X support. */
                        PMSIXTBLENTRY pVec = &pFun->paMsixTbl[uVec];

                        uint16_t uMmc = PDMPciDevGetWord(pPciDev, pFun->offMsixCtrl);

                        /* Mark as pending if vector is disabled. */
                        if (   uMmc & RT_BIT(14)
                            || pVec->u32VecCtrl & RT_BIT_32(0))
                        {
                            uint16_t offPba = uVec / 64;
                            uint8_t  idxBit = uVec & 0x3f;
                            volatile uint64_t *pbmPba = &pFun->pbmMsixPba[offPba];
                            ASMAtomicBitSet(pbmPba, idxBit);
                        }
                        else
                        {
                            /** @todo This isn't pretty as it accesses stuff internal to the PCI subsystem. */
                            PCPDMPCIHLPR3 pPciHlp = (PCPDMPCIHLPR3)pPciDev->Int.s.pvPciBusPtrR3;
                            MSIMSG Msi;
                            Msi.Addr.u64 = ((uint64_t)pVec->u32MsgAddrUpper << 32) | pVec->u32MsgAddr;
                            Msi.Data.u32 = pVec->u32MsgData;

                            PPDMDEVINS pDevInsBus = pPciHlp->pfnGetBusByNo(pDevIns, pPciDev->Int.s.idxPdmBus);
                            Assert(pDevInsBus);
                            PDEVPCIBUS pBus = PDMINS_2_DATA(pDevInsBus, PDEVPCIBUS);
                            uint16_t const uBusDevFn = PCIBDF_MAKE(pBus->iBus, pPciDev->uDevFn);

                            pPciHlp->pfnIoApicSendMsi(pDevIns, uBusDevFn, &Msi, 0 /*uTagSrc*/);
                        }
                    }
                    else
                        PDMDevHlpPCISetIrqEx(pDevIns, pPciDev, uVec, 1);

                    if (pFun->uIrqModeCur == VFIO_PCI_INTX_IRQ_INDEX)
                    {
                        /** @todo The interrupt seems to be masked and we would need a mechanism
                         * to get notified when the interrupt is de-asserted in the interrupt controller
                         * (through an EOI for example) so we can unmask them. With KVM this is supported
                         * when KVM_CAP_IRQFD_RESAMPLE is available.
                         */
#if 0
                        union
                        {
                            struct vfio_irq_set IrqSet;
                            uint32_t            au32[sizeof(struct vfio_irq_set) / sizeof(uint32_t) + 1];
                        } uBuf;

                        uBuf.IrqSet.argsz = sizeof(uBuf);
                        uBuf.IrqSet.flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_UNMASK;
                        uBuf.IrqSet.index = i;
                        uBuf.IrqSet.start = 0;
                        uBuf.IrqSet.count = 1;
                        uBuf.au32[sizeof(struct vfio_irq_set) / sizeof(uint32_t)] = pThis->aIrqFds[i].fd;

                        int rcLnx = ioctl(pThis->iFdVfio, VFIO_DEVICE_SET_IRQS, &uBuf);
                        if (rcLnx == -1)
                            LogRel(("Unmasking one INTX interrupt failed with %d", errno));
#endif
                    }
                }
            }
        }
        else
            AssertFailed();
    }

    RTMemFree(paIrqFds);
    LogFlowFunc(("Poller thread terminating\n"));
    return VINF_SUCCESS;
}


/**
 * Wakes up the IRQ poller to respond to state changes.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThread     The command thread.
 */
static DECLCALLBACK(int) pciVfioIrqPollerWakeup(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    RT_NOREF(pDevIns);
    Log4Func(("\n"));
    PVFIOPCIFUN pFun = (PVFIOPCIFUN)pThread->pvUser;

    uint64_t u64 = 1;
    ssize_t cb = write(pFun->iFdWakeup, &u64, sizeof(u64));
    Assert(cb == sizeof(u64)); RT_NOREF(cb);

    return VINF_SUCCESS;
}


static int pciVfioIrqPollerSwitchMode(PVFIOPCI pThis, PVFIOPCIFUN pFun, uint32_t uVfioIrq, bool fWait)
{
    /* Wakeup the IRQ poller to switch to the new mode. */
    RTThreadUserReset(pFun->pThrdIrq->Thread);
    ASMAtomicWriteU8(&pFun->uIrqModeNew, uVfioIrq);
    pciVfioIrqPollerWakeup(pThis->pDevIns, pFun->pThrdIrq);

    /* Wait until it got confirmed. */
    if (fWait)
        return RTThreadUserWait(pFun->pThrdIrq->Thread, 5 * RT_MS_1SEC);

    return VINF_SUCCESS;
}


DECLINLINE(int) pciVfioQueryIrqInfo(PVFIOPCI pThis, PCVFIOPCIFUN pFun, uint32_t uIrq, uint32_t *pcVectors)
{
    struct vfio_irq_info IrqInfo;
    RT_ZERO(IrqInfo);
    IrqInfo.argsz = sizeof(IrqInfo);
    IrqInfo.index = uIrq;

    int rcLnx = ioctl(pFun->iFdVfio, VFIO_DEVICE_GET_IRQ_INFO, &IrqInfo);
    if (rcLnx == -1)
        return PDMDevHlpVMSetError(pThis->pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                N_("Getting information for irq %u of opened VFIO device failed with %d"), uIrq, errno);

    const int iInstance = pThis->iInstance;
    LogRel(("VFIO#%d.%u: Irq %u:\n"
            "VFIO#%d.%u:     flags:       %#RX32\n"
            "VFIO#%d.%u:     index:       %#RU32\n"
            "VFIO#%d.%u:     count:       %#RU32\n",
            iInstance, pFun->uPciFun, uIrq,
            iInstance, pFun->uPciFun, IrqInfo.flags,
            iInstance, pFun->uPciFun, IrqInfo.index,
            iInstance, pFun->uPciFun, IrqInfo.count));

    *pcVectors = IrqInfo.count;
    return VINF_SUCCESS;
}


static int pciVfioIrqReconfigure(PVFIOPCI pThis, PVFIOPCIFUN pFun, uint32_t uVfioIrq, uint16_t cVectors)
{
    int rc = VINF_SUCCESS;
    uint8_t uIrqModeCur = ASMAtomicReadU8(&pFun->uIrqModeCur);

    if (   !cVectors
        || (   uIrqModeCur != uVfioIrq
            && uIrqModeCur != UINT8_MAX))
    {
        /* Clear. */
        rc = pciVfioIrqPollerSwitchMode(pThis, pFun, UINT8_MAX, true /*fWait*/);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pThis->pDevIns, rc, RT_SRC_POS,
                                       N_("Switching the IRQ poller mode failed"));

        /* Disable the interrupt mode. */
        struct vfio_irq_set IrqSet;
        IrqSet.argsz = sizeof(IrqSet);
        IrqSet.flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_TRIGGER;
        IrqSet.index = uIrqModeCur;
        IrqSet.start = 0;
        IrqSet.count = 0;

        int rcLnx = ioctl(pFun->iFdVfio, VFIO_DEVICE_SET_IRQS, &IrqSet);
        if (rcLnx == -1)
            return PDMDevHlpVMSetError(pThis->pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("Clearing interrupts on the device failed with %d"), errno);

        if (   uIrqModeCur == VFIO_PCI_MSI_IRQ_INDEX
            || uIrqModeCur == VFIO_PCI_MSIX_IRQ_INDEX)
        {
            /*
             * When disabling MSI/MSI-X interrupts the kernel will always switch to INTx.
             * In case it is masked in our device turn it off as well, otherwise switch
             * to INTx mode and fall through below.
             */
            uint16_t u16Cmd = PDMPciDevGetCommand(pThis->pDevIns->apPciDevs[pFun->uPciFun]);
            if (u16Cmd & RT_BIT(10))
            {
#if 0
                /* Disable INTx again. */
                IrqSet.argsz = sizeof(IrqSet);
                IrqSet.flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_TRIGGER;
                IrqSet.index = VFIO_PCI_INTX_IRQ_INDEX;
                IrqSet.start = 0;
                IrqSet.count = 0;

                rcLnx = ioctl(pFun->iFdVfio, VFIO_DEVICE_SET_IRQS, &IrqSet);
                if (rcLnx == -1)
                    return PDMDevHlpVMSetError(pThis->pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                               N_("Clearing INTx interrupts on the device failed with %d"), errno);

                return VINF_SUCCESS;
#endif
            }
            else
                uVfioIrq = VFIO_PCI_INTX_IRQ_INDEX;
        }
    }

    Assert(ASMAtomicReadU8(&pFun->uIrqModeCur) == UINT8_MAX);

    if (cVectors)
    {
        if (uVfioIrq == VFIO_PCI_INTX_IRQ_INDEX)
        {
            Assert(cVectors == 1);

            union
            {
                struct vfio_irq_set IrqSet;
                uint32_t            au32[sizeof(struct vfio_irq_set) / sizeof(uint32_t) + 1];
            } uBuf;

            uBuf.IrqSet.argsz = sizeof(uBuf);
            uBuf.IrqSet.flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
            uBuf.IrqSet.index = uVfioIrq;
            uBuf.IrqSet.start = 0;
            uBuf.IrqSet.count = 1;
            uBuf.au32[sizeof(struct vfio_irq_set) / sizeof(uint32_t)] = pFun->afdEvts[0];

            int rcLnx = ioctl(pFun->iFdVfio, VFIO_DEVICE_SET_IRQS, &uBuf);
            if (rcLnx == -1)
                return PDMDevHlpVMSetError(pThis->pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                        N_("Assigning one INTX interrupt failed with %d (%u)"), errno, sizeof(uBuf));
        }
        else if (   uVfioIrq == VFIO_PCI_MSI_IRQ_INDEX
                 || uVfioIrq == VFIO_PCI_MSIX_IRQ_INDEX)
        {
            union
            {
                struct vfio_irq_set IrqSet;
                uint32_t            au32[sizeof(struct vfio_irq_set) / sizeof(uint32_t) + VBOX_MSI_MAX_ENTRIES];
            } uBuf;

            uBuf.IrqSet.argsz = sizeof(uBuf.IrqSet) + cVectors * sizeof(uint32_t);
            uBuf.IrqSet.flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
            uBuf.IrqSet.index = uVfioIrq;
            uBuf.IrqSet.start = 0;
            uBuf.IrqSet.count = cVectors;

            for (uint32_t i = 0; i < cVectors; i++)
                uBuf.au32[sizeof(struct vfio_irq_set) / sizeof(uint32_t) + i] = pFun->afdEvts[i];

            int rcLnx = ioctl(pFun->iFdVfio, VFIO_DEVICE_SET_IRQS, &uBuf);
            if (rcLnx == -1)
                return PDMDevHlpVMSetError(pThis->pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                        N_("Assigning MSI/MSI-X interrupts failed with %d (%u)"), errno, sizeof(uBuf));
        }
        else
            AssertReleaseFailed();

        rc = pciVfioIrqPollerSwitchMode(pThis, pFun, uVfioIrq, false /*fWait*/);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pThis->pDevIns, rc, RT_SRC_POS,
                                       N_("Switching the IRQ poller mode failed"));
    }

    return VINF_SUCCESS;
}


DECLINLINE(void) pciVfioCfgSpaceSetInterceptU8(PVFIOPCIFUN pFun, uint32_t off, uint8_t fRd, uint8_t fWr)
{
    AssertReturnVoid(off < sizeof(pFun->abPciCfgIntercept) * 8 / 4);
    uint32_t offByte = off >> 1;
    uint8_t  cShift  = (off & 0x1) ? 4 : 0;

    pFun->abPciCfgIntercept[offByte] |= ((fWr << 2) | fRd) << cShift;
}


DECLINLINE(void) pciVfioCfgSpaceSetInterceptRoU8(PVFIOPCIFUN pFun, uint32_t off, uint8_t fRd)
{
    pciVfioCfgSpaceSetInterceptU8(pFun, off,     fRd, VFIO_PCI_CFG_SPACE_ACCESS_INVALID);
}


DECLINLINE(void) pciVfioCfgSpaceSetInterceptU16(PVFIOPCIFUN pFun, uint32_t off, uint8_t fRd, uint8_t fWr)
{
    pciVfioCfgSpaceSetInterceptU8(pFun, off,     fRd, fWr);
    pciVfioCfgSpaceSetInterceptU8(pFun, off + 1, fRd, fWr);
}


DECLINLINE(void) pciVfioCfgSpaceSetInterceptRoU16(PVFIOPCIFUN pFun, uint32_t off, uint8_t fRd)
{
    pciVfioCfgSpaceSetInterceptU8(pFun, off,     fRd, VFIO_PCI_CFG_SPACE_ACCESS_INVALID);
    pciVfioCfgSpaceSetInterceptU8(pFun, off + 1, fRd, VFIO_PCI_CFG_SPACE_ACCESS_INVALID);
}


DECLINLINE(void) pciVfioCfgSpaceSetInterceptU32(PVFIOPCIFUN pFun, uint32_t off, uint8_t fRd, uint8_t fWr)
{
    pciVfioCfgSpaceSetInterceptU16(pFun, off,     fRd, fWr);
    pciVfioCfgSpaceSetInterceptU16(pFun, off + 2, fRd, fWr);
}


DECLINLINE(void) pciVfioCfgSpaceSetInterceptRoU32(PVFIOPCIFUN pFun, uint32_t off, uint8_t fRd)
{
    pciVfioCfgSpaceSetInterceptRoU16(pFun, off,     fRd);
    pciVfioCfgSpaceSetInterceptRoU16(pFun, off + 2, fRd);
}


DECLINLINE(uint8_t) pciVfioCfgSpaceGetInterceptRd(PVFIOPCIFUN pFun, uint32_t off)
{
    AssertReturn(off < sizeof(pFun->abPciCfgIntercept) * 8 / 4, VFIO_PCI_CFG_SPACE_ACCESS_INVALID);

    uint32_t offByte = off >> 1;
    uint8_t  cShift  = (off & 0x1) ? 4 : 0;

    return (pFun->abPciCfgIntercept[offByte] >> cShift) & 0x3;
}


DECLINLINE(uint8_t) pciVfioCfgSpaceGetInterceptWr(PVFIOPCIFUN pFun, uint32_t off)
{
    AssertReturn(off < sizeof(pFun->abPciCfgIntercept) * 8 / 4, VFIO_PCI_CFG_SPACE_ACCESS_INVALID);

    uint32_t offByte = off >> 1;
    uint8_t  cShift  = (off & 0x1) ? 4 + 2 : 2;

    return (pFun->abPciCfgIntercept[offByte] >> cShift) & 0x3;
}


static int pciVfioTrySetupMsix(PVFIOPCI pThis, PVFIOPCIFUN pFun, PPDMPCIDEV pPciDev, uint8_t offCap)
{
    PPDMDEVINS pDevIns = pThis->pDevIns;

    /*
     * For now we will only support MSI-X if the Table and PBA are on a dedicated BAR and are separated by
     * a page size alignment.
     */
    /** @todo Our own MSI-X support is not very efficient as the Table and PBA are standard MMIO regions
     * causing page faults and emulation when the guest accesses it, even for reads. They could be made
     * MMIO2 regions with a write protection layer on top, so only write accesses are faulting.
     * However I don't feel like fixing that just now and deal with saved states compatibility and testing
     * each and every device emulation supporting MSI-X, so we will do everything on our own here and maybe
     * port that over later. */

    /* First get at the information from the capability. */
    uint16_t u16Mmc = 0;
    int rc = pciVfioCfgSpaceReadU16(pFun, offCap + 2, &u16Mmc);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Failed to read MSI-X message control register with %Rrc"), rc);

    uint32_t u32TblCfg = 0;
    rc = pciVfioCfgSpaceReadU32(pFun, offCap + 4, &u32TblCfg);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Failed to read MSI-X table config register with %Rrc"), rc);

    uint32_t u32PbaCfg = 0;
    rc = pciVfioCfgSpaceReadU32(pFun, offCap + 8, &u32PbaCfg);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Failed to read MSI-X PBA config register with %Rrc"), rc);

    uint16_t cVectors = (u16Mmc & 0x7ff) + 1;
    uint8_t  uTblBir  = u32TblCfg & 0x7;
    uint8_t  uPbaBir  = u32PbaCfg & 0x7;
    uint32_t offTbl   = u32TblCfg & UINT32_C(0xfffffff8);
    uint32_t offPba   = u32PbaCfg & UINT32_C(0xfffffff8);

    if (   uTblBir >= 6                                            /* 6 and 7 are reserved, indicates a broken device. */
        || uTblBir != uPbaBir                                      /* Same region */
        || uTblBir != pFun->iBarMsix                               /* BAR doesn't match what VFIO presented to us. */
        || offPba <= offTbl                                        /* PBA must not intersect table. */
        || cVectors != pFun->acIrqVectors[VFIO_PCI_MSIX_IRQ_INDEX] /* The indicated table size must match what VFIO reported as number of IRQ vectors. */
       )
    {
        /* Just log and don't set an error. */
        LogRel(("VFIO#%d.%u: MSI-X not supported for this device as it doesn't meet the requirements: uTblBir=%u uPbaBir=%u offTbl=%#x offPba=%#x cVectors=%u cVfioVectors=%u\n",
                pThis->iInstance, pFun->uPciFun, uTblBir, uPbaBir, offTbl, offPba, cVectors, pFun->acIrqVectors[VFIO_PCI_MSIX_IRQ_INDEX]));
        return VERR_NOT_SUPPORTED;
    }

    uint32_t const cbMsix = RT_ALIGN_32((offPba - offTbl) + RT_MAX(8, cVectors / 8), GUEST_PAGE_SIZE);
    if (!(offTbl & ~GUEST_PAGE_OFFSET_MASK))
    {
        /* Dedicated MSI-X BARs get handled differently. */
        if (offTbl == 0)
        {
            /* Clear the BAR containing the MSI-X region so it doesn't get mapped later on. */
            Assert(pFun->aBars[uTblBir].bType == 2);
            pFun->aBars[uTblBir].bType = 0;

            rc = PDMDevHlpPCIIORegionCreateMmio2Ex(pDevIns, pPciDev, uTblBir, cbMsix,
                                                   PCI_ADDRESS_SPACE_MEM_PREFETCH, 0 /*fFlags*/,
                                                   /*pciVfioMsixMapUnmap*/ NULL, "MSI-X", (void **)&pFun->pbMsix, &pFun->hMsix);
        }
        else
        {
            /*
             * MSI-X state is part of an MMIO region but page aligned so just allocate the MMIO2 memory without setting up
             * the PCI region.
             */
            rc = PDMDevHlpMmio2Create(pDevIns, pPciDev, uTblBir + 0x10, cbMsix,
                                      0 /*fFlags*/, "MSI-X State", (void **)&pFun->pbMsix, &pFun->hMsix);
        }

        AssertLogRelRCReturn(rc, PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                                     N_("Failed to allocate %zu bytes of MSI-X state"), cbMsix));

#if 0
        rc = PDMDevHlpPGMHandlerPhysicalTypeRegister(pThis->pDevIns, PGMPHYSHANDLERKIND_WRITE,
                                                     pciVfioMsixWriteHandler, "VFIO MSI-X", &pFun->hMsixWrHndType);
        AssertRCReturn(rc, rc);
#endif
    }
    else
    {
        /*
         * The MSI-X region is not page aligned so we have to intercept accesses to the whole BAR,
         * which can be quite inefficient depending on the device when the guest accesses the MMIO
         * region extensively.
         */
        /** @todo Maybe it would be better performance wise to just mask MSI-X support and let the guest
         *        resort to MSI or INTX style interrupts. */
        pFun->aBars[uTblBir].fInterceptMmio = true;
        LogRel(("VFIO#%d.%u: Intercepting accesses to MMIO region %u because MSI-X state is not page aligned\n",
                pThis->iInstance, pFun->uPciFun, uTblBir));

        pFun->pbMsix = (uint8_t *)PDMDevHlpMMHeapAllocZ(pDevIns, cbMsix);
        AssertLogRelReturn(pFun->pbMsix, PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                                             N_("Failed to allocate %zu bytes of MSI-X state"), cbMsix));
    }

    pFun->paMsixTbl  = (PMSIXTBLENTRY)pFun->pbMsix;
    pFun->pbmMsixPba = (volatile uint64_t *)pFun->pbMsix;
    pFun->cbMsix     = cbMsix;

    /* Each vector is masked by default. */
    PMSIXTBLENTRY pMsixEntry = (PMSIXTBLENTRY)pFun->paMsixTbl;
    for (uint16_t i = 0; i < cVectors; i++)
    {
        pMsixEntry->u32VecCtrl = RT_BIT(0);
        pMsixEntry++;
    }

    /* Configure the capability if everything succeeded. */
    pFun->offMsixCtrl = offCap + 2;
    PDMPciDevSetWord( pPciDev, offCap + 2, u16Mmc);           /* Message control */
    PDMPciDevSetDWord(pPciDev, offCap + 4, offTbl | uTblBir); /* Table config */
    PDMPciDevSetDWord(pPciDev, offCap + 8, offPba | uPbaBir); /* PBA config   */
    pciVfioCfgSpaceSetInterceptU16(  pFun, offCap + 2, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT, VFIO_PCI_CFG_SPACE_ACCESS_EMULATE);    /* Message Control */
    pciVfioCfgSpaceSetInterceptRoU32(pFun, offCap + 4, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT);                                       /* Table config */
    pciVfioCfgSpaceSetInterceptRoU32(pFun, offCap + 8, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT);                                       /* PBA config */

    return VINF_SUCCESS;
}


static int pciVfioCfgSpaceParseCapabilities(PVFIOPCI pThis, PVFIOPCIFUN pFun, PPDMPCIDEV pPciDev)
{
    PPDMDEVINS pDevIns = pThis->pDevIns;

    /* Initialize with 0. */
    PDMPciDevSetCapabilityList(pPciDev, 0);

    /*
     * Try building a 1:1 mapping of the capabilities, punching holes for capabilities
     * currently not being supported.
     */
    uint8_t offCap = 0;
    int rc = pciVfioCfgSpaceReadU8(pFun, VBOX_PCI_CAPABILITY_LIST, &offCap);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Failed to read capabilities list pointer with %Rrc"), rc);

    pciVfioCfgSpaceSetInterceptRoU8(pFun, VBOX_PCI_CAPABILITY_LIST, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT);
    if (!offCap)
    {
        /* No capabilities, return early. */
        return VINF_SUCCESS;
    }

    /* This ASSUMES that the cpabilities are not going backwards when pointing to the next one. */
    uint8_t offCapNextPrev = VBOX_PCI_CAPABILITY_LIST;
    for (;;)
    {
        uint8_t bCapId = 0;
        rc = pciVfioCfgSpaceReadU8(pFun, offCap, &bCapId);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Failed to read capabilitiy ID at offset %#x with %Rrc"), offCap, rc);

        uint8_t offCapNext = 0;
        rc = pciVfioCfgSpaceReadU8(pFun, offCap + 1, &offCapNext);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Failed to read next capability pointer at offset %#x with %Rrc"), offCap, rc);

        uint8_t cbCap = 2;
        bool    fSupported = false;
        switch (bCapId)
        {
            case VBOX_PCI_CAP_ID_PM:  LogRel(("VFIO#%d.%u: Cap[%#x]: PCI Power Management Interface -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap)); break;
            case VBOX_PCI_CAP_ID_AGP: LogRel(("VFIO#%d.%u: Cap[%#x]: AGP -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap));                            break;
            case VBOX_PCI_CAP_ID_VPD:
            {
                LogRel(("VFIO#%d.%u: Cap[%#x]: VPD -> passthrough\n", pThis->iInstance, pFun->uPciFun, offCap));
                pciVfioCfgSpaceSetInterceptU16(pFun, offCap + 2, VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH, VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH);
                pciVfioCfgSpaceSetInterceptU32(pFun, offCap + 4, VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH, VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH);
                cbCap      = 2 + 2 + 4;
                fSupported = true;
                break;
            }
            case VBOX_PCI_CAP_ID_SLOTID: LogRel(("VFIO#%d.%u: Cap[%#x]: Slot Identification -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap)); break;
            case VBOX_PCI_CAP_ID_MSI:
            {
                LogRel(("VFIO#%d.%u: Cap[%#x]: Message Signaled Interrupts -> emulate\n", pThis->iInstance, pFun->uPciFun, offCap));
                fSupported = true;

                /* Read the message control from the device. */
                uint16_t u16Mmc = 0;
                rc = pciVfioCfgSpaceReadU16(pFun, offCap + 2, &u16Mmc);
                if (RT_FAILURE(rc))
                    return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                               N_("Failed to read MSI message control register with %Rrc"), rc);

                bool f64Bit = RT_BOOL(u16Mmc & VBOX_PCI_MSI_FLAGS_64BIT);
                uint8_t cVectors = RT_BIT((u16Mmc & VBOX_PCI_MSI_FLAGS_QMASK) >> 1);

                /* Add capability. */
                PDMMSIREG MsiReg;
                RT_ZERO(MsiReg);
                MsiReg.cMsiVectors     = cVectors;
                MsiReg.iMsiCapOffset   = offCap;
                MsiReg.iMsiNextOffset  = 0; /* Gets updated later. */
                MsiReg.fMsi64bit       = f64Bit;
                rc = PDMDevHlpPCIRegisterMsiEx(pDevIns, pPciDev, &MsiReg);
                if (RT_FAILURE(rc))
                    AssertFailed();

                pciVfioCfgSpaceSetInterceptU16(pFun, offCap +  2, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT, VFIO_PCI_CFG_SPACE_ACCESS_EMULATE);    /* Message Control */
                pciVfioCfgSpaceSetInterceptU32(pFun, offCap +  4, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT); /* Message Address [Low] */
                if (f64Bit)
                {
                    pciVfioCfgSpaceSetInterceptU32(pFun, offCap +  8, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT); /* Message Address High */
                    pciVfioCfgSpaceSetInterceptU32(pFun, offCap + 12, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT); /* Message Data */

                    /* We always implement MSI with per-vector masking support. */
                    pciVfioCfgSpaceSetInterceptU32(pFun, offCap + 16, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT); /* Mask */
                    pciVfioCfgSpaceSetInterceptU32(pFun, offCap + 20, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT); /* Pending */
                }
                else
                {
                    pciVfioCfgSpaceSetInterceptU32(pFun, offCap + 8, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT); /* Message Data */

                    /* We always implement MSI with per-vector masking support. */
                    pciVfioCfgSpaceSetInterceptU32(pFun, offCap + 12, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT); /* Mask */
                    pciVfioCfgSpaceSetInterceptU32(pFun, offCap + 16, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT); /* Pending */
                }

                pFun->offMsiCtrl = offCap + 2;
                break;
            }
            case VBOX_PCI_CAP_ID_CHSWP: LogRel(("VFIO#%d.%u: Cap[%#x]: CompactPCI Hot Swap -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap)); break;
            case VBOX_PCI_CAP_ID_PCIX:  LogRel(("VFIO#%d.%u: Cap[%#x]: PCI-X -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap));               break;
            case VBOX_PCI_CAP_ID_HT:    LogRel(("VFIO#%d.%u: Cap[%#x]: HyperTransport -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap));      break;
            case VBOX_PCI_CAP_ID_VNDR:
            {
                LogRel(("VFIO#%d.%u: Cap[%#x]: Vendor Specific -> passthrough\n", pThis->iInstance, pFun->uPciFun, offCap));

                /* The next byte after the header is a length field. */
                uint8_t cbVendor = 0;
                rc = pciVfioCfgSpaceReadU8(pFun, offCap + 2, &cbVendor);
                if (RT_FAILURE(rc))
                    return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                               N_("Failed to read vendor length field at offset %#x with %Rrc"), offCap + 2, rc);
                if (cbVendor < 2 || cbVendor > 256 - offCap)
                    return PDMDevHlpVMSetError(pDevIns, VERR_BUFFER_OVERFLOW, RT_SRC_POS,
                                               N_("Invalid vendor length field %#x"), cbVendor);

                for (uint8_t i = 0; i < cbVendor - 2; i++)
                    pciVfioCfgSpaceSetInterceptU8(pFun, offCap + 2 + i, VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH, VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH);

                cbCap      = cbVendor;
                fSupported = true;
                break;
            }
            case VBOX_PCI_CAP_ID_DBG:    LogRel(("VFIO#%d.%u: Cap[%#x]: Debug port -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap));                          break;
            case VBOX_PCI_CAP_ID_CCRC:   LogRel(("VFIO#%d.%u: Cap[%#x]: CompactPCI central resource control -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap)); break;
            case VBOX_PCI_CAP_ID_SHPC:   LogRel(("VFIO#%d.%u: Cap[%#x]: Standard PCI Hot-Plug Controller -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap));    break;
            case VBOX_PCI_CAP_ID_SSVID:  LogRel(("VFIO#%d.%u: Cap[%#x]: SPCI Bridge Subsystem Vendor ID -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap));     break;
            case VBOX_PCI_CAP_ID_AGP3:   LogRel(("VFIO#%d.%u: Cap[%#x]: AGP 8x -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap));                              break;
            case VBOX_PCI_CAP_ID_SECURE: LogRel(("VFIO#%d.%u: Cap[%#x]: Secure Device -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap));                       break;
            case VBOX_PCI_CAP_ID_EXP:
            {
                LogRel(("VFIO#%d.%u: Cap[%#x]: PCI Express -> emulate\n", pThis->iInstance, pFun->uPciFun, offCap));
                fSupported = true;

#if 1 /** @todo Proper support. */
                uint16_t u16ExpReg = 0;
                rc = pciVfioCfgSpaceReadU16(pFun, offCap + 2, &u16ExpReg);
                if (RT_FAILURE(rc))
                    return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                               N_("Failed to read PCI Express Capabilities Register at offset %#x with %Rrc"), offCap + 2, rc);

                uint8_t const bVers = u16ExpReg & 0xf;
                uint8_t cbCap2 = 0;
                if (bVers == 1)
                    cbCap2 = 36;
                else if (bVers == 2)
                    cbCap2 = 60;

                for (uint16_t i = offCap + 2; i < (uint16_t)offCap + cbCap2; i += 2)
                    pciVfioCfgSpaceSetInterceptU16(pFun, i, VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH, VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH);
                cbCap = cbCap2;
#endif

                break;
            }
            case VBOX_PCI_CAP_ID_MSIX:
            {
                LogRel(("VFIO#%d.%u: Cap[%#x]: MSI-X -> emulate\n", pThis->iInstance, pFun->uPciFun, offCap));
                rc = pciVfioTrySetupMsix(pThis, pFun, pPciDev, offCap);
                if (RT_SUCCESS(rc))
                {
                    fSupported = true;
                    cbCap      = 12;
                }
                break;
            }
            case VBOX_PCI_CAP_ID_SATA: LogRel(("VFIO#%d.%u: Cap[%#x]: Serial ATA HBA -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap));        break;
            case VBOX_PCI_CAP_ID_AF:   LogRel(("VFIO#%d.%u: Cap[%#x]: PCI Advanced Features -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap)); break;
            default:
                LogRel(("VFIO#%d.%u: Cap[%#x]: Unknown capability ID %#x encountered -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapId));
                break;
        }

        if (fSupported)
        {
            /* Accesses to capability ID and pointer to the next capability are never passed through. */
            pciVfioCfgSpaceSetInterceptRoU16(pFun, offCap, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT);
            PDMPciDevSetByte(pPciDev, offCap, bCapId);
            PDMPciDevSetByte(pPciDev, offCapNextPrev, offCap);
            offCapNextPrev = offCap + 1;
        }

        if (!offCapNext)
            break;

        if (   offCapNext < offCap
            || offCapNext < offCap + cbCap)
            return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_STATE, RT_SRC_POS,
                                       N_("Next capability pointer points backwards or inside the current capability (next offset %#x )"), offCapNext);

        offCap = offCapNext;
    }

    /* Mark the end of the list. */
    PDMPciDevSetByte(pPciDev, offCapNextPrev, 0);

    return VINF_SUCCESS;
}


static int pciVfioCfgSpaceParseExtCapabilities(PVFIOPCI pThis, PVFIOPCIFUN pFun, PPDMPCIDEV pPciDev)
{
    PPDMDEVINS pDevIns = pThis->pDevIns;
    RT_NOREF(pPciDev);

    uint16_t offCap = 256;

    /* This ASSUMES that the cpabilities are not going backwards when pointing to the next one. */
    for (;;)
    {
        uint32_t u32CapHdr = 0;
        int rc = pciVfioCfgSpaceReadU32(pFun, offCap, &u32CapHdr);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Failed to read extended capabilitiy header at offset %#x with %Rrc"), offCap, rc);

        if (!u32CapHdr)
            break;

        uint16_t const u16CapId   = u32CapHdr & UINT16_C(0xffff);
        uint8_t  const bCapVers   = (u32CapHdr >> 16) & 0xf; RT_NOREF(bCapVers);
        uint16_t const offCapNext = (u32CapHdr >> 20) & 0xfff;
        uint16_t       cbCap      = sizeof(uint32_t);
        bool fSupported = false;
        switch (u16CapId)
        {
            case VBOX_PCI_EXT_CAP_ID_ERR:     LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Advanced Error Reporting -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));               break;
            case VBOX_PCI_EXT_CAP_ID_VC:      LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Virtual Channel -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));                        break;
            case VBOX_PCI_EXT_CAP_ID_DSN:     LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Device Serial Number -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));                   break;
            case VBOX_PCI_EXT_CAP_ID_PWR:     LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Power Budgeting -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));                        break;
            case VBOX_PCI_EXT_CAP_ID_RCLINK:  LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Root Complex Link Declaration -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));          break;
            case VBOX_PCI_EXT_CAP_ID_RCILINK: LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Coot Complex Internal Link Declaration -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers)); break;
            case VBOX_PCI_EXT_CAP_ID_RCECOLL: LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Root Complex Event Collector -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));           break;
            case VBOX_PCI_EXT_CAP_ID_MFVC:    LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Multi-Function Virtual Channel -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));         break;
            case VBOX_PCI_EXT_CAP_ID_RBCB:    LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Root Bridge Control Block -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));              break;
            case VBOX_PCI_EXT_CAP_ID_VNDR:    LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Vendor Specific -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));                        break;
            case VBOX_PCI_EXT_CAP_ID_ACS:     LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Access Controls -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));                        break;
            case VBOX_PCI_EXT_CAP_ID_ARI:     LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Alternative Routing ID -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));                 break;
            case VBOX_PCI_EXT_CAP_ID_ATS:     LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Address Translation Service -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));            break;
            case VBOX_PCI_EXT_CAP_ID_SRIOV:   LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Single Root I/O Virtualization -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));         break;
            case VBOX_PCI_EXT_CAP_ID_MCAST:   LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Multicast -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));                              break;
            case VBOX_PCI_EXT_CAP_ID_RESZBAR: LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Resizable BAR -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));                          break;
            case VBOX_PCI_EXT_CAP_ID_DPA:     LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Dynamic Power Allocation -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));               break;
            case VBOX_PCI_EXT_CAP_ID_TPH:     LogRel(("VFIO#%d.%u: Cap[%#x v%u]: TPH Requester -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));                          break;
            case VBOX_PCI_EXT_CAP_ID_LTR:     LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Latency Tolerance Reporting -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));            break;
            case VBOX_PCI_EXT_CAP_ID_SECPCIE: LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Secondary PCI Express -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));                  break;
            case VBOX_PCI_EXT_CAP_ID_PASID:   LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Process Address Space Identifier -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));       break;
            case VBOX_PCI_EXT_CAP_ID_LNR:     LogRel(("VFIO#%d.%u: Cap[%#x v%u]: LN Requester -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));                           break;
            case VBOX_PCI_EXT_CAP_ID_DPC:     LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Downstream Port Containment -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));            break;
            case VBOX_PCI_EXT_CAP_ID_L1PM:    LogRel(("VFIO#%d.%u: Cap[%#x v%u]: L1 PM Substates -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));                        break;
            case VBOX_PCI_EXT_CAP_ID_PTM:     LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Precision Time Management -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));              break;
            case VBOX_PCI_EXT_CAP_ID_MPCIE:   LogRel(("VFIO#%d.%u: Cap[%#x v%u]: M-PCI Express -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));                          break;
            case VBOX_PCI_EXT_CAP_ID_FRS:     LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Function Readiness Status -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));              break;
            case VBOX_PCI_EXT_CAP_ID_RTR:     LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Readiness Time Reporting -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));               break;
            case VBOX_PCI_EXT_CAP_ID_DVSEC:   LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Desginated Vendor-Specific -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers));             break;
            default:
                LogRel(("VFIO#%d.%u: Cap[%#x v%u]: Unknown capability ID %#x encountered -> unsupported\n", pThis->iInstance, pFun->uPciFun, offCap, bCapVers, u16CapId));
                break;
        }

        if (fSupported)
        {
            /** @todo */
        }

        if (!offCapNext)
            break;

        if (   offCapNext < offCap
            || offCapNext < offCap + cbCap)
            return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_STATE, RT_SRC_POS,
                                       N_("Next capability pointer points backwards or inside the current capability (next offset %#x )"), offCapNext);

        offCap = offCapNext;
    }

#if 1 /** @todo Proper support. */
    for (uint32_t i = 256; i < 4096; i += 4)
        pciVfioCfgSpaceSetInterceptU32(pFun, i, VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH, VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH);
#endif

    return VINF_SUCCESS;
}


/**
 * The descriptors for the standard PCI config space.
 */
static const struct
{
    uint8_t offReg;
    uint8_t cbReg;
    bool    fInitFromDev;
    uint8_t fRd;
    uint8_t fWr;
} s_aCfgSpaceDesc[] =
{
    { VBOX_PCI_VENDOR_ID,           sizeof(uint16_t), true,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_INVALID     },
    { VBOX_PCI_DEVICE_ID,           sizeof(uint16_t), true,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_INVALID     },
    { VBOX_PCI_COMMAND,             sizeof(uint16_t), true,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_EMULATE     },
    { VBOX_PCI_STATUS,              sizeof(uint16_t), true,  VFIO_PCI_CFG_SPACE_ACCESS_EMULATE,     VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH },
    { VBOX_PCI_REVISION_ID,         sizeof(uint8_t),  true,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_INVALID     },
    { VBOX_PCI_CLASS_PROG,          sizeof(uint8_t),  true,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_INVALID     },
    { VBOX_PCI_CLASS_SUB,           sizeof(uint8_t),  true,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_INVALID     },
    { VBOX_PCI_CLASS_BASE,          sizeof(uint8_t),  true,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_INVALID     },
    { VBOX_PCI_CACHE_LINE_SIZE,     sizeof(uint8_t),  false, VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH, VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH },
    { VBOX_PCI_LATENCY_TIMER,       sizeof(uint8_t),  false, VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH, VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH },
    { VBOX_PCI_HEADER_TYPE,         sizeof(uint8_t),  false, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_INVALID     },
    { VBOX_PCI_BIST,                sizeof(uint8_t),  false, VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH, VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH },
    { VBOX_PCI_BASE_ADDRESS_0,      sizeof(uint32_t), false, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT  },
    { VBOX_PCI_BASE_ADDRESS_1,      sizeof(uint32_t), false, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT  },
    { VBOX_PCI_BASE_ADDRESS_2,      sizeof(uint32_t), false, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT  },
    { VBOX_PCI_BASE_ADDRESS_3,      sizeof(uint32_t), false, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT  },
    { VBOX_PCI_BASE_ADDRESS_4,      sizeof(uint32_t), false, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT  },
    { VBOX_PCI_BASE_ADDRESS_5,      sizeof(uint32_t), false, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT  },
    { VBOX_PCI_CARDBUS_CIS,         sizeof(uint32_t), false, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT  },
    { VBOX_PCI_SUBSYSTEM_VENDOR_ID, sizeof(uint16_t), true,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_INVALID     },
    { VBOX_PCI_SUBSYSTEM_ID,        sizeof(uint16_t), true,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_INVALID     },
    { VBOX_PCI_ROM_ADDRESS,         sizeof(uint32_t), false, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT  },
    { VBOX_PCI_INTERRUPT_LINE,      sizeof(uint8_t),  false, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT  },
    { VBOX_PCI_INTERRUPT_PIN,       sizeof(uint8_t),  false, VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_INVALID     },
    { VBOX_PCI_MIN_GNT,             sizeof(uint8_t),  true,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_INVALID     },
    { VBOX_PCI_MAX_LAT,             sizeof(uint8_t),  true,  VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT,  VFIO_PCI_CFG_SPACE_ACCESS_INVALID     },
};

static int pciVfioCfgSpaceSetup(PVFIOPCIFUN pFun, PPDMPCIDEV pPciDev)
{
    for (uint32_t i = 0; i < RT_ELEMENTS(s_aCfgSpaceDesc); i++)
    {
        switch (s_aCfgSpaceDesc[i].cbReg)
        {
            case 1:
            {
                if (s_aCfgSpaceDesc[i].fInitFromDev)
                {
                    uint8_t u8;
                    int rc = pciVfioCfgSpaceReadU8(pFun, s_aCfgSpaceDesc[i].offReg, &u8);
                    if (RT_FAILURE(rc)) return rc;
                    PDMPciDevSetByte(pPciDev, s_aCfgSpaceDesc[i].offReg, u8);
                }
                pciVfioCfgSpaceSetInterceptU8(pFun, s_aCfgSpaceDesc[i].offReg, s_aCfgSpaceDesc[i].fRd, s_aCfgSpaceDesc[i].fWr);
                break;
            }
            case 2:
            {
                if (s_aCfgSpaceDesc[i].fInitFromDev)
                {
                    uint16_t u16;
                    int rc = pciVfioCfgSpaceReadU16(pFun, s_aCfgSpaceDesc[i].offReg, &u16);
                    if (RT_FAILURE(rc)) return rc;
                    PDMPciDevSetWord(pPciDev, s_aCfgSpaceDesc[i].offReg, u16);
                }
                pciVfioCfgSpaceSetInterceptU16(pFun, s_aCfgSpaceDesc[i].offReg, s_aCfgSpaceDesc[i].fRd, s_aCfgSpaceDesc[i].fWr);
                break;
            }
            case 4:
            {
                if (s_aCfgSpaceDesc[i].fInitFromDev)
                {
                    uint32_t u32;
                    int rc = pciVfioCfgSpaceReadU32(pFun, s_aCfgSpaceDesc[i].offReg, &u32);
                    if (RT_FAILURE(rc)) return rc;
                    PDMPciDevSetDWord(pPciDev, s_aCfgSpaceDesc[i].offReg, u32);
                }
                pciVfioCfgSpaceSetInterceptU32(pFun, s_aCfgSpaceDesc[i].offReg, s_aCfgSpaceDesc[i].fRd, s_aCfgSpaceDesc[i].fWr);
                break;
            }
            default:
                AssertReleaseFailed();
        }
    }

    return VINF_SUCCESS;
}


static int pciVfioMapRegion(PVFIOPCI pThis, RTGCPHYS GCPhysStart, uintptr_t uPtrMapping, size_t cbMapping)
{
    if (pThis->fVfioLegacy)
    {
        struct vfio_iommu_type1_dma_map Map;
        Map.argsz      = sizeof(Map);
        Map.flags      = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;
        Map.vaddr      = uPtrMapping;
        Map.size       = cbMapping;
        Map.iova       = GCPhysStart;

        int rcLnx = ioctl(pThis->VfioGroup.iFdVfioContainer, VFIO_IOMMU_MAP_DMA, &Map);
        if (rcLnx == -1)
        {
            LogRel(("errno=%d\n", errno));
            return RTErrConvertFromErrno(errno);
        }
    }
    else
    {
        struct iommu_ioas_map Map;
        Map.size       = sizeof(Map);
        Map.flags      = IOMMU_IOAS_MAP_FIXED_IOVA | IOMMU_IOAS_MAP_WRITEABLE | IOMMU_IOAS_MAP_READABLE;
        Map.ioas_id    = pThis->IommuFd.idIommuHwpt;
        Map.__reserved = 0;
        Map.user_va    = uPtrMapping;
        Map.length     = cbMapping;
        Map.iova       = GCPhysStart;

        int rcLnx = ioctl(pThis->IommuFd.iFdIommu, IOMMU_IOAS_MAP, &Map);
        if (rcLnx == -1)
        {
            LogRel(("errno=%d\n", errno));
            return RTErrConvertFromErrno(errno);
        }
    }

    return VINF_SUCCESS;
}


static int pciVfioIommuGuestRamMap(PVFIOPCI pThis, PPDMDEVINS pDevIns)
{
    if (!pThis->fGuestRamMapped)
    {
        /** @todo This is a really gross hack because we currently lack
         * a dedicated interface to get knowledge about guest RAM mappings.
         * This might also return mappings for stuff not being guest RAM.
         */
        RTGCPHYS  GCPhysStart = 0;
        uintptr_t uPtrMapping = 0;
        size_t    cbMapping   = 0;
        for (RTGCPHYS GCPhys = 0; GCPhys < _4G + PDMDevHlpMMPhysGetRamSizeAbove4GB(pDevIns); GCPhys += _4K)
        {
            void *pv = NULL;
            PGMPAGEMAPLOCK Lock;
            int rc = PDMDevHlpPhysGCPhys2CCPtr(pDevIns, GCPhys, 0 /*fFlags*/, &pv, &Lock);
            if (RT_SUCCESS(rc))
            {
                if (cbMapping)
                {
                    if (uPtrMapping + cbMapping == (uintptr_t)pv)
                        cbMapping += _4K;
                    else
                    {
                        rc = pciVfioMapRegion(pThis, GCPhysStart, uPtrMapping, cbMapping);
                        if (RT_FAILURE(rc))
                            LogRel(("Mapping %RGp/%zu failed with %Rrc\n", GCPhysStart, cbMapping, rc));

                        GCPhysStart = GCPhys;
                        uPtrMapping = (uintptr_t)pv;
                        cbMapping = _4K;
                    }
                }
                else
                {
                    Assert(!uPtrMapping);
                    GCPhysStart = GCPhys;
                    uPtrMapping = (uintptr_t)pv;
                    cbMapping = _4K;
                }
                PDMDevHlpPhysReleasePageMappingLock(pDevIns, &Lock);
            }
            else if (cbMapping)
            {
                LogRel(("PDMDevHlpPhysGCPhys2CCPtr(,%RGp) -> %Rrc\n", GCPhys, rc));
                /* Map what we currently have. */
                Assert(uPtrMapping);
                rc = pciVfioMapRegion(pThis, GCPhysStart, uPtrMapping, cbMapping);
                if (RT_FAILURE(rc))
                    LogRel(("Mapping %RGp/%zu failed with %Rrc\n", GCPhysStart, cbMapping, rc));
                cbMapping   = 0;
                uPtrMapping = 0;
                GCPhysStart = GCPhys;
            }
        }

        if (cbMapping)
        {
            /* Map what we currently have. */
            Assert(uPtrMapping);
            int rc = pciVfioMapRegion(pThis, GCPhysStart, uPtrMapping, cbMapping);
            if (RT_FAILURE(rc))
                LogRel(("Mapping %RGp/%zu failed with %Rrc\n", GCPhysStart, cbMapping, rc));
            cbMapping   = 0;
            uPtrMapping = 0;
        }

        pThis->fGuestRamMapped = true;
    }

    return VINF_SUCCESS;
}


static int pciVfioConfigPassthroughRead(PVFIOPCIFUN pFun, uint32_t uAddress, unsigned cb, uint32_t *pu32Value)
{
    switch (cb)
    {
        case 1:
        {
            uint8_t u8;
            int rc = pciVfioCfgSpaceReadU8(pFun, uAddress, &u8);
            if (RT_FAILURE(rc)) return rc;
            *pu32Value = u8;
            break;
        }
        case 2:
        {
            uint16_t u16;
            int rc = pciVfioCfgSpaceReadU16(pFun, uAddress, &u16);
            if (RT_FAILURE(rc)) return rc;
            *pu32Value = u16;
            break;
        }
        case 4:
        {
            uint32_t u32;
            int rc = pciVfioCfgSpaceReadU32(pFun, uAddress, &u32);
            if (RT_FAILURE(rc)) return rc;
            *pu32Value = u32;
            break;
        }
        default:
            AssertFailedReturn(VERR_INVALID_PARAMETER);
    }

    return VINF_SUCCESS;
}


static int pciVfioConfigPassthroughWrite(PVFIOPCIFUN pFun, uint32_t uAddress, unsigned cb, uint32_t u32Value)
{
    switch (cb)
    {
        case 1:
        {
            int rc = pciVfioCfgSpaceWriteU8(pFun, uAddress, (uint8_t)u32Value);
            if (RT_FAILURE(rc)) return rc;
            break;
        }
        case 2:
        {
            int rc = pciVfioCfgSpaceWriteU16(pFun, uAddress, (uint16_t)u32Value);
            if (RT_FAILURE(rc)) return rc;
            break;
        }
        case 4:
        {
            int rc = pciVfioCfgSpaceWriteU32(pFun, uAddress, (uint32_t)u32Value);
            if (RT_FAILURE(rc)) return rc;
            break;
        }
        default:
            AssertFailedReturn(VERR_INVALID_PARAMETER);
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNPCICONFIGREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) pciVfioConfigRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                    uint32_t uAddress, unsigned cb, uint32_t *pu32Value)
{
    PVFIOPCI pThis = PDMDEVINS_2_DATA(pDevIns, PVFIOPCI);
    PVFIOPCIFUN pFun = &pThis->aPciFuns[pPciDev->idxSubDev];

    uint32_t u32 = 0;
    for (uint8_t i = 0; i < cb; i++)
    {
        uint32_t bRead = 0;
        uint8_t fRd = pciVfioCfgSpaceGetInterceptRd(pFun, uAddress + i);
        switch (fRd)
        {
            case VFIO_PCI_CFG_SPACE_ACCESS_INVALID:
            {
                Log(("VFIO#%d.%u: Invalid PCI config read at offset %#x, returning 0\n", pThis->iInstance, pFun->uPciFun, uAddress + i));
                break;
            }
            case VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH:
            {
                int rc = pciVfioConfigPassthroughRead(pFun, uAddress + i, 1, &bRead);
                if (RT_FAILURE(rc))
                LogRel(("VFIO#%d.%u: Failed to passthrough PCI config read at offset %#x with %Rrc, returning 0\n",
                        pThis->iInstance, pFun->uPciFun, uAddress + i, rc));
                break;
            }
            case VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT:
            {
                bRead = PDMPciDevGetByte(pPciDev, uAddress + i);
                break;
            }
            case VFIO_PCI_CFG_SPACE_ACCESS_EMULATE:
            {
                switch (uAddress + i)
                {
                    case VBOX_PCI_STATUS:
                    case VBOX_PCI_STATUS + 1:
                    {
                        uint16_t u16;
                        int rc = pciVfioCfgSpaceReadU16(pFun, VBOX_PCI_STATUS, &u16);
                        if (RT_SUCCESS(rc))
                        {
                            /* Reflect proper capabilities bit. */
                            uint8_t offCap = PDMPciDevGetCapabilityList(pPciDev);
                            if (offCap)
                                u16 |= RT_BIT(4);
                            else
                                u16 &= ~RT_BIT(4);
                            bRead = (u16 >> ((uAddress + i) == VBOX_PCI_STATUS ? 0 : 8)) & 0xff;
                        }
                        break;
                    }
                    default:
                        AssertReleaseFailed();
                }
                break;
            }
            default:
                AssertReleaseFailed();
        }

        u32 |= (uint32_t)bRead << (i * 8);
    }

    *pu32Value = u32;
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNPCICONFIGWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) pciVfioConfigWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                     uint32_t uAddress, unsigned cb, uint32_t u32Value)
{
    PVFIOPCI pThis = PDMDEVINS_2_DATA(pDevIns, PVFIOPCI);
    PVFIOPCIFUN pFun = &pThis->aPciFuns[pPciDev->idxSubDev];

    /** @todo Only handles accesses with the same intercept config for now. */
    uint8_t fWr = pciVfioCfgSpaceGetInterceptWr(pFun, uAddress);
    for (uint8_t i = 1; i < cb; i++)
    {
        uint8_t fWrU8 = pciVfioCfgSpaceGetInterceptWr(pFun, uAddress + i);
        if (fWr != fWrU8)
        {
            LogRel(("VFIO#%d.%u: Complicated PCI config space write at offset %#x (+ %u) intercept %u vs %u\n",
                    pThis->iInstance, pFun->uPciFun, uAddress, i, fWr, fWrU8));
            return VINF_SUCCESS;
        }
    }

    int rc = VINF_SUCCESS;
    switch (fWr)
    {
        case VFIO_PCI_CFG_SPACE_ACCESS_INVALID:
        {
            LogRel(("VFIO#%d.%u: Invalid PCI config write at offset %#x, ignoring\n",
                    pThis->iInstance, pFun->uPciFun, uAddress));
            break;
        }
        case VFIO_PCI_CFG_SPACE_ACCESS_PASSTHROUGH:
        {
            rc = pciVfioConfigPassthroughWrite(pFun, uAddress, cb, u32Value);
            break;
        }
        case VFIO_PCI_CFG_SPACE_ACCESS_DO_DEFAULT:
            return VINF_PDM_PCI_DO_DEFAULT;
        case VFIO_PCI_CFG_SPACE_ACCESS_EMULATE:
        {
            /* Map all the guest memory into the IOMMU as soon as the BUSMASTER bit is enabled. */
            if (uAddress == VBOX_PCI_COMMAND)
            {
                if (u32Value & RT_BIT(2))
                {
                    rc = pciVfioIommuGuestRamMap(pThis, pDevIns);
                    if (RT_FAILURE(rc))
                        LogRel(("VFIO#%d.%u: Failed to map guest RAM into IOMMU for device, expect broken device (%Rrc)\n",
                                pThis->iInstance, pFun->uPciFun, rc));
                }

                /* Check whether the INTx config changes. */
                bool const fIntxEnabled = !RT_BOOL(u32Value & RT_BIT(10));
                if (fIntxEnabled != (pFun->uIrqModeCur == VFIO_PCI_INTX_IRQ_INDEX))
                {
                    rc = pciVfioIrqReconfigure(pThis, pFun, VFIO_PCI_INTX_IRQ_INDEX, fIntxEnabled ? 1 : 0);
                    if (RT_FAILURE(rc))
                        LogRel(("VFIO#%d.%u: Failed to reconfigure the INTx interrupt config of the device, expect a broken device (%Rrc)\n",
                                pThis->iInstance, pFun->uPciFun, rc));
                }

                /* Now write to the device. */
                rc = pciVfioConfigPassthroughWrite(pFun, uAddress, cb, u32Value);
                if (RT_FAILURE(rc))
                    LogRel(("VFIO#%d.%u: Failed to update command register in device (%Rrc)", pThis->iInstance, pFun->uPciFun, rc));
                rc = VINF_PDM_PCI_DO_DEFAULT; /* Need to update BAR mappings. */
            }
            else if (   pFun->offMsiCtrl == uAddress
                     && cb == sizeof(uint16_t))
            {
                bool const fMsiEnabled = RT_BOOL(u32Value & RT_BIT(0));
                uint8_t const cVectors = fMsiEnabled ? RT_BIT((u32Value >> 4) & 0x7) : 0;
                if (fMsiEnabled != (pFun->uIrqModeCur == VFIO_PCI_MSI_IRQ_INDEX))
                {
                    rc = pciVfioIrqReconfigure(pThis, pFun, VFIO_PCI_MSI_IRQ_INDEX, cVectors);
                    if (RT_FAILURE(rc))
                        LogRel(("VFIO#%d.%u: Failed to reconfigure the MSI interrupt config of the device, expect a broken device (%Rrc)\n",
                                pThis->iInstance, pFun->uPciFun, rc));
                }

                /* Now write to the device. */
                rc = pciVfioConfigPassthroughWrite(pFun, uAddress, cb, u32Value);
                if (RT_FAILURE(rc))
                    LogRel(("VFIO#%d.%u: Failed to update MSI control register in device (%Rrc)", pThis->iInstance, pFun->uPciFun, rc));
                rc = VINF_PDM_PCI_DO_DEFAULT; /* Need to update our internal MSI state. */
            }
            else if (   pFun->offMsixCtrl == uAddress
                     && cb == sizeof(uint16_t))
            {
                bool const fMsixEnabled = RT_BOOL(u32Value & RT_BIT(15));
                if (fMsixEnabled != (pFun->uIrqModeCur == VFIO_PCI_MSIX_IRQ_INDEX))
                {
                    rc = pciVfioIrqReconfigure(pThis, pFun, VFIO_PCI_MSIX_IRQ_INDEX, fMsixEnabled ? pFun->acIrqVectors[VFIO_PCI_MSIX_IRQ_INDEX] : 0);
                    if (RT_FAILURE(rc))
                        LogRel(("VFIO#%d.%u: Failed to reconfigure the MSI-X interrupt config of the device, expect a broken device (%Rrc)\n",
                                pThis->iInstance, pFun->uPciFun, rc));
                }

                /* Now write to the device. */
                rc = pciVfioConfigPassthroughWrite(pFun, uAddress, cb, u32Value);
                if (RT_FAILURE(rc))
                    LogRel(("VFIO#%d.%u: Failed to update MSI-X control register in device (%Rrc)", pThis->iInstance, pFun->uPciFun, rc));
                PDMPciDevSetWord(pPciDev, uAddress, (uint16_t)u32Value);
                rc = VINF_SUCCESS;
            }
            else
                AssertFailed();
            break;
        }
        default:
            AssertReleaseFailed();
    }

    return rc;
}


static int pciVfioConfigureAccess(PPDMDEVINS pDevIns, PVFIOPCI pThis, PCPDMDEVHLPR3 pHlp, PCFGMNODE pCfg)
{
    /* Query configuration, configuration using the IOMMUFD takes precedence over the legacy VFIO group style. */
    char szPath[RTPATH_MAX];
    int rc = pHlp->pfnCFGMQueryStringDef(pCfg, "IommuPath", &szPath[0], sizeof(szPath), "/dev/iommu");
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Configuration error: Querying \"IommuPath\" failed"));

    pThis->IommuFd.iFdIommu = open(szPath, O_RDWR);
    if (pThis->IommuFd.iFdIommu == -1)
    {
        pThis->fVfioLegacy = true;
        pThis->VfioGroup.iFdVfioGroup     = -1;
        pThis->VfioGroup.iFdVfioContainer = -1;

        rc = pHlp->pfnCFGMQueryStringDef(pCfg, "VfioPath", &szPath[0], sizeof(szPath), "/dev/vfio/vfio");
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Configuration error: Querying \"VfioPath\" failed, no access method configured"));

        pThis->VfioGroup.iFdVfioContainer = open(szPath, O_RDWR);
        if (pThis->VfioGroup.iFdVfioContainer == -1)
            return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("Opening VFIO container path \"%s\" failed with %d"), szPath, errno);

        int rcLnx = ioctl(pThis->VfioGroup.iFdVfioContainer, VFIO_GET_API_VERSION);
        if (rcLnx == -1)
            return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("Querying VFIO API version failed"));
        if (rcLnx != VFIO_API_VERSION)
            return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("Returned VFIO API version %d doesn't match expected version %d\n"),
                                       rcLnx, VFIO_API_VERSION);

        rcLnx = ioctl(pThis->VfioGroup.iFdVfioContainer, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU);
        if (rcLnx == 0)
            return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("VFIO_TYPE1_IOMMU extension not supported"));

        /* Open the group. */
        rc = pHlp->pfnCFGMQueryString(pCfg, "VfioGroupPath", &szPath[0], sizeof(szPath));
        if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        {
            /* Try to determine the IOMMU group from the first device being used. */
            PCFGMNODE pCfgFun = pHlp->pfnCFGMGetChildF(pCfg, "Fun%u", 0);
            if (!pCfgFun)
                return PDMDevHlpVMSetError(pDevIns, VERR_NOT_FOUND, RT_SRC_POS,
                                           N_("Configuration error: The device configuration misses function 0"));

            char szHostAddress[32]; /* Format is xxxx:xx:xx.x */
            rc = pHlp->pfnCFGMQueryString(pCfg, "HostAddress", &szHostAddress[0], sizeof(szHostAddress));
            if (RT_FAILURE(rc))
                return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                           N_("Configuration error: Querying \"HostAddress\" failed, no access method configured"));

            char szIommuGroup[8];
            szIommuGroup[0] = '\0';
            size_t cchIommuGroup = 0;
            rc = RTLinuxSysFsGetLinkDest(&szIommuGroup[0], sizeof(szIommuGroup), &cchIommuGroup, "/sys/bus/pci/devices/%s/iommu_group", szHostAddress);
            if (RT_FAILURE(rc))
                return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                           N_("Configuration error: Resolving the IOMMU group of the PCI device failed"));

            rc = RTPathJoin(szPath, sizeof(szPath), "/dev/vfio", szIommuGroup);
            AssertRC(rc);
        }
        else  if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Configuration error: Querying \"VfioGroupPath\" failed"));

        pThis->VfioGroup.iFdVfioGroup = open(szPath, O_RDWR);
        if (pThis->VfioGroup.iFdVfioGroup == -1)
            return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("Opening VFIO group path \"%s\" failed with %d"), szPath, errno);

        /* Check whether the group is viable. */
        struct vfio_group_status GroupStatus;
        GroupStatus.argsz = sizeof(vfio_group_status);
        GroupStatus.flags = 0;
        rcLnx = ioctl(pThis->VfioGroup.iFdVfioGroup, VFIO_GROUP_GET_STATUS, &GroupStatus);
        if (rcLnx == -1)
            return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("Querying VFIO status failed"));

        if (!(GroupStatus.flags & VFIO_GROUP_FLAGS_VIABLE))
            return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("The VFIO group is not usable for PCI passthrough (all devices in the group bound to VFIO?)"));

        rcLnx = ioctl(pThis->VfioGroup.iFdVfioGroup, VFIO_GROUP_SET_CONTAINER, &pThis->VfioGroup.iFdVfioContainer);
        if (rcLnx == -1)
            return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("Attaching VFIO group to container failed"));

        rcLnx = ioctl(pThis->VfioGroup.iFdVfioContainer, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
        if (rcLnx == -1)
            return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("Enabling IOMMU for container failed"));
    }
    else
    {
        /* Allocate a new I/O address space on the IOMMU. */
        struct iommu_ioas_alloc IoasAlloc;
        IoasAlloc.size  = sizeof(IoasAlloc);
        IoasAlloc.flags = 0;
        int rcLnx = ioctl(pThis->IommuFd.iFdIommu, IOMMU_IOAS_ALLOC, &IoasAlloc);
        if (rcLnx == -1)
            return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("Allocating I/O address space for VFIO device failed with %d"), errno);

        pThis->IommuFd.idIommuHwpt = IoasAlloc.out_ioas_id;
    }

    return VINF_SUCCESS;
}


static int pciVfioFunConfigureAccess(PPDMDEVINS pDevIns, PVFIOPCI pThis, PVFIOPCIFUN pFun, PCPDMDEVHLPR3 pHlp, PCFGMNODE pCfg)
{
    bool fHostAddress = false;
    char szPath[RTPATH_MAX];
    int rc = pHlp->pfnCFGMQueryString(pCfg, "VfioPath", &szPath[0], sizeof(szPath));
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
    {
        rc = pHlp->pfnCFGMQueryString(pCfg, "HostAddress", &szPath[0], sizeof(szPath));
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Configuration error: Querying \"HostAddress\" failed, no access method configured"));
        fHostAddress = true;
    }
    else if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Configuration error: Querying \"VfioPath\" failed"));

    if (pThis->fVfioLegacy)
    {
        pFun->iFdVfio = ioctl(pThis->VfioGroup.iFdVfioGroup, VFIO_GROUP_GET_DEVICE_FD, &szPath[0]);
        if (pFun->iFdVfio == -1)
            return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("Failed to get device for VFIO device \"%s\" -> %d"), szPath, errno);
    }
    else
    {
        if (fHostAddress)
        {
            /* Try determining the VFIO device from sysfs. */
            char szSysfsPath[128];
            rc = RTPathJoin(szSysfsPath, sizeof(szSysfsPath), "/sys/bus/pci/devices", szPath);
            AssertRC(rc);
            rc = RTPathAppend(szSysfsPath, sizeof(szSysfsPath), "vfio-dev");
            AssertRC(rc);

            RTDIR hDir = NIL_RTDIR;
            rc = RTDirOpen(&hDir, szSysfsPath);
            if (RT_SUCCESS(rc))
            {
                for (;;)
                {
                    RTDIRENTRY Entry; /* The default name size is more than enough for the format xxxx:xx:xx.x */
                    rc = RTDirRead(hDir, &Entry, NULL /*pcbDirEntry*/);
                    if (RT_FAILURE(rc))
                        break;

                    if (RTDirEntryIsStdDotLink(&Entry))
                        continue;

                    if (!strncmp(Entry.szName, RT_STR_TUPLE("vfio")))
                    {
                        rc = RTPathJoin(szPath, sizeof(szPath), "/dev/vfio/devices", Entry.szName);
                        AssertRC(rc);
                        break;
                    }
                }

                int rc2 = RTDirClose(hDir);
                AssertRC(rc2); RT_NOREF(rc2);

                if (RT_FAILURE(rc))
                    return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                               N_("Configuration error: Resolving the VFIO device failed"));
            }
        }

        pFun->iFdVfio = open(szPath, O_RDWR);
        if (pFun->iFdVfio == -1)
            return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("Opening VFIO device \"%s\" failed with %d"), szPath, errno);

        /* Bind the IOMMU to the device. */
        struct vfio_device_bind_iommufd VfioBind; RT_ZERO(VfioBind);
        VfioBind.argsz   = sizeof(VfioBind);
        VfioBind.flags   = 0;
        VfioBind.iommufd = pThis->IommuFd.iFdIommu;
        int rcLnx = ioctl(pFun->iFdVfio, VFIO_DEVICE_BIND_IOMMUFD, &VfioBind);
        if (rcLnx == -1)
            return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("Binding IOMMU device to opened VFIO device failed with %d"), errno);

        /* And bind it to the device. */
        struct vfio_device_attach_iommufd_pt VfioAttachIommu;
        VfioAttachIommu.argsz = sizeof(VfioAttachIommu);
        VfioAttachIommu.flags = 0;
        VfioAttachIommu.pt_id = pThis->IommuFd.idIommuHwpt;
        rcLnx = ioctl(pFun->iFdVfio, VFIO_DEVICE_ATTACH_IOMMUFD_PT, &VfioAttachIommu);
        if (rcLnx == -1)
            return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("Attaching IO address space to opened VFIO device failed with %d"), errno);
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) pciVfioReset(PPDMDEVINS pDevIns)
{
    PVFIOPCI pThis = PDMDEVINS_2_DATA(pDevIns, PVFIOPCI);

    for (uint8_t iPciFun = 0; iPciFun < RT_ELEMENTS(pThis->aPciFuns); iPciFun++)
    {
        PVFIOPCIFUN pFun = &pThis->aPciFuns[iPciFun];

        if (pFun->iFdVfio != -1)
        {
            int rcLnx = ioctl(pFun->iFdVfio, VFIO_DEVICE_RESET, NULL);
            AssertLogRelMsg(!rcLnx, ("VFIO#%d.%u: Failed to reset device %d\n", pThis->iInstance, pFun->uPciFun, errno));
        }
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) pciVfioDestruct(PPDMDEVINS pDevIns)
{
    PVFIOPCI pThis = PDMDEVINS_2_DATA(pDevIns, PVFIOPCI);

    for (uint8_t iPciFun = 0; iPciFun < RT_ELEMENTS(pThis->aPciFuns); iPciFun++)
    {
        PVFIOPCIFUN pFun = &pThis->aPciFuns[iPciFun];

        /* Detach address space. */
        if (pFun->iFdVfio != -1)
        {
            struct vfio_device_detach_iommufd_pt VfioDetach;
            VfioDetach.argsz   = sizeof(VfioDetach);
            VfioDetach.flags   = 0;
            int rcLnx = ioctl(pFun->iFdVfio, VFIO_DEVICE_DETACH_IOMMUFD_PT, &VfioDetach);
            AssertLogRelMsg(!rcLnx, ("VFIO#%d.%u: Failed to detach IOMMU page table with %d\n", pThis->iInstance, pFun->uPciFun, errno));

            close(pFun->iFdVfio);
        }

        for (uint32_t i = 0; i < RT_ELEMENTS(pFun->afdEvts); i++)
            if (pFun->afdEvts[i] != 0)
                close(pFun->afdEvts[i]);

        if (pFun->iFdWakeup != -1)
            close(pFun->iFdWakeup);
    }

    if (pThis->fVfioLegacy)
    {
        if (pThis->VfioGroup.iFdVfioContainer != -1)
            close(pThis->VfioGroup.iFdVfioContainer);
        if (pThis->VfioGroup.iFdVfioGroup != -1)
            close(pThis->VfioGroup.iFdVfioGroup);
    }
    else
    {
        if (pThis->IommuFd.iFdIommu != -1)
        {
            struct iommu_destroy HwptDestroy;
            HwptDestroy.size = sizeof(HwptDestroy);
            HwptDestroy.id   = pThis->IommuFd.idIommuHwpt;
            int rcLnx = ioctl(pThis->IommuFd.iFdIommu, IOMMU_DESTROY, &HwptDestroy);
            AssertLogRelMsg(!rcLnx, ("VFIO#%d: Failed to destroy I/O address space with %d\n", pThis->iInstance, errno));

            close(pThis->IommuFd.iFdIommu);
        }
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) pciVfioConstruct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);

    PVFIOPCI      pThis = PDMDEVINS_2_DATA(pDevIns, PVFIOPCI);
    PCPDMDEVHLPR3 pHlp  = pDevIns->pHlpR3;

    pThis->pDevIns          = pDevIns;
    pThis->iInstance        = iInstance;
    pThis->IommuFd.iFdIommu = -1;
    pThis->fVfioLegacy      = false;

    for (uint8_t i = 0; i < RT_ELEMENTS(pThis->aPciFuns); i++)
    {
        PVFIOPCIFUN pFun = &pThis->aPciFuns[i];

        pFun->uPciFun        = UINT32_MAX;
        pFun->iFdVfio        = -1;
        pFun->iFdWakeup      = -1;
        pFun->fVga           = false;
        pFun->fInterceptMmio = false;
        pFun->iBarMsix       = UINT8_MAX;
        pFun->offMsiCtrl     = 0;
        pFun->uIrqModeCur    = UINT8_MAX;
        pFun->uIrqModeNew    = UINT8_MAX;
    }

    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Validate configuration.
     */
    if (!pHlp->pfnCFGMAreValuesValid(pCfg,
                                     "IommuPath\0"
                                     "VfioPath\0"
                                     "VfioGroupPath\0"
                                     "Fun*\0"
                                    ))
        return VERR_PDM_DEVINS_UNKNOWN_CFG_VALUES;

    rc = pciVfioConfigureAccess(pDevIns, pThis, pHlp, pCfg);
    if (RT_FAILURE(rc))
        return rc;

    /* Initialize available functions. */
    uint32_t iPciDevNo = PDMPCIDEVREG_DEV_NO_FIRST_UNUSED;
    bool fMultiFn = false;
    for (uint8_t iPciFun = 0; iPciFun < RT_ELEMENTS(pThis->aPciFuns); iPciFun++)
    {
        PVFIOPCIFUN pFun    = &pThis->aPciFuns[iPciFun];
        PPDMPCIDEV  pPciDev = pDevIns->apPciDevs[iPciFun];
        PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

        /* Query per port configuration options if available. */
        PCFGMNODE pCfgFun = pHlp->pfnCFGMGetChildF(pCfg, "Fun%u", iPciFun);
        if (!pCfgFun)
            continue;

        if (iPciFun > 0)
            fMultiFn = true;

        pFun->uPciFun = iPciFun;
        RTStrPrintf(pFun->szName, sizeof(pFun->szName), "VFIO#%u.%u", pThis->iInstance, iPciFun);

        /*
         * Validate configuration.
         */
        if (!pHlp->pfnCFGMAreValuesValid(pCfgFun,
                                         "VfioPath\0"
                                         "ExposeVga\0"
                                         "InterceptMmio\0"
                                         "HostAddress\0"
                                        ))
            return VERR_PDM_DEVINS_UNKNOWN_CFG_VALUES;

        rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "InterceptMmio", &pFun->fInterceptMmio, false);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Configuration error: Querying \"InterceptMmio\" failed"));

        bool fVga = false;
        rc = pHlp->pfnCFGMQueryBoolDef(pCfgFun, "ExposeVga", &fVga, false);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Configuration error: Querying \"ExposeVga\" failed"));

        rc = pciVfioFunConfigureAccess(pDevIns, pThis, pFun, pHlp, pCfgFun);
        if (RT_FAILURE(rc))
            return rc;

        struct vfio_device_info DevInfo;
        DevInfo.argsz = sizeof(DevInfo);
        int rcLnx = ioctl(pFun->iFdVfio, VFIO_DEVICE_GET_INFO, &DevInfo);
        if (rcLnx == -1)
            return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("Getting device information of opened VFIO device failed with %d"), errno);

        LogRel(("VFIO#%d.%u: Info flags:       %#RX32\n"
                "VFIO#%d.%u: Info num_regions: %#RU32\n"
                "VFIO#%d.%u: Info num_irqs:    %#RU32\n"
                "VFIO#%d.%u: Info cap_offset:  %#RX32\n",
                iInstance, iPciFun, DevInfo.flags,
                iInstance, iPciFun, DevInfo.num_regions,
                iInstance, iPciFun, DevInfo.num_irqs,
                iInstance, iPciFun, DevInfo.cap_offset));

        if (!(DevInfo.flags & VFIO_DEVICE_FLAGS_PCI))
            return PDMDevHlpVMSetError(pDevIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("VFIO device is not a PCI/PCI Express device. This configuration is not supported"));

        /* Setup access to the PCI config space first. */
        struct vfio_region_info RegionInfo;
        rc = pciVfioQueryRegionInfo(pThis, pFun, VFIO_PCI_CONFIG_REGION_INDEX, &RegionInfo);
        if (RT_FAILURE(rc))
            return rc;

        if (   (RegionInfo.flags & (VFIO_REGION_INFO_FLAG_READ | VFIO_REGION_INFO_FLAG_WRITE))
            != (VFIO_REGION_INFO_FLAG_READ | VFIO_REGION_INFO_FLAG_WRITE))
            return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_STATE, RT_SRC_POS,
                                       N_("The PCI config region is not marked as read/write as expected"));

        pFun->offPciCfg = RegionInfo.offset;
        pFun->cbPciCfg  = RegionInfo.size;

        /* Setup the PCI config space. */
        rc = pciVfioCfgSpaceSetup(pFun, pPciDev);
        if (RT_FAILURE(rc))
            return rc;

        LogRel(("VFIO#%d.%u: Attached PCI device %04x:%04x\n", iInstance, iPciFun,
                PDMPciDevGetVendorId(pPciDev), PDMPciDevGetDeviceId(pPciDev)));

        /* Attach the device. */
        rc = PDMDevHlpPCIRegisterEx(pDevIns, pPciDev, 0 /*fFlags*/, iPciDevNo, iPciFun, pFun->szName);
        if (RT_FAILURE(rc))
            return rc;

        rc = PDMDevHlpPCIInterceptConfigAccesses(pDevIns, pPciDev, pciVfioConfigRead, pciVfioConfigWrite);
        if (RT_FAILURE(rc))
            return rc;

        /* Set up BAR0 through BAR5. */
        static uint32_t s_aVfioPciRegions[] =
        {
            VFIO_PCI_BAR0_REGION_INDEX,
            VFIO_PCI_BAR1_REGION_INDEX,
            VFIO_PCI_BAR2_REGION_INDEX,
            VFIO_PCI_BAR3_REGION_INDEX,
            VFIO_PCI_BAR4_REGION_INDEX,
            VFIO_PCI_BAR5_REGION_INDEX
        };

        for (uint32_t i = 0; i < RT_ELEMENTS(s_aVfioPciRegions); i++)
        {
            rc = pciVfioSetupBar(pThis, pFun, pDevIns, pPciDev, i, s_aVfioPciRegions[i]);
            if (RT_FAILURE(rc))
                return rc;
        }

        uint8_t bClassBase = PDMPciDevGetByte(pPciDev, VBOX_PCI_CLASS_BASE);
        uint8_t bClassSub  = PDMPciDevGetByte(pPciDev, VBOX_PCI_CLASS_SUB);
        if (   fVga
            && bClassBase == VBOX_PCI_CLASS_DISPLAY
            && bClassSub  == VBOX_PCI_SUB_DISPLAY_VGA)
        {
            rc = pciVfioSetupVga(pThis, pFun, pDevIns);
            if (RT_FAILURE(rc))
                return rc;
        }
        else
            pFun->fVga = false;

        rc = pciVfioSetupRom(pThis, pFun, pPciDev, pDevIns);
        if (RT_FAILURE(rc))
            return rc;

        /* Query the supported interrupt types now. */
        static uint32_t s_aVfioPciIrqs[] =
        {
            VFIO_PCI_INTX_IRQ_INDEX,
            VFIO_PCI_MSI_IRQ_INDEX,
            VFIO_PCI_MSIX_IRQ_INDEX
        };

        for (uint32_t i = 0; i < RT_ELEMENTS(s_aVfioPciIrqs); i++)
        {
            uint32_t const uIrq = s_aVfioPciIrqs[i];
            rc = pciVfioQueryIrqInfo(pThis, pFun, uIrq, &pFun->acIrqVectors[i]);
            if (RT_FAILURE(rc))
                return rc;
        }

        /*
         * Pre create the event file descriptors to not risk running out of file descriptors
         * while the VM is running.
         */
        uint32_t cMaxVectors = RT_MAX(RT_MAX(pFun->acIrqVectors[VFIO_PCI_MSI_IRQ_INDEX],
                                             pFun->acIrqVectors[VFIO_PCI_MSIX_IRQ_INDEX]),
                                      pFun->acIrqVectors[VFIO_PCI_INTX_IRQ_INDEX]);

        for (uint32_t i = 0; i < cMaxVectors; i++)
        {
            rc = pciVfioLnxEventfd2(0 /*uValInit*/, 0 /*fFlags*/, &pFun->afdEvts[i]);
            if (RT_FAILURE(rc))
                return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                           N_("Failed to create event file descriptor %u out of %u for interrupts"),
                                           i, cMaxVectors);
        }

        /* Parse the capability lists now. */
        rc = pciVfioCfgSpaceParseCapabilities(pThis, pFun, pPciDev);
        if (RT_FAILURE(rc))
            return rc;

        if (pFun->cbPciCfg == 4096)
        {
            rc = pciVfioCfgSpaceParseExtCapabilities(pThis, pFun, pPciDev);
            if (RT_FAILURE(rc))
                return rc;
        }

        /* Create the MMIO region mappings now. */
        for (uint8_t i = 0; i < RT_ELEMENTS(pFun->aBars); i++)
        {
            /* PIO regions were created before. */
            if (pFun->aBars[i].bType == 2)
            {
                uint64_t const cbRegion = pFun->aBars[i].cbRegion +
                                          (  i == pFun->iBarMsix
                                           ? pFun->cbMsix
                                           : 0);

                rc = PDMDevHlpPCIIORegionRegisterMmioCustom(pDevIns, i, cbRegion, pFun->aBars[i].enmAddrSpace, pciVfioMmioMapUnmap);
                AssertLogRelRCReturn(rc, rc);

                if (pFun->aBars[i].fInterceptMmio)
                {
                    rc = PDMDevHlpMmioCreate(pDevIns, pFun->aBars[i].cbRegion, pPciDev, i /*iPciRegion*/,
                                             pciVfioMmioWrite, pciVfioMmioRead, &pFun->aBars[i],
                                             IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU, "MMIO",
                                             &pFun->aBars[i].hnd.hMmio);
                    AssertLogRelRCReturn(rc, rc);

                    if (i == pFun->iBarMsix)
                    {
                        rc = PDMDevHlpMmioCreate(pDevIns, pFun->cbMsix, pPciDev, i /*iPciRegion*/,
                                                 pciVfioMsixMmioWrite, pciVfioMsixMmioRead, pFun,
                                                 IOMMMIO_FLAGS_READ_DWORD_QWORD | IOMMMIO_FLAGS_WRITE_DWORD_QWORD_READ_MISSING,
                                                 "MSI-X MMIO", &pFun->hMsixMmio);
                        AssertLogRelRCReturn(rc, rc);
                    }
                }
                else
                {
                    rc = PDMDevHlpMmio2CreateFromExisting(pDevIns, pPciDev, i, pFun->aBars[i].cbRegion,
                                                          0 /*fFlags*/, "MMIO", (void *)pFun->aBars[i].u.pvMmio,
                                                          &pFun->aBars[i].hnd.hMmio2);
                    AssertLogRelRCReturn(rc, rc);
                }
            }
        }

        /* Create wakeup eventfd for IRQ poller. */
        rc = pciVfioLnxEventfd2(0 /*uValInit*/, 0 /*fFlags*/, &pFun->iFdWakeup);
        if (RT_FAILURE(rc))
            return rc;

        /* Spin up the interrupt poller. */
        char szDev[64];
        RT_ZERO(szDev);
        RTStrPrintf(szDev, sizeof(szDev), "VFIO#%u.%u", iInstance, iPciFun);
        rc = PDMDevHlpThreadCreate(pDevIns, &pFun->pThrdIrq, pFun, pciVfioIrqPoller, pciVfioIrqPollerWakeup,
                                   0 /* cbStack */, RTTHREADTYPE_IO, szDev);
        AssertLogRelRCReturn(rc, rc);

        /* SR-IOV devices don't offer INTx. */
        if (pFun->acIrqVectors[VFIO_PCI_INTX_IRQ_INDEX])
        {
            PDMPciDevSetInterruptPin(pPciDev, 0x01); /* A */

            rc = pciVfioIrqReconfigure(pThis, pFun, VFIO_PCI_INTX_IRQ_INDEX, pFun->acIrqVectors[VFIO_PCI_INTX_IRQ_INDEX]);
            if (RT_FAILURE(rc))
                return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                           N_("Failed to set initial interrupt mode to INTx"));
        }
        else
            PDMPciDevSetInterruptPin(pPciDev, 0);

        /* Subsequent function should use the same major as the previous one. */
        iPciDevNo = PDMPCIDEVREG_DEV_NO_SAME_AS_PREV;
    }

    /* Need to fixup the header type for all functions. */
    if (fMultiFn)
    {
        for (uint8_t iPciFun = 0; iPciFun < RT_ELEMENTS(pThis->aPciFuns); iPciFun++)
        {
            PPDMPCIDEV  pPciDev = pDevIns->apPciDevs[iPciFun];

            uint8_t const u8HdrType = PDMPciDevGetHeaderType(pPciDev);
            PDMPciDevSetHeaderType(pPciDev, u8HdrType | RT_BIT(7));
        }
    }

    return rc;
}


/**
 * The device registration structure.
 */
const PDMDEVREG g_DevicePciVfio =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "pci-vfio",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_HOST_DEV,
    /* .cMaxInstances = */          ~0U,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(VFIOPCI),
    /* .cbInstanceCC = */           0,
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         8,
    /* .cMaxMsixVectors = */        VBOX_MSIX_MAX_ENTRIES,
    /* .pszDescription = */         "PCI passthrough through VFIO",
#if defined(IN_RING3)
    /* .pszRCMod = */               "",
    /* .pszR0Mod = */               "",
    /* .pfnConstruct = */           pciVfioConstruct,
    /* .pfnDestruct = */            pciVfioDestruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               pciVfioReset,
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
    /* .pfnConstruct = */           NULL,
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
    /* .pfnConstruct = */           NULL,
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

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

