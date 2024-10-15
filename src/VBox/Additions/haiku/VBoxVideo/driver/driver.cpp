/* $Id: driver.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVideo driver, Haiku Guest Additions, implementation.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

/*
 * This code is based on:
 *
 * VirtualBox Guest Additions for Haiku.
 * Copyright (c) 2011 Mike Smith <mike@scgtrp.net>
 *                    François Revol <revol@free.fr>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <KernelExport.h>
#include <PCI.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <graphic_driver.h>
#include <VBoxGuest-haiku.h>
#include <VBoxVideoGuest.h>
#include "../common/VBoxVideo_common.h"

#define VENDOR_ID 0x80ee
#define DEVICE_ID 0xbeef
#define DRIVER_NAME                 "VBoxVideoDriver"
#define DEVICE_FORMAT               "vd_%04X_%04X_%02X%02X%02X"

/** @todo r=ramshankar: pretty sure IPRT has something for page rounding,
 *        replace with IPRT version later. */
#define ROUND_TO_PAGE_SIZE(x) (((x) + (B_PAGE_SIZE) - 1) & ~((B_PAGE_SIZE) - 1))

#define ENABLE_DEBUG_TRACE

#undef TRACE
#ifdef ENABLE_DEBUG_TRACE
#define TRACE(x...) dprintf("VBoxVideo: " x)
#else
#define TRACE(x...) ;
#endif

int32 api_version = B_CUR_DRIVER_API_VERSION; // revision of driver API we support

extern "C" status_t vm_set_area_memory_type(area_id id, phys_addr_t physicalBase, uint32 type);

struct Benaphore
{
    sem_id    sem;
    int32    count;

    status_t Init(const char *name)
    {
        count = 0;
        sem = create_sem(0, name);
        return sem < 0 ? sem : B_OK;
    }

    status_t Acquire()
    {
        if (atomic_add(&count, 1) > 0)
            return acquire_sem(sem);
        return B_OK;
    }

    status_t Release()
    {
        if (atomic_add(&count, -1) > 1)
            return release_sem(sem);
        return B_OK;
    }

    void Delete()
    {
        delete_sem(sem);
    }
};

