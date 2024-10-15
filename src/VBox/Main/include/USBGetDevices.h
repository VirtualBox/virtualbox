/* $Id: USBGetDevices.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Linux host USB device enumeration.
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

#ifndef MAIN_INCLUDED_USBGetDevices_h
#define MAIN_INCLUDED_USBGetDevices_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/usb.h>
#include <iprt/mem.h>
#include <iprt/string.h>

/**
 * Free all the members of a USB device created by the Linux enumeration code.
 *
 * @note This duplicates a USBProxyService method which we needed access too
 *       without pulling in the rest of the proxy service code.
 *
 * @param   pDevice     Pointer to the device.
 */
DECLINLINE(void) deviceFreeMembers(PUSBDEVICE pDevice)
{
    RTStrFree((char *)pDevice->pszManufacturer);
    pDevice->pszManufacturer = NULL;
    RTStrFree((char *)pDevice->pszProduct);
    pDevice->pszProduct = NULL;
    RTStrFree((char *)pDevice->pszSerialNumber);
    pDevice->pszSerialNumber = NULL;

    RTStrFree((char *)pDevice->pszAddress);
    pDevice->pszAddress = NULL;
}

/**
 * Free one USB device created by the Linux enumeration code.
 *
 * @note This duplicates a USBProxyService method which we needed access too
 *       without pulling in the rest of the proxy service code.
 *
 * @param   pDevice     Pointer to the device. NULL is OK.
 */
DECLINLINE(void) deviceFree(PUSBDEVICE pDevice)
{
    if (pDevice)
    {
        deviceFreeMembers(pDevice);
        RTMemFree(pDevice);
    }
}

/**
 * Free a linked list of USB devices created by the Linux enumeration code.
 * @param  ppHead  Pointer to the first device in the linked list
 */
DECLINLINE(void) deviceListFree(PUSBDEVICE *ppHead)
{
    PUSBDEVICE pHead = *ppHead;
    while (pHead)
    {
        PUSBDEVICE pNext = pHead->pNext;
        deviceFree(pHead);
        pHead = pNext;
    }
    *ppHead = NULL;
}

RT_C_DECLS_BEGIN

extern bool USBProxyLinuxCheckDeviceRoot(const char *pcszRoot, bool fIsDeviceNodes);

#ifdef UNIT_TEST
void TestUSBSetupInit(const char *pcszUsbfsRoot, bool fUsbfsAccessible,
                      const char *pcszDevicesRoot, bool fDevicesAccessible,
                      int vrcMethodInitResult);
void TestUSBSetEnv(const char *pcszEnvUsb, const char *pcszEnvUsbRoot);
#endif

extern int USBProxyLinuxChooseMethod(bool *pfUsingUsbfsDevices, const char **ppcszDevicesRoot);
#ifdef UNIT_TEST
extern void TestUSBSetAvailableUsbfsDevices(const char **pacszDeviceAddresses);
extern void TestUSBSetAccessibleFiles(const char **pacszAccessibleFiles);
#endif

extern PUSBDEVICE USBProxyLinuxGetDevices(const char *pcszDevicesRoot, bool fUseSysfs);

RT_C_DECLS_END

#endif /* !MAIN_INCLUDED_USBGetDevices_h */