struct DeviceInfo
{
    uint32          openCount;              /* Count of how many times device has been opened */
    uint32          flags;                  /* Device flags */
    area_id         sharedArea;             /* Area shared between driver and all accelerants */
    SharedInfo     *sharedInfo;             /* Pointer to shared info area memory */
    pci_info        pciInfo;                /* Copy of pci info for this device */
    char            name[B_OS_NAME_LENGTH]; /* Name of device */
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
status_t device_open(const char *name, uint32 flags, void **cookie);
status_t device_close(void *dev);
status_t device_free(void *dev);
status_t device_read(void *dev, off_t pos, void *buf, size_t *len);
status_t device_write(void *dev, off_t pos, const void *buf, size_t *len);
status_t device_ioctl(void *dev, uint32 msg, void *buf, size_t len);
static uint32 get_color_space_for_depth(uint32 depth);


/*********************************************************************************************************************************
*   Globals                                                                                                                      *
*********************************************************************************************************************************/
/* At most one virtual video card ever appears, no reason for this to be an array */
static DeviceInfo gDeviceInfo;
static char *gDeviceNames[2] = { gDeviceInfo.name, NULL };
static bool gCanHasDevice = false; /* is the device present? */
static Benaphore gLock;
static pci_module_info *gPCI;

static device_hooks gDeviceHooks =
{
    device_open,
    device_close,
    device_free,
    device_ioctl,
    device_read,
    device_write,
    NULL,          /* select */
    NULL,          /* deselect */
    NULL,          /* read_pages */
    NULL           /* write_pages */
};


status_t init_hardware()
{
    LogFlowFunc(("init_hardware\n"));

    status_t err = get_module(VBOXGUEST_MODULE_NAME, (module_info **)&g_VBoxGuest);
    if (err == B_OK)
    {
        err = get_module(B_PCI_MODULE_NAME, (module_info **)&gPCI);
        if (err == B_OK)
            return B_OK;

        LogRel((DRIVER_NAME ":_init_hardware() get_module(%s) failed. err=%08lx\n", B_PCI_MODULE_NAME));
    }
    else
        LogRel((DRIVER_NAME ":_init_hardware() get_module(%s) failed. err=%08lx\n", VBOXGUEST_MODULE_NAME, err));
    return B_ERROR;
}


status_t init_driver()
{
    LogFlowFunc(("init_driver\n"));

    gLock.Init("VBoxVideo driver lock");

    uint32 pciIndex = 0;

    while (gPCI->get_nth_pci_info(pciIndex, &gDeviceInfo.pciInfo) == B_OK)
    {
        if (gDeviceInfo.pciInfo.vendor_id == VENDOR_ID && gDeviceInfo.pciInfo.device_id == DEVICE_ID)
        {
            sprintf(gDeviceInfo.name, "graphics/" DEVICE_FORMAT,
                    gDeviceInfo.pciInfo.vendor_id, gDeviceInfo.pciInfo.device_id,
                    gDeviceInfo.pciInfo.bus, gDeviceInfo.pciInfo.device, gDeviceInfo.pciInfo.function);
            TRACE("found device %s\n", gDeviceInfo.name);

            gCanHasDevice = true;
            gDeviceInfo.openCount = 0;

            size_t sharedSize = (sizeof(SharedInfo) + 7) & ~7;
            gDeviceInfo.sharedArea = create_area("vboxvideo shared info",
                                                 (void **)&gDeviceInfo.sharedInfo, B_ANY_KERNEL_ADDRESS,
                                                 ROUND_TO_PAGE_SIZE(sharedSize), B_FULL_LOCK,
                                                 B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_USER_CLONEABLE_AREA);

            uint16_t width, height, vwidth, bpp, flags;
            VBoxVideoGetModeRegisters(&width, &height, &vwidth, &bpp, &flags);

            gDeviceInfo.sharedInfo->currentMode.space = get_color_space_for_depth(bpp);
            gDeviceInfo.sharedInfo->currentMode.virtual_width = width;
            gDeviceInfo.sharedInfo->currentMode.virtual_height = height;
            gDeviceInfo.sharedInfo->currentMode.h_display_start = 0;
            gDeviceInfo.sharedInfo->currentMode.v_display_start = 0;
            gDeviceInfo.sharedInfo->currentMode.flags = 0;
            gDeviceInfo.sharedInfo->currentMode.timing.h_display = width;
            gDeviceInfo.sharedInfo->currentMode.timing.v_display = height;
            /* Not used, but this makes a reasonable-sounding refresh rate show in screen prefs: */
            gDeviceInfo.sharedInfo->currentMode.timing.h_total = 1000;
            gDeviceInfo.sharedInfo->currentMode.timing.v_total = 1;
            gDeviceInfo.sharedInfo->currentMode.timing.pixel_clock = 850;

            /* Map the PCI memory space */
            uint32 command_reg = gPCI->read_pci_config(gDeviceInfo.pciInfo.bus,
                                                       gDeviceInfo.pciInfo.device, gDeviceInfo.pciInfo.function,  PCI_command, 2);
            command_reg |= PCI_command_io | PCI_command_memory | PCI_command_master;
            gPCI->write_pci_config(gDeviceInfo.pciInfo.bus, gDeviceInfo.pciInfo.device,
                                   gDeviceInfo.pciInfo.function, PCI_command, 2, command_reg);

            gDeviceInfo.sharedInfo->framebufferArea = map_physical_memory("vboxvideo framebuffer",
                                                      (phys_addr_t)gDeviceInfo.pciInfo.u.h0.base_registers[0],
                                                      gDeviceInfo.pciInfo.u.h0.base_register_sizes[0], B_ANY_KERNEL_BLOCK_ADDRESS,
                                                      B_READ_AREA | B_WRITE_AREA, &(gDeviceInfo.sharedInfo->framebuffer));
            vm_set_area_memory_type(gDeviceInfo.sharedInfo->framebufferArea,
                                    (phys_addr_t)gDeviceInfo.pciInfo.u.h0.base_registers[0], B_MTR_WC);
            break;
        }

        pciIndex++;
    }

    return B_OK;
}


const char** publish_devices()
{
    LogFlowFunc(("publish_devices\n"));
    if (gCanHasDevice)
        return (const char **)gDeviceNames;
    return NULL;
}


device_hooks* find_device(const char *name)
{
    LogFlowFunc(("find_device\n"));
    if (gCanHasDevice && strcmp(name, gDeviceInfo.name) == 0)
        return &gDeviceHooks;

    return NULL;
}


void uninit_driver()
{
    LogFlowFunc(("uninit_driver\n"));
    gLock.Delete();
    put_module(VBOXGUEST_MODULE_NAME);
}

status_t device_open(const char *name, uint32 flags, void **cookie)
{
    LogFlowFunc(("device_open\n"));

    if (!gCanHasDevice || strcmp(name, gDeviceInfo.name) != 0)
        return B_BAD_VALUE;

    /** @todo init device! */

    *cookie = (void *)&gDeviceInfo;
    return B_OK;
}


status_t device_close(void *dev)
{
    LogFlowFunc(("device_close\n"));
    return B_ERROR;
}


status_t device_free(void *dev)
{
    LogFlowFunc(("device_free\n"));

    DeviceInfo& di = *(DeviceInfo *)dev;
    gLock.Acquire();

    if (di.openCount <= 1)
    {
        /// @todo deinit device!
        delete_area(di.sharedArea);
        di.sharedArea = -1;
        di.sharedInfo = NULL;
    }

    if (di.openCount > 0)
        di.openCount--;

    gLock.Release();

    return B_OK;
}


status_t device_read(void *dev, off_t pos, void *buf, size_t *len)
{
    LogFlowFunc(("device_read\n"));
    return B_NOT_ALLOWED;
}


status_t device_write(void *dev, off_t pos, const void *buf, size_t *len)
{
    LogFlowFunc(("device_write\n"));
    return B_NOT_ALLOWED;
}


status_t device_ioctl(void *cookie, uint32 msg, void *buf, size_t len)
{
    LogFlowFunc(("device_ioctl\n"));

    DeviceInfo *dev = (DeviceInfo *)cookie;

    switch (msg)
    {
        case B_GET_ACCELERANT_SIGNATURE:
        {
            strcpy((char *)buf, "vboxvideo.accelerant");
            return B_OK;
        }

        case VBOXVIDEO_GET_PRIVATE_DATA:
        {
            /** @todo r=ramshankar: implement RTR0MemUserCopyFrom for haiku. */
            return user_memcpy(buf, &dev->sharedArea, sizeof(area_id));
        }

        case VBOXVIDEO_GET_DEVICE_NAME:
        {
            /** @todo r=ramshankar: implement RTR0MemUserCopyFrom for haiku. */
            if (user_strlcpy((char *)buf, gDeviceInfo.name, len) < B_OK)
                return B_BAD_ADDRESS;
            return B_OK;
        }

        case VBOXVIDEO_SET_DISPLAY_MODE:
        {
            display_mode *mode = (display_mode *)buf;
            VBoxVideoSetModeRegisters(mode->timing.h_display, mode->timing.v_display,
                                      mode->timing.h_display, get_depth_for_color_space(mode->space), 0, 0, 0);
            gDeviceInfo.sharedInfo->currentMode = *mode;
            return B_OK;
        }
        default:
            return B_BAD_VALUE;
    }
}

