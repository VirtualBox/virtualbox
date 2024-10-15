/* $Id: VBoxUsbLib-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox USB ring-3 Driver Interface library, Windows.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_USBPROXY
#include <iprt/win/windows.h>

#include <VBox/sup.h>
#include <VBox/types.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <iprt/path.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/utf16.h>
#include <VBox/log.h>
#include <VBox/usblib.h>
#include <VBox/usblib-win.h>
#include <VBox/usb.h>
#include <VBox/VBoxDrvCfg-win.h>
#pragma warning (disable:4200) /* shuts up the empty array member warnings */
#include <iprt/win/setupapi.h>
#include <usbdi.h>
#include <hidsdi.h>
#include <Dbt.h>

/* Defined in Windows 8 DDK (through usbdi.h) but we use Windows 7 DDK to build. */
#define UsbSuperSpeed   3

#ifdef VBOX_WITH_NEW_USB_ENUM
# include <cfgmgr32.h>
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct _USB_INTERFACE_DESCRIPTOR2
{
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bInterfaceNumber;
    UCHAR  bAlternateSetting;
    UCHAR  bNumEndpoints;
    UCHAR  bInterfaceClass;
    UCHAR  bInterfaceSubClass;
    UCHAR  bInterfaceProtocol;
    UCHAR  iInterface;
    USHORT wNumClasses;
} USB_INTERFACE_DESCRIPTOR2, *PUSB_INTERFACE_DESCRIPTOR2;

typedef struct VBOXUSBGLOBALSTATE
{
    HANDLE hMonitor;
    HANDLE hNotifyEvent;
    HANDLE hInterruptEvent;
    HANDLE hThread;
    HWND   hWnd;
    HANDLE hTimerQueue;
    HANDLE hTimer;
} VBOXUSBGLOBALSTATE, *PVBOXUSBGLOBALSTATE;

typedef struct VBOXUSB_STRING_DR_ENTRY
{
    struct VBOXUSB_STRING_DR_ENTRY *pNext;
    UCHAR iDr;
    USHORT idLang;
    USB_STRING_DESCRIPTOR StrDr;
} VBOXUSB_STRING_DR_ENTRY, *PVBOXUSB_STRING_DR_ENTRY;

/**
 * This represents VBoxUsb device instance
 */
typedef struct VBOXUSB_DEV
{
    struct VBOXUSB_DEV *pNext;
    char                szName[512];
    char                szDriverRegName[512];
} VBOXUSB_DEV, *PVBOXUSB_DEV;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static VBOXUSBGLOBALSTATE g_VBoxUsbGlobal;


static void usbLibVuFreeDevices(PVBOXUSB_DEV pDevInfos)
{
    while (pDevInfos)
    {
        PVBOXUSB_DEV pNext = pDevInfos->pNext;
        RTMemFree(pDevInfos);
        pDevInfos = pNext;
    }
}

/* Check that a proxied device responds the way we expect it to. */
static int usbLibVuDeviceValidate(PVBOXUSB_DEV pVuDev)
{
    HANDLE  hOut = INVALID_HANDLE_VALUE;
    DWORD   dwErr;

    hOut = CreateFile(pVuDev->szName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL,
                      OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM, NULL);

    if (hOut == INVALID_HANDLE_VALUE)
    {
        dwErr = GetLastError();
        AssertFailed();
        LogRelFunc(("Failed to open `%s' (dwErr=%u)!\n", pVuDev->szName, dwErr));
        return VERR_GENERAL_FAILURE;
    }

    USBSUP_VERSION version = {0};
    DWORD cbReturned = 0;
    int rc = VERR_VERSION_MISMATCH;

    do
    {
        if (!DeviceIoControl(hOut, SUPUSB_IOCTL_GET_VERSION, NULL, 0,&version, sizeof(version),  &cbReturned, NULL))
        {
            dwErr = GetLastError();
            AssertFailed();
            LogRelFunc(("SUPUSB_IOCTL_GET_VERSION failed on `%s' (dwErr=%u)!\n", pVuDev->szName, dwErr));
            break;
        }

        if (   version.u32Major != USBDRV_MAJOR_VERSION
#if USBDRV_MINOR_VERSION != 0
            || version.u32Minor <  USBDRV_MINOR_VERSION
#endif
           )
        {
            AssertFailed();
            LogRelFunc(("Invalid version %d:%d (%s) vs %d:%d (library)!\n", version.u32Major, version.u32Minor, pVuDev->szName, USBDRV_MAJOR_VERSION, USBDRV_MINOR_VERSION));
            break;
        }

        if (!DeviceIoControl(hOut, SUPUSB_IOCTL_IS_OPERATIONAL, NULL, 0, NULL, NULL, &cbReturned, NULL))
        {
            dwErr = GetLastError();
            AssertFailed();
            LogRelFunc(("SUPUSB_IOCTL_IS_OPERATIONAL failed on `%s' (dwErr=%u)!\n", pVuDev->szName, dwErr));
            break;
        }

        rc = VINF_SUCCESS;
    } while (0);

    CloseHandle(hOut);
    return rc;
}

#ifndef VBOX_WITH_NEW_USB_ENUM
static int usbLibVuDevicePopulate(PVBOXUSB_DEV pVuDev, HDEVINFO hDevInfo, PSP_DEVICE_INTERFACE_DATA pIfData)
{
    DWORD cbIfDetailData;
    int rc = VINF_SUCCESS;

    SetupDiGetDeviceInterfaceDetail(hDevInfo, pIfData,
                NULL, /* OUT PSP_DEVICE_INTERFACE_DETAIL_DATA DeviceInterfaceDetailData */
                0, /* IN DWORD DeviceInterfaceDetailDataSize */
                &cbIfDetailData,
                NULL
                );
    Assert(GetLastError() == ERROR_INSUFFICIENT_BUFFER);

    PSP_DEVICE_INTERFACE_DETAIL_DATA pIfDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)RTMemAllocZ(cbIfDetailData);
    if (!pIfDetailData)
    {
        AssertMsgFailed(("RTMemAllocZ failed\n"));
        return VERR_OUT_OF_RESOURCES;
    }

    DWORD cbDbgRequired;
    SP_DEVINFO_DATA DevInfoData;
    DevInfoData.cbSize = sizeof (DevInfoData);
    /* the cbSize should contain the sizeof a fixed-size part according to the docs */
    pIfDetailData->cbSize = sizeof (*pIfDetailData);
    do
    {
        if (!SetupDiGetDeviceInterfaceDetail(hDevInfo, pIfData,
                                pIfDetailData,
                                cbIfDetailData,
                                &cbDbgRequired,
                                &DevInfoData))
        {
            DWORD dwErr = GetLastError(); NOREF(dwErr);
            AssertMsgFailed(("SetupDiGetDeviceInterfaceDetail, cbRequired (%d), was (%d), dwErr (%d)\n", cbDbgRequired, cbIfDetailData, dwErr));
            rc = VERR_GENERAL_FAILURE;
            break;
        }

        strncpy(pVuDev->szName, pIfDetailData->DevicePath, sizeof (pVuDev->szName));

        if (!SetupDiGetDeviceRegistryPropertyA(hDevInfo, &DevInfoData, SPDRP_DRIVER,
            NULL, /* OUT PDWORD PropertyRegDataType */
            (PBYTE)pVuDev->szDriverRegName,
            sizeof (pVuDev->szDriverRegName),
            &cbDbgRequired))
        {
            DWORD dwErr = GetLastError(); NOREF(dwErr);
            AssertMsgFailed(("SetupDiGetDeviceRegistryPropertyA, cbRequired (%d), was (%d), dwErr (%d)\n", cbDbgRequired, sizeof (pVuDev->szDriverRegName), dwErr));
            rc = VERR_GENERAL_FAILURE;
            break;
        }

        rc = usbLibVuDeviceValidate(pVuDev);
        LogRelFunc(("Found VBoxUSB on `%s' (rc=%d)\n", pVuDev->szName, rc));
        AssertRC(rc);
    } while (0);

    RTMemFree(pIfDetailData);
    return rc;
}

static int usbLibVuGetDevices(PVBOXUSB_DEV *ppVuDevs, uint32_t *pcVuDevs)
{
    *ppVuDevs = NULL;
    *pcVuDevs = 0;

    HDEVINFO hDevInfo =  SetupDiGetClassDevs(&GUID_CLASS_VBOXUSB,
            NULL, /* IN PCTSTR Enumerator */
            NULL, /* IN HWND hwndParent */
            (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE) /* IN DWORD Flags */
            );
    if (hDevInfo == INVALID_HANDLE_VALUE)
    {
        DWORD dwErr = GetLastError(); NOREF(dwErr);
        AssertMsgFailed(("SetupDiGetClassDevs, dwErr (%u)\n", dwErr));
        return VERR_GENERAL_FAILURE;
    }

    for (int i = 0; ; ++i)
    {
        SP_DEVICE_INTERFACE_DATA IfData;
        IfData.cbSize = sizeof (IfData);
        if (!SetupDiEnumDeviceInterfaces(hDevInfo,
                            NULL, /* IN PSP_DEVINFO_DATA DeviceInfoData */
                            &GUID_CLASS_VBOXUSB, /* IN LPGUID InterfaceClassGuid */
                            i,
                            &IfData))
        {
            DWORD dwErr = GetLastError();
            if (dwErr == ERROR_NO_MORE_ITEMS)
                break;

            AssertMsgFailed(("SetupDiEnumDeviceInterfaces, dwErr (%u), resuming\n", dwErr));
            continue;
        }

        /* we've now got the IfData */
        PVBOXUSB_DEV pVuDev = (PVBOXUSB_DEV)RTMemAllocZ(sizeof (*pVuDev));
        if (!pVuDev)
        {
            AssertMsgFailed(("RTMemAllocZ failed, resuming\n"));
            continue;
        }

        int rc = usbLibVuDevicePopulate(pVuDev, hDevInfo, &IfData);
        if (!RT_SUCCESS(rc))
        {
            AssertMsgFailed(("usbLibVuDevicePopulate failed, rc (%d), resuming\n", rc));
            continue;
        }

        pVuDev->pNext = *ppVuDevs;
        *ppVuDevs = pVuDev;
        ++*pcVuDevs;
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);

    return VINF_SUCCESS;
}

static int usbLibDevPopulate(PUSBDEVICE pDev, PUSB_NODE_CONNECTION_INFORMATION_EX pConInfo, ULONG iPort, LPCSTR lpszDrvKeyName, LPCSTR lpszHubName, PVBOXUSB_STRING_DR_ENTRY pDrList)
{
    pDev->bcdUSB = pConInfo->DeviceDescriptor.bcdUSB;
    pDev->bDeviceClass = pConInfo->DeviceDescriptor.bDeviceClass;
    pDev->bDeviceSubClass = pConInfo->DeviceDescriptor.bDeviceSubClass;
    pDev->bDeviceProtocol = pConInfo->DeviceDescriptor.bDeviceProtocol;
    pDev->idVendor = pConInfo->DeviceDescriptor.idVendor;
    pDev->idProduct = pConInfo->DeviceDescriptor.idProduct;
    pDev->bcdDevice = pConInfo->DeviceDescriptor.bcdDevice;
    pDev->bBus = 0; /** @todo figure out bBus on windows... */
    pDev->bPort = iPort;
    /** @todo check which devices are used for primary input (keyboard & mouse) */
    if (!lpszDrvKeyName || *lpszDrvKeyName == 0)
        pDev->enmState = USBDEVICESTATE_UNUSED;
    else
        pDev->enmState = USBDEVICESTATE_USED_BY_HOST_CAPTURABLE;

    /* Determine the speed the device is operating at. */
    switch (pConInfo->Speed)
    {
        case UsbLowSpeed:   pDev->enmSpeed = USBDEVICESPEED_LOW;    break;
        case UsbFullSpeed:  pDev->enmSpeed = USBDEVICESPEED_FULL;   break;
        case UsbHighSpeed:  pDev->enmSpeed = USBDEVICESPEED_HIGH;   break;
        default:    /* If we don't know, most likely it's something new. */
        case UsbSuperSpeed: pDev->enmSpeed = USBDEVICESPEED_SUPER;  break;
    }
    /* Unfortunately USB_NODE_CONNECTION_INFORMATION_EX will not report UsbSuperSpeed, and
     * it's not even defined in the Win7 DDK we use. So we go by the USB version, and
     * luckily we know that USB3 must mean SuperSpeed. The USB3 spec guarantees this (9.6.1).
     */
    if (pDev->bcdUSB >= 0x0300)
        pDev->enmSpeed = USBDEVICESPEED_SUPER;

    int rc = RTStrAPrintf((char **)&pDev->pszAddress, "%s", lpszDrvKeyName);
    if (rc < 0)
        return VERR_NO_MEMORY;
    pDev->pszBackend = RTStrDup("host");
    if (!pDev->pszBackend)
    {
        RTStrFree((char *)pDev->pszAddress);
        return VERR_NO_STR_MEMORY;
    }
    pDev->pszHubName = RTStrDup(lpszHubName);
    pDev->bNumConfigurations = 0;
    pDev->u64SerialHash = 0;

    for (; pDrList; pDrList = pDrList->pNext)
    {
        char **ppszString = NULL;
        if (   pConInfo->DeviceDescriptor.iManufacturer
            && pDrList->iDr == pConInfo->DeviceDescriptor.iManufacturer)
            ppszString = (char **)&pDev->pszManufacturer;
        else if (   pConInfo->DeviceDescriptor.iProduct
                 && pDrList->iDr == pConInfo->DeviceDescriptor.iProduct)
            ppszString = (char **)&pDev->pszProduct;
        else if (   pConInfo->DeviceDescriptor.iSerialNumber
                 && pDrList->iDr == pConInfo->DeviceDescriptor.iSerialNumber)
            ppszString = (char **)&pDev->pszSerialNumber;
        if (ppszString)
        {
            rc = RTUtf16ToUtf8((PCRTUTF16)pDrList->StrDr.bString, ppszString);
            if (RT_SUCCESS(rc))
            {
                Assert(*ppszString);
                USBLibPurgeEncoding(*ppszString);

                if (pDrList->iDr == pConInfo->DeviceDescriptor.iSerialNumber)
                    pDev->u64SerialHash = USBLibHashSerial(*ppszString);
            }
            else
            {
                AssertMsgFailed(("RTUtf16ToUtf8 failed, rc (%d), resuming\n", rc));
                *ppszString = NULL;
            }
        }
    }

    return VINF_SUCCESS;
}
#else

static PSP_DEVICE_INTERFACE_DETAIL_DATA usbLibGetDevDetail(HDEVINFO InfoSet, PSP_DEVICE_INTERFACE_DATA InterfaceData, PSP_DEVINFO_DATA DevInfoData);
static void *usbLibGetRegistryProperty(HDEVINFO InfoSet, const PSP_DEVINFO_DATA DevData, DWORD Property);

/* Populate the data for a single proxied USB device. */
static int usbLibVUsbDevicePopulate(PVBOXUSB_DEV pVuDev, HDEVINFO InfoSet, PSP_DEVICE_INTERFACE_DATA InterfaceData)
{
    PSP_DEVICE_INTERFACE_DETAIL_DATA    DetailData = NULL;
    SP_DEVINFO_DATA                     DeviceData;
    LPCSTR                              Location;
    int                                 rc = VINF_SUCCESS;

    memset(&DeviceData, 0, sizeof(DeviceData));
    DeviceData.cbSize = sizeof(DeviceData);
    /* The interface detail includes the device path. */
    DetailData = usbLibGetDevDetail(InfoSet, InterfaceData, &DeviceData);
    if (DetailData)
    {
        strncpy(pVuDev->szName, DetailData->DevicePath, sizeof(pVuDev->szName));

        /* The location is used as a unique identifier for cross-referencing the two lists. */
        Location = (LPCSTR)usbLibGetRegistryProperty(InfoSet, &DeviceData, SPDRP_DRIVER);
        if (Location)
        {
            strncpy(pVuDev->szDriverRegName, Location, sizeof(pVuDev->szDriverRegName));
            rc = usbLibVuDeviceValidate(pVuDev);
            LogRelFunc(("Found VBoxUSB on `%s' (rc=%d)\n", pVuDev->szName, rc));
            AssertRC(rc);

            RTMemFree((void *)Location);
        }
        else
        {
            /* Errors will be logged by usbLibGetRegistryProperty(). */
            rc = VERR_GENERAL_FAILURE;
        }

        RTMemFree(DetailData);
    }
    else
    {
        /* Errors will be logged by usbLibGetDevDetail(). */
        rc = VERR_GENERAL_FAILURE;
    }


    return rc;
}

/* Enumerate proxied USB devices (with VBoxUSB.sys loaded). */
static int usbLibEnumVUsbDevices(PVBOXUSB_DEV *ppVuDevs, uint32_t *pcVuDevs)
{
    SP_DEVICE_INTERFACE_DATA    InterfaceData;
    HDEVINFO                    InfoSet;
    DWORD                       DeviceIndex;
    DWORD                       dwErr;

    *ppVuDevs = NULL;
    *pcVuDevs = 0;

    /* Enumerate all present devices which support the GUID_CLASS_VBOXUSB interface. */
    InfoSet = SetupDiGetClassDevs(&GUID_CLASS_VBOXUSB, NULL, NULL,
                                  (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
    if (InfoSet == INVALID_HANDLE_VALUE)
    {
        DWORD dwErr = GetLastError();
        LogRelFunc(("SetupDiGetClassDevs for GUID_CLASS_VBOXUSB failed (dwErr=%u)\n", dwErr));
        AssertFailed();
        return VERR_GENERAL_FAILURE;
    }

    memset(&InterfaceData, 0, sizeof(InterfaceData));
    InterfaceData.cbSize = sizeof(InterfaceData);
    DeviceIndex = 0;

    /* Loop over the enumerated list. */
    while (SetupDiEnumDeviceInterfaces(InfoSet, NULL, &GUID_CLASS_VBOXUSB, DeviceIndex, &InterfaceData))
    {
        /* we've now got the IfData */
        PVBOXUSB_DEV pVuDev = (PVBOXUSB_DEV)RTMemAllocZ(sizeof (*pVuDev));
        if (!pVuDev)
        {
            AssertFailed();
            LogRelFunc(("RTMemAllocZ failed\n"));
            break;
        }

        int rc = usbLibVUsbDevicePopulate(pVuDev, InfoSet, &InterfaceData);
        if (RT_SUCCESS(rc))
        {
            pVuDev->pNext = *ppVuDevs;
            *ppVuDevs = pVuDev;
            ++*pcVuDevs;
        }
        else    /* Skip this device but continue enumerating. */
            AssertMsgFailed(("usbLibVuDevicePopulate failed, rc=%d\n", rc));

        memset(&InterfaceData, 0, sizeof(InterfaceData));
        InterfaceData.cbSize = sizeof(InterfaceData);
        ++DeviceIndex;
    }

    /* Paranoia. */
    dwErr = GetLastError();
    if (dwErr != ERROR_NO_MORE_ITEMS)
    {
        LogRelFunc(("SetupDiEnumDeviceInterfaces failed (dwErr=%u)\n", dwErr));
        AssertFailed();
    }

    SetupDiDestroyDeviceInfoList(InfoSet);

    return VINF_SUCCESS;
}

static uint16_t usbLibParseHexNumU16(LPCSTR *ppStr)
{
    const char  *pStr = *ppStr;
    char        c;
    uint16_t    num = 0;
    unsigned    u;

    for (int i = 0; i < 4; ++i)
    {
        if (!*pStr)     /* Just in case the string is too short. */
            break;

        c = *pStr;
        u = c >= 'A' ? c - 'A' + 10 : c - '0';  /* Hex digit to number. */
        num |= u << (12 - 4 * i);
        pStr++;
    }
    *ppStr = pStr;

    return num;
}

static int usbLibDevPopulate(PUSBDEVICE pDev, PUSB_NODE_CONNECTION_INFORMATION_EX pConInfo, ULONG iPort, LPCSTR lpszLocation, LPCSTR lpszDrvKeyName, LPCSTR lpszHubName, PVBOXUSB_STRING_DR_ENTRY pDrList)
{
    pDev->bcdUSB = pConInfo->DeviceDescriptor.bcdUSB;
    pDev->bDeviceClass = pConInfo->DeviceDescriptor.bDeviceClass;
    pDev->bDeviceSubClass = pConInfo->DeviceDescriptor.bDeviceSubClass;
    pDev->bDeviceProtocol = pConInfo->DeviceDescriptor.bDeviceProtocol;
    pDev->idVendor = pConInfo->DeviceDescriptor.idVendor;
    pDev->idProduct = pConInfo->DeviceDescriptor.idProduct;
    pDev->bcdDevice = pConInfo->DeviceDescriptor.bcdDevice;
    pDev->bBus = 0; /* The hub numbering is not very useful on Windows. Skip it. */
    pDev->bPort = iPort;

    /* The port path/location uniquely identifies the port. */
    pDev->pszPortPath = RTStrDup(lpszLocation);
    if (!pDev->pszPortPath)
        return VERR_NO_STR_MEMORY;

    /* If there is no DriverKey, the device is unused because there's no driver. */
    if (!lpszDrvKeyName || *lpszDrvKeyName == 0)
        pDev->enmState = USBDEVICESTATE_UNUSED;
    else
        pDev->enmState = USBDEVICESTATE_USED_BY_HOST_CAPTURABLE;

    /* Determine the speed the device is operating at. */
    switch (pConInfo->Speed)
    {
        case UsbLowSpeed:   pDev->enmSpeed = USBDEVICESPEED_LOW;    break;
        case UsbFullSpeed:  pDev->enmSpeed = USBDEVICESPEED_FULL;   break;
        case UsbHighSpeed:  pDev->enmSpeed = USBDEVICESPEED_HIGH;   break;
        default:    /* If we don't know, most likely it's something new. */
        case UsbSuperSpeed: pDev->enmSpeed = USBDEVICESPEED_SUPER;  break;
    }
    /* Unfortunately USB_NODE_CONNECTION_INFORMATION_EX will not report UsbSuperSpeed, and
     * it's not even defined in the Win7 DDK we use. So we go by the USB version, and
     * luckily we know that USB3 must mean SuperSpeed. The USB3 spec guarantees this (9.6.1).
     */
    if (pDev->bcdUSB >= 0x0300)
        pDev->enmSpeed = USBDEVICESPEED_SUPER;

    /* If there's no DriverKey, jam in an empty string to avoid NULL pointers. */
    if (!lpszDrvKeyName)
        pDev->pszAddress = RTStrDup("");
    else
        pDev->pszAddress = RTStrDup(lpszDrvKeyName);

    pDev->pszBackend = RTStrDup("host");
    if (!pDev->pszBackend)
    {
        RTStrFree((char *)pDev->pszAddress);
        return VERR_NO_STR_MEMORY;
    }
    pDev->pszHubName = RTStrDup(lpszHubName);
    pDev->bNumConfigurations = 0;
    pDev->u64SerialHash = 0;

    for (; pDrList; pDrList = pDrList->pNext)
    {
        char **ppszString = NULL;
        if (   pConInfo->DeviceDescriptor.iManufacturer
            && pDrList->iDr == pConInfo->DeviceDescriptor.iManufacturer)
            ppszString = (char **)&pDev->pszManufacturer;
        else if (   pConInfo->DeviceDescriptor.iProduct
                 && pDrList->iDr == pConInfo->DeviceDescriptor.iProduct)
            ppszString = (char **)&pDev->pszProduct;
        else if (   pConInfo->DeviceDescriptor.iSerialNumber
                 && pDrList->iDr == pConInfo->DeviceDescriptor.iSerialNumber)
            ppszString = (char **)&pDev->pszSerialNumber;
        if (ppszString)
        {
            int rc = RTUtf16ToUtf8((PCRTUTF16)pDrList->StrDr.bString, ppszString);
            if (RT_SUCCESS(rc))
            {
                Assert(*ppszString);
                USBLibPurgeEncoding(*ppszString);

                if (pDrList->iDr == pConInfo->DeviceDescriptor.iSerialNumber)
                    pDev->u64SerialHash = USBLibHashSerial(*ppszString);
            }
            else
            {
                AssertMsgFailed(("RTUtf16ToUtf8 failed, rc (%d), resuming\n", rc));
                *ppszString = NULL;
            }
        }
    }

    return VINF_SUCCESS;
}
#endif

static void usbLibDevStrFree(LPSTR lpszName)
{
    RTStrFree(lpszName);
}

#ifndef VBOX_WITH_NEW_USB_ENUM
static int usbLibDevStrDriverKeyGet(HANDLE hHub, ULONG iPort, LPSTR* plpszName)
{
    USB_NODE_CONNECTION_DRIVERKEY_NAME Name;
    DWORD cbReturned = 0;
    Name.ConnectionIndex = iPort;
    *plpszName = NULL;
    if (!DeviceIoControl(hHub, IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME, &Name, sizeof (Name), &Name, sizeof (Name), &cbReturned, NULL))
    {
#ifdef VBOX_WITH_ANNOYING_USB_ASSERTIONS
        AssertMsgFailed(("DeviceIoControl 1 fail dwErr (%u)\n", GetLastError()));
#endif
        return VERR_GENERAL_FAILURE;
    }

    if (Name.ActualLength < sizeof (Name))
    {
        AssertFailed();
        return VERR_OUT_OF_RESOURCES;
    }

    PUSB_NODE_CONNECTION_DRIVERKEY_NAME pName = (PUSB_NODE_CONNECTION_DRIVERKEY_NAME)RTMemAllocZ(Name.ActualLength);
    if (!pName)
    {
        AssertFailed();
        return VERR_OUT_OF_RESOURCES;
    }

    int rc = VINF_SUCCESS;
    pName->ConnectionIndex = iPort;
    if (DeviceIoControl(hHub, IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME, pName, Name.ActualLength, pName, Name.ActualLength, &cbReturned, NULL))
    {
        rc = RTUtf16ToUtf8Ex((PCRTUTF16)pName->DriverKeyName, pName->ActualLength / sizeof (WCHAR), plpszName, 0, NULL);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
            rc = VINF_SUCCESS;
    }
    else
    {
        DWORD dwErr = GetLastError(); NOREF(dwErr);
        AssertMsgFailed(("DeviceIoControl 2 fail dwErr (%u)\n", dwErr));
        rc = VERR_GENERAL_FAILURE;
    }
    RTMemFree(pName);
    return rc;
}
#endif

static int usbLibDevStrHubNameGet(HANDLE hHub, ULONG iPort, LPSTR* plpszName)
{
    USB_NODE_CONNECTION_NAME Name;
    DWORD cbReturned = 0;
    Name.ConnectionIndex = iPort;
    *plpszName = NULL;
    if (!DeviceIoControl(hHub, IOCTL_USB_GET_NODE_CONNECTION_NAME, &Name, sizeof (Name), &Name, sizeof (Name), &cbReturned, NULL))
    {
        AssertFailed();
        return VERR_GENERAL_FAILURE;
    }

    if (Name.ActualLength < sizeof (Name))
    {
        AssertFailed();
        return VERR_OUT_OF_RESOURCES;
    }

    PUSB_NODE_CONNECTION_NAME pName = (PUSB_NODE_CONNECTION_NAME)RTMemAllocZ(Name.ActualLength);
    if (!pName)
    {
        AssertFailed();
        return VERR_OUT_OF_RESOURCES;
    }

    int rc = VINF_SUCCESS;
    pName->ConnectionIndex = iPort;
    if (DeviceIoControl(hHub, IOCTL_USB_GET_NODE_CONNECTION_NAME, pName, Name.ActualLength, pName, Name.ActualLength, &cbReturned, NULL))
    {
        rc = RTUtf16ToUtf8Ex((PCRTUTF16)pName->NodeName, pName->ActualLength / sizeof (WCHAR), plpszName, 0, NULL);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
            rc = VINF_SUCCESS;
    }
    else
    {
        AssertFailed();
        rc = VERR_GENERAL_FAILURE;
    }
    RTMemFree(pName);
    return rc;
}

static int usbLibDevStrRootHubNameGet(HANDLE hCtl, LPSTR* plpszName)
{
    USB_ROOT_HUB_NAME HubName;
    DWORD cbReturned = 0;
    *plpszName = NULL;
    if (!DeviceIoControl(hCtl, IOCTL_USB_GET_ROOT_HUB_NAME, NULL, 0, &HubName, sizeof (HubName), &cbReturned, NULL))
    {
        return VERR_GENERAL_FAILURE;
    }
    PUSB_ROOT_HUB_NAME pHubName = (PUSB_ROOT_HUB_NAME)RTMemAllocZ(HubName.ActualLength);
    if (!pHubName)
        return VERR_OUT_OF_RESOURCES;

    int rc = VINF_SUCCESS;
    if (DeviceIoControl(hCtl, IOCTL_USB_GET_ROOT_HUB_NAME, NULL, 0, pHubName, HubName.ActualLength, &cbReturned, NULL))
    {
        rc = RTUtf16ToUtf8Ex((PCRTUTF16)pHubName->RootHubName, pHubName->ActualLength / sizeof (WCHAR), plpszName, 0, NULL);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
            rc = VINF_SUCCESS;
    }
    else
    {
        rc = VERR_GENERAL_FAILURE;
    }
    RTMemFree(pHubName);
    return rc;
}

static int usbLibDevCfgDrGet(HANDLE hHub, LPCSTR lpcszHubName, ULONG iPort, ULONG iDr, PUSB_CONFIGURATION_DESCRIPTOR *ppDr)
{
    *ppDr = NULL;

    char Buf[sizeof (USB_DESCRIPTOR_REQUEST) + sizeof (USB_CONFIGURATION_DESCRIPTOR)];
    memset(&Buf, 0, sizeof (Buf));

    PUSB_DESCRIPTOR_REQUEST pCfgDrRq = (PUSB_DESCRIPTOR_REQUEST)Buf;
    PUSB_CONFIGURATION_DESCRIPTOR pCfgDr = (PUSB_CONFIGURATION_DESCRIPTOR)(Buf + sizeof (*pCfgDrRq));

    pCfgDrRq->ConnectionIndex = iPort;
    pCfgDrRq->SetupPacket.wValue = (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8) | iDr;
    pCfgDrRq->SetupPacket.wLength = (USHORT)(sizeof (USB_CONFIGURATION_DESCRIPTOR));
    DWORD cbReturned = 0;
    if (!DeviceIoControl(hHub, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, pCfgDrRq, sizeof (Buf),
                                pCfgDrRq, sizeof (Buf),
                                &cbReturned, NULL))
    {
        DWORD dwErr = GetLastError();
        LogRelFunc(("IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION #1 failed (dwErr=%u) on hub %s port %d\n", dwErr, lpcszHubName, iPort));
#ifdef VBOX_WITH_ANNOYING_USB_ASSERTIONS
        AssertFailed();
#endif
        return VERR_GENERAL_FAILURE;
    }

    if (sizeof (Buf) != cbReturned)
    {
        AssertFailed();
        return VERR_GENERAL_FAILURE;
    }

    if (pCfgDr->wTotalLength < sizeof (USB_CONFIGURATION_DESCRIPTOR))
    {
        AssertFailed();
        return VERR_GENERAL_FAILURE;
    }

    DWORD cbRq = sizeof (USB_DESCRIPTOR_REQUEST) + pCfgDr->wTotalLength;
    PUSB_DESCRIPTOR_REQUEST pRq = (PUSB_DESCRIPTOR_REQUEST)RTMemAllocZ(cbRq);
    Assert(pRq);
    if (!pRq)
        return VERR_OUT_OF_RESOURCES;

    int rc = VERR_GENERAL_FAILURE;
    do
    {
        PUSB_CONFIGURATION_DESCRIPTOR pDr = (PUSB_CONFIGURATION_DESCRIPTOR)(pRq + 1);
        pRq->ConnectionIndex = iPort;
        pRq->SetupPacket.wValue = (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8) | iDr;
        pRq->SetupPacket.wLength = (USHORT)(cbRq - sizeof (USB_DESCRIPTOR_REQUEST));
        if (!DeviceIoControl(hHub, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, pRq, cbRq,
                                    pRq, cbRq,
                                    &cbReturned, NULL))
        {
            DWORD dwErr = GetLastError();
            LogRelFunc(("IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION #2 failed (dwErr=%u) on hub %s port %d\n", dwErr, lpcszHubName, iPort));
#ifdef VBOX_WITH_ANNOYING_USB_ASSERTIONS
            AssertFailed();
#endif
            break;
        }

        if (cbRq != cbReturned)
        {
            AssertFailed();
            break;
        }

        if (pDr->wTotalLength != cbRq - sizeof (USB_DESCRIPTOR_REQUEST))
        {
            AssertFailed();
            break;
        }

        *ppDr = pDr;
        return VINF_SUCCESS;
    } while (0);

    RTMemFree(pRq);
    return rc;
}

static void usbLibDevCfgDrFree(PUSB_CONFIGURATION_DESCRIPTOR pDr)
{
    Assert(pDr);
    PUSB_DESCRIPTOR_REQUEST pRq = ((PUSB_DESCRIPTOR_REQUEST)pDr)-1;
    RTMemFree(pRq);
}

static int usbLibDevStrDrEntryGet(HANDLE hHub, LPCSTR lpcszHubName, ULONG iPort, ULONG iDr, USHORT idLang, PVBOXUSB_STRING_DR_ENTRY *ppList)
{
    char szBuf[sizeof (USB_DESCRIPTOR_REQUEST) + MAXIMUM_USB_STRING_LENGTH];
    RT_ZERO(szBuf);

    PUSB_DESCRIPTOR_REQUEST pRq = (PUSB_DESCRIPTOR_REQUEST)szBuf;
    PUSB_STRING_DESCRIPTOR pDr = (PUSB_STRING_DESCRIPTOR)(szBuf + sizeof (*pRq));
    RT_BZERO(pDr, sizeof(USB_STRING_DESCRIPTOR));

    pRq->ConnectionIndex = iPort;
    pRq->SetupPacket.wValue = (USB_STRING_DESCRIPTOR_TYPE << 8) | iDr;
    pRq->SetupPacket.wIndex = idLang;
    pRq->SetupPacket.wLength = sizeof (szBuf) - sizeof (*pRq);

    DWORD cbReturned = 0;
    if (!DeviceIoControl(hHub, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, pRq, sizeof (szBuf),
                         pRq, sizeof(szBuf),
                         &cbReturned, NULL))
    {
        DWORD dwErr = GetLastError();
        LogRel(("Getting USB descriptor (id %u) failed (dwErr=%u) on hub %s port %d\n", iDr, dwErr, lpcszHubName, iPort));
        return RTErrConvertFromWin32(dwErr);
    }

    /* Wrong descriptor type at the requested port index? Bail out. */
    if (pDr->bDescriptorType != USB_STRING_DESCRIPTOR_TYPE)
        return VERR_NOT_FOUND;

    /* Some more sanity checks. */
    if (   (cbReturned < sizeof (*pDr) + 2)
        || (!!(pDr->bLength % 2))
        || (pDr->bLength != cbReturned - sizeof(*pRq)))
    {
        AssertMsgFailed(("Sanity check failed for string descriptor: cbReturned=%RI32, cbDevReq=%zu, type=%RU8, len=%RU8, port=%RU32, index=%RU32, lang=%RU32\n",
                         cbReturned, sizeof(*pRq), pDr->bDescriptorType, pDr->bLength, iPort, iDr, idLang));
        return VERR_INVALID_PARAMETER;
    }

    PVBOXUSB_STRING_DR_ENTRY pEntry =
        (PVBOXUSB_STRING_DR_ENTRY)RTMemAllocZ(sizeof(VBOXUSB_STRING_DR_ENTRY) + pDr->bLength + 2);
    AssertPtr(pEntry);
    if (!pEntry)
        return VERR_NO_MEMORY;

    pEntry->pNext = *ppList;
    pEntry->iDr = iDr;
    pEntry->idLang = idLang;
    memcpy(&pEntry->StrDr, pDr, pDr->bLength);

    *ppList = pEntry;

    return VINF_SUCCESS;
}

static void usbLibDevStrDrEntryFree(PVBOXUSB_STRING_DR_ENTRY pDr)
{
    RTMemFree(pDr);
}

static void usbLibDevStrDrEntryFreeList(PVBOXUSB_STRING_DR_ENTRY pDr)
{
    while (pDr)
    {
        PVBOXUSB_STRING_DR_ENTRY pNext = pDr->pNext;
        usbLibDevStrDrEntryFree(pDr);
        pDr = pNext;
    }
}

static int usbLibDevStrDrEntryGetForLangs(HANDLE hHub, LPCSTR lpcszHubName, ULONG iPort, ULONG iDr, ULONG cIdLang, const USHORT *pIdLang, PVBOXUSB_STRING_DR_ENTRY *ppList)
{
    for (ULONG i = 0; i < cIdLang; ++i)
    {
        usbLibDevStrDrEntryGet(hHub, lpcszHubName, iPort, iDr, pIdLang[i], ppList);
    }
    return VINF_SUCCESS;
}

static int usbLibDevStrDrEntryGetAll(HANDLE hHub, LPCSTR lpcszHubName, ULONG iPort, PUSB_DEVICE_DESCRIPTOR pDevDr, PUSB_CONFIGURATION_DESCRIPTOR pCfgDr, PVBOXUSB_STRING_DR_ENTRY *ppList)
{
    /* Read string descriptor zero to determine what languages are available. */
    int rc = usbLibDevStrDrEntryGet(hHub, lpcszHubName, iPort, 0, 0, ppList);
    if (RT_FAILURE(rc))
        return rc;

    PUSB_STRING_DESCRIPTOR pLangStrDr = &(*ppList)->StrDr;
    USHORT *pIdLang = (USHORT *)pLangStrDr->bString;
    ULONG cIdLang = (pLangStrDr->bLength - RT_OFFSETOF(USB_STRING_DESCRIPTOR, bString)) / sizeof (*pIdLang);

    if (pDevDr->iManufacturer)
    {
        rc = usbLibDevStrDrEntryGetForLangs(hHub, lpcszHubName, iPort, pDevDr->iManufacturer, cIdLang, pIdLang, ppList);
        AssertRC(rc);
    }

    if (pDevDr->iProduct)
    {
        rc = usbLibDevStrDrEntryGetForLangs(hHub, lpcszHubName, iPort, pDevDr->iProduct, cIdLang, pIdLang, ppList);
        AssertRC(rc);
    }

    if (pDevDr->iSerialNumber)
    {
        rc = usbLibDevStrDrEntryGetForLangs(hHub, lpcszHubName, iPort, pDevDr->iSerialNumber, cIdLang, pIdLang, ppList);
        AssertRC(rc);
    }

    PUCHAR pCur = (PUCHAR)pCfgDr;
    PUCHAR pEnd = pCur + pCfgDr->wTotalLength;
    while (pCur + sizeof (USB_COMMON_DESCRIPTOR) <= pEnd)
    {
        PUSB_COMMON_DESCRIPTOR pCmnDr = (PUSB_COMMON_DESCRIPTOR)pCur;
        if (pCur + pCmnDr->bLength > pEnd)
        {
            AssertFailed();
            break;
        }

        /* This is invalid but was seen with a TerraTec Aureon 7.1 USB sound card. */
        if (!pCmnDr->bLength)
            break;

        switch (pCmnDr->bDescriptorType)
        {
            case USB_CONFIGURATION_DESCRIPTOR_TYPE:
            {
                if (pCmnDr->bLength != sizeof (USB_CONFIGURATION_DESCRIPTOR))
                {
                    AssertFailed();
                    break;
                }
                PUSB_CONFIGURATION_DESCRIPTOR pCurCfgDr = (PUSB_CONFIGURATION_DESCRIPTOR)pCmnDr;
                if (!pCurCfgDr->iConfiguration)
                    break;
                rc = usbLibDevStrDrEntryGetForLangs(hHub, lpcszHubName, iPort, pCurCfgDr->iConfiguration, cIdLang, pIdLang, ppList);
                AssertRC(rc);
                break;
            }
            case USB_INTERFACE_DESCRIPTOR_TYPE:
            {
                if (pCmnDr->bLength != sizeof (USB_INTERFACE_DESCRIPTOR) && pCmnDr->bLength != sizeof (USB_INTERFACE_DESCRIPTOR2))
                {
                    AssertFailed();
                    break;
                }
                PUSB_INTERFACE_DESCRIPTOR pCurIfDr = (PUSB_INTERFACE_DESCRIPTOR)pCmnDr;
                if (!pCurIfDr->iInterface)
                    break;
                rc = usbLibDevStrDrEntryGetForLangs(hHub, lpcszHubName, iPort, pCurIfDr->iInterface, cIdLang, pIdLang, ppList);
                AssertRC(rc);
                break;
            }
            default:
                break;
        }

        pCur = pCur + pCmnDr->bLength;
    }

    return VINF_SUCCESS;
}

#ifndef VBOX_WITH_NEW_USB_ENUM
static int usbLibDevGetHubDevices(LPCSTR lpszName, PUSBDEVICE *ppDevs, uint32_t *pcDevs);

static int usbLibDevGetHubPortDevices(HANDLE hHub, LPCSTR lpcszHubName, ULONG iPort, PUSBDEVICE *ppDevs, uint32_t *pcDevs)
{
    int rc = VINF_SUCCESS;
    char Buf[sizeof (USB_NODE_CONNECTION_INFORMATION_EX) + (sizeof (USB_PIPE_INFO) * 20)];
    PUSB_NODE_CONNECTION_INFORMATION_EX pConInfo = (PUSB_NODE_CONNECTION_INFORMATION_EX)Buf;
    //PUSB_PIPE_INFO paPipeInfo = (PUSB_PIPE_INFO)(Buf + sizeof (PUSB_NODE_CONNECTION_INFORMATION_EX));
    DWORD cbReturned = 0;
    memset(&Buf, 0, sizeof (Buf));
    pConInfo->ConnectionIndex = iPort;
    if (!DeviceIoControl(hHub, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
                                  pConInfo, sizeof (Buf),
                                  pConInfo, sizeof (Buf),
                                  &cbReturned, NULL))
    {
        DWORD dwErr = GetLastError(); NOREF(dwErr);
        LogRel(("Getting USB connection information failed (dwErr=%u) on hub %s\n", dwErr, lpcszHubName));
        AssertMsg(dwErr == ERROR_DEVICE_NOT_CONNECTED, (__FUNCTION__": DeviceIoControl failed (dwErr=%u)\n", dwErr));
        return VERR_GENERAL_FAILURE;
    }

    if (pConInfo->ConnectionStatus != DeviceConnected)
    {
        /* just ignore & return success */
        return VWRN_INVALID_HANDLE;
    }

    if (pConInfo->DeviceIsHub)
    {
        LPSTR lpszChildHubName = NULL;
        rc = usbLibDevStrHubNameGet(hHub, iPort, &lpszChildHubName);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            rc = usbLibDevGetHubDevices(lpszChildHubName, ppDevs, pcDevs);
            usbLibDevStrFree(lpszChildHubName);
            AssertRC(rc);
            return rc;
        }
        /* ignore this err */
        return VINF_SUCCESS;
    }

    bool fFreeNameBuf = true;
    char nameEmptyBuf = '\0';
    LPSTR lpszName = NULL;
    rc = usbLibDevStrDriverKeyGet(hHub, iPort, &lpszName);
    Assert(!!lpszName == !!RT_SUCCESS(rc));
    if (!lpszName)
    {
        LogRelFunc(("No DriverKey on hub %s port %d\n", lpcszHubName, iPort));
        lpszName = &nameEmptyBuf;
        fFreeNameBuf = false;
    }

    PUSB_CONFIGURATION_DESCRIPTOR pCfgDr = NULL;
    PVBOXUSB_STRING_DR_ENTRY pList = NULL;
    rc = usbLibDevCfgDrGet(hHub, lpcszHubName, iPort, 0, &pCfgDr);
    if (pCfgDr)
    {
        rc = usbLibDevStrDrEntryGetAll(hHub, lpcszHubName, iPort, &pConInfo->DeviceDescriptor, pCfgDr, &pList);
#ifdef VBOX_WITH_ANNOYING_USB_ASSERTIONS
        AssertRC(rc); // this can fail if device suspended
#endif
    }

    PUSBDEVICE pDev = (PUSBDEVICE)RTMemAllocZ(sizeof (*pDev));
    if (RT_LIKELY(pDev))
    {
        rc = usbLibDevPopulate(pDev, pConInfo, iPort, lpszName, lpcszHubName, pList);
        if (RT_SUCCESS(rc))
        {
            pDev->pNext = *ppDevs;
            *ppDevs = pDev;
            ++*pcDevs;
        }
        else
            RTMemFree(pDev);
    }
    else
        rc = VERR_NO_MEMORY;

    if (pCfgDr)
        usbLibDevCfgDrFree(pCfgDr);
    if (fFreeNameBuf)
    {
        Assert(lpszName);
        usbLibDevStrFree(lpszName);
    }
    if (pList)
        usbLibDevStrDrEntryFreeList(pList);

    return rc;
}

static int usbLibDevGetHubDevices(LPCSTR lpszName, PUSBDEVICE *ppDevs, uint32_t *pcDevs)
{
    LPSTR lpszDevName = (LPSTR)RTMemAllocZ(strlen(lpszName) + sizeof("\\\\.\\"));
    HANDLE hDev = INVALID_HANDLE_VALUE;
    Assert(lpszDevName);
    if (!lpszDevName)
    {
        AssertFailed();
        return VERR_OUT_OF_RESOURCES;
    }

    int rc = VINF_SUCCESS;
    strcpy(lpszDevName, "\\\\.\\");
    strcpy(lpszDevName + sizeof("\\\\.\\") - sizeof (lpszDevName[0]), lpszName);
    do
    {
        DWORD cbReturned = 0;
        hDev = CreateFile(lpszDevName, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hDev == INVALID_HANDLE_VALUE)
        {
            AssertFailed();
            break;
        }

        USB_NODE_INFORMATION NodeInfo;
        memset(&NodeInfo, 0, sizeof (NodeInfo));
        if (!DeviceIoControl(hDev, IOCTL_USB_GET_NODE_INFORMATION,
                            &NodeInfo, sizeof (NodeInfo),
                            &NodeInfo, sizeof (NodeInfo),
                            &cbReturned, NULL))
        {
            LogRel(("Getting USB node information failed (dwErr=%u) on hub %s\n", GetLastError(), lpszName));
            AssertFailed();
            break;
        }

        for (ULONG i = 1; i <= NodeInfo.u.HubInformation.HubDescriptor.bNumberOfPorts; ++i)
        {
            /* Just skip devices for which we failed to create the device structure. */
            usbLibDevGetHubPortDevices(hDev, lpszName, i, ppDevs, pcDevs);
        }
    } while (0);

    if (hDev != INVALID_HANDLE_VALUE)
        CloseHandle(hDev);

    RTMemFree(lpszDevName);

    return rc;
}
#endif

#ifdef VBOX_WITH_NEW_USB_ENUM

/* Get a registry property for a device given its HDEVINFO + SP_DEVINFO_DATA. */
static void *usbLibGetRegistryProperty(HDEVINFO InfoSet, const PSP_DEVINFO_DATA DevData, DWORD Property)
{
    BOOL    rc;
    DWORD   dwReqLen;
    void    *PropertyData;

    /* How large a buffer do we need? */
    rc = SetupDiGetDeviceRegistryProperty(InfoSet, DevData, Property,
                                          NULL, NULL, 0, &dwReqLen);
    if (!rc && (GetLastError() != ERROR_INSUFFICIENT_BUFFER))
    {
        LogRelFunc(("Failed to query buffer size, error %ld\n", GetLastError()));
        return NULL;
    }

    PropertyData = RTMemAlloc(dwReqLen);
    if (!PropertyData)
        return NULL;

    /* Get the actual property data. */
    rc = SetupDiGetDeviceRegistryProperty(InfoSet, DevData, Property,
                                          NULL, (PBYTE)PropertyData, dwReqLen, &dwReqLen);
    if (!rc)
    {
        LogRelFunc(("Failed to get property data, error %ld\n", GetLastError()));
        RTMemFree(PropertyData);
        return NULL;
    }
    return PropertyData;
}

/* Given a HDEVINFO and SP_DEVICE_INTERFACE_DATA, get the interface detail data and optionally device info data. */
static PSP_DEVICE_INTERFACE_DETAIL_DATA usbLibGetDevDetail(HDEVINFO InfoSet, PSP_DEVICE_INTERFACE_DATA InterfaceData, PSP_DEVINFO_DATA DevInfoData)
{
    BOOL                                rc;
    DWORD                               dwReqLen;
    PSP_DEVICE_INTERFACE_DETAIL_DATA    DetailData;

    rc = SetupDiGetDeviceInterfaceDetail(InfoSet, InterfaceData, NULL, 0, &dwReqLen, DevInfoData);
    if (!rc && (GetLastError() != ERROR_INSUFFICIENT_BUFFER))
    {
        LogRelFunc(("Failed to get interface detail size, error %ld\n", GetLastError()));
        return NULL;
    }

    DetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)RTMemAlloc(dwReqLen);
    if (!DetailData)
        return NULL;

    memset(DetailData, 0, dwReqLen);
    DetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    rc = SetupDiGetDeviceInterfaceDetail(InfoSet, InterfaceData, DetailData, dwReqLen, &dwReqLen, DevInfoData);
    if (!rc)
    {
        LogRelFunc(("Failed to get interface detail, error %ld\n", GetLastError()));
        RTMemFree(DetailData);
    }

    return DetailData;
}

/* Given a hub's PnP device instance, find its device path (file name). */
static LPCSTR usbLibGetHubPathFromInstanceID(LPCSTR InstanceID)
{
    HDEVINFO                            InfoSet;
    SP_DEVICE_INTERFACE_DATA            InterfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA    DetailData;
    BOOL                                rc;
    LPSTR                               DevicePath = NULL;

    /* Enumerate the DevInst's USB hub interface. */
    InfoSet = SetupDiGetClassDevs(&GUID_DEVINTERFACE_USB_HUB, InstanceID, NULL,
                                  DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (InfoSet == INVALID_HANDLE_VALUE)
    {
        LogRelFunc(("Failed to get interface for InstID %se, error %ld\n", InstanceID, GetLastError()));
        return NULL;
    }

    memset(&InterfaceData, 0, sizeof(InterfaceData));
    InterfaceData.cbSize = sizeof(InterfaceData);
    rc = SetupDiEnumDeviceInterfaces(InfoSet, 0, &GUID_DEVINTERFACE_USB_HUB, 0, &InterfaceData);
    if (!rc)
    {
        DWORD   dwErr = GetLastError();

        /* The parent device might not be a hub; that is valid, ignore such errors. */
        if (dwErr != ERROR_NO_MORE_ITEMS)
            LogRelFunc(("Failed to get interface data for InstID %s, error %ld\n", InstanceID, dwErr));
        SetupDiDestroyDeviceInfoList(InfoSet);
        return NULL;
    }

    DetailData = usbLibGetDevDetail(InfoSet, &InterfaceData, NULL);
    if (!DetailData)
    {
        SetupDiDestroyDeviceInfoList(InfoSet);
        return NULL;
    }

    /* Copy the device path out of the interface detail. */
    DevicePath = RTStrDup(DetailData->DevicePath);
    RTMemFree(DetailData);
    SetupDiDestroyDeviceInfoList(InfoSet);

    return DevicePath;
}


/* Use the Configuration Manager (CM) to get a devices's parent given its DEVINST and
 * turn it into a PnP device instance ID string.
 */
static LPCSTR usbLibGetParentInstanceID(DEVINST DevInst)
{
    LPSTR       InstanceID;
    DEVINST     ParentInst;
    ULONG       ulReqChars;
    ULONG       ulReqBytes;
    CONFIGRET   cr;

    /* First get the parent DEVINST. */
    cr = CM_Get_Parent(&ParentInst, DevInst, 0);
    if (cr != CR_SUCCESS)
    {
        LogRelFunc(("Failed to get parent instance, error %ld\n", GetLastError()));
        return NULL;
    }

    /* Then convert it to the instance ID string. */
    cr = CM_Get_Device_ID_Size(&ulReqChars, ParentInst, 0);
    if (cr != CR_SUCCESS)
    {
        LogRelFunc(("Failed to get device ID size (DevInst=%X), error %ld\n", DevInst, GetLastError()));
        return NULL;
    }

    /* CM_Get_Device_ID_Size gives us the size in characters without terminating null. */
    ulReqBytes = (ulReqChars + 1) * sizeof(char);
    InstanceID = (LPSTR)RTMemAlloc(ulReqBytes);
    if (!InstanceID)
        return NULL;

    cr = CM_Get_Device_ID(ParentInst, InstanceID, ulReqBytes, 0);
    if (cr != CR_SUCCESS)
    {
        LogRelFunc(("Failed to get device ID (DevInst=%X), error %ld\n", DevInst, GetLastError()));
        RTMemFree(InstanceID);
        return NULL;
    }

    return InstanceID;
}

/* Process a single USB device that's being enumerated and grab its hub-specific data. */
static int usbLibDevGetDevice(LPCSTR lpcszHubFile, ULONG iPort, LPCSTR lpcszLocation, LPCSTR lpcszDriverKey, PUSBDEVICE *ppDevs, uint32_t *pcDevs)
{
    HANDLE                                  HubDevice;
    BYTE                                    abConBuf[sizeof(USB_NODE_CONNECTION_INFORMATION_EX)];
    PUSB_NODE_CONNECTION_INFORMATION_EX     pConInfo = PUSB_NODE_CONNECTION_INFORMATION_EX(abConBuf);
    int                                     rc = VINF_SUCCESS;
    DWORD                                   cbReturned = 0;

    /* Validate inputs. */
    if ((iPort < 1) || (iPort > 255))
    {
        LogRelFunc(("Port index out of range (%u)\n", iPort));
        return VERR_INVALID_PARAMETER;
    }
    if (!lpcszHubFile)
    {
        LogRelFunc(("Hub path is NULL!\n"));
        return VERR_INVALID_PARAMETER;
    }
    if (!lpcszLocation)
    {
        LogRelFunc(("Location NULL!\n"));
        return VERR_INVALID_PARAMETER;
    }

    /* Try opening the hub file so we can send IOCTLs to it. */
    HubDevice = CreateFile(lpcszHubFile, GENERIC_WRITE, FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (HubDevice == INVALID_HANDLE_VALUE)
    {
        LogRelFunc(("Failed to open hub `%s' (dwErr=%u)\n", lpcszHubFile, GetLastError()));
        return VERR_FILE_NOT_FOUND;
    }

    /* The shenanigans with abConBuf are due to USB_NODE_CONNECTION_INFORMATION_EX
     * containing a zero-sized array, triggering compiler warnings.
     */
    memset(pConInfo, 0, sizeof(abConBuf));
    pConInfo->ConnectionIndex = iPort;

    /* We expect that IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX is always available
     * on any supported Windows version and hardware.
     * NB: IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2 is Win8 and later only.
     */
    if (!DeviceIoControl(HubDevice, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
                         pConInfo, sizeof(abConBuf), pConInfo, sizeof(abConBuf),
                         &cbReturned, NULL))
    {
        DWORD dwErr = GetLastError(); NOREF(dwErr);
        LogRel(("IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX failed (dwErr=%u) on hub %s, port %d\n", dwErr, lpcszHubFile, iPort));
        AssertMsg(dwErr == ERROR_DEVICE_NOT_CONNECTED, (__FUNCTION__": DeviceIoControl failed dwErr (%u)\n", dwErr));
        CloseHandle(HubDevice);
        return VERR_GENERAL_FAILURE;
    }

    if (pConInfo->ConnectionStatus != DeviceConnected)
    {
        /* Ignore this, can't do anything with it. */
        LogFunc(("Device is not connected, skipping.\n"));
        CloseHandle(HubDevice);
        return VINF_SUCCESS;
    }

    if (pConInfo->DeviceIsHub)
    {
        /* We're ignoring hubs, just skip this. */
        LogFunc(("Device is a hub, skipping.\n"));
        CloseHandle(HubDevice);
        return VINF_SUCCESS;
    }

    PUSB_CONFIGURATION_DESCRIPTOR pCfgDr = NULL;
    PVBOXUSB_STRING_DR_ENTRY pList = NULL;
    rc = usbLibDevCfgDrGet(HubDevice, lpcszHubFile, iPort, 0, &pCfgDr);
    if (pCfgDr)
    {
        rc = usbLibDevStrDrEntryGetAll(HubDevice, lpcszHubFile, iPort, &pConInfo->DeviceDescriptor, pCfgDr, &pList);
#ifdef VBOX_WITH_ANNOYING_USB_ASSERTIONS
        AssertRC(rc); // this can fail if device suspended
#endif
    }

    /* At this point we're done with the hub device. */
    CloseHandle(HubDevice);

    PUSBDEVICE pDev = (PUSBDEVICE)RTMemAllocZ(sizeof (*pDev));
    if (RT_LIKELY(pDev))
    {
        rc = usbLibDevPopulate(pDev, pConInfo, iPort, lpcszLocation, lpcszDriverKey, lpcszHubFile, pList);
        if (RT_SUCCESS(rc))
        {
            pDev->pNext = *ppDevs;
            *ppDevs = pDev;
            ++*pcDevs;
        }
        else
            RTMemFree(pDev);
    }
    else
        rc = VERR_NO_MEMORY;

    if (pCfgDr)
        usbLibDevCfgDrFree(pCfgDr);
    if (pList)
        usbLibDevStrDrEntryFreeList(pList);

    return rc;
}


/*
 * Enumerate the USB devices in the host system. Since we do not care about the hierarchical
 * structure of root hubs, other hubs, and devices, we just ask the USB PnP enumerator to
 * give us all it has. This includes hubs (though not root hubs), as well as multiple child
 * interfaces of multi-interface USB devices, which we filter out. It also includes USB
 * devices with no driver, which is notably something we cannot get by enumerating via
 * GUID_DEVINTERFACE_USB_DEVICE.
 *
 * This approach also saves us some trouble relative to enumerating devices via hub IOCTLs and
 * then hunting through the PnP manager to find them. Instead, we look up the device's parent
 * which (for devices we're interested in) is always a hub, and that allows us to obtain
 * USB-specific data (descriptors, speeds, etc.) when combined with the devices PnP "address"
 * (USB port on parent hub).
 *
 * NB: Every USB device known to the Windows PnP Manager will have a device instance ID. Typically
 * it also has a DriverKey but only if it has a driver installed. Hence we ignore the DriverKey, at
 * least prior to capturing (once VBoxUSB.sys is installed, a DriverKey must by definition be
 * present). Also note that the device instance ID changes for captured devices since we change
 * their USB VID/PID, though it is unique at any given point.
 *
 * The location information should be a reliable way of identifying a device and does not change
 * with driver installs, capturing, etc. USB device location information is only available on
 * Windows Vista and later; earlier Windows version had no reliable way of cross-referencing the
 * USB IOCTL and PnP Manager data.
 */
static int usbLibEnumDevices(PUSBDEVICE *ppDevs, uint32_t *pcDevs)
{
    HDEVINFO            InfoSet;
    DWORD               DeviceIndex;
    LPDWORD             Address;
    SP_DEVINFO_DATA     DeviceData;
    LPCSTR              ParentInstID;
    LPCSTR              HubPath = NULL;
    LPCSTR              Location;
    LPCSTR              DriverKey;

    /* Ask the USB PnP enumerator for all it has. */
    InfoSet = SetupDiGetClassDevs(NULL, "USB", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);

    memset(&DeviceData, 0, sizeof(DeviceData));
    DeviceData.cbSize = sizeof(DeviceData);
    DeviceIndex = 0;

    /* Enumerate everything in the info set. */
    while (SetupDiEnumDeviceInfo(InfoSet, DeviceIndex, &DeviceData))
    {
        /* Use the CM API to get the parent instance ID. */
        ParentInstID = usbLibGetParentInstanceID(DeviceData.DevInst);

        /* Now figure out the hub's file path fron the instance ID, if there is one. */
        if (ParentInstID)
            HubPath = usbLibGetHubPathFromInstanceID(ParentInstID);

        /* If there's no hub interface on the parent, then this might be a child
         * device of a multi-interface device. Either way, we're not interested.
         */
        if (HubPath)
        {
            /* The location information uniquely identifies the USB device, (hub/port). */
            Location = (LPCSTR)usbLibGetRegistryProperty(InfoSet, &DeviceData, SPDRP_LOCATION_PATHS);

            /* The software key aka DriverKey. This will be NULL for devices with no driver
             * and allows us to distinguish between 'busy' (driver installed) and 'available'
             * (no driver) devices.
             */
            DriverKey = (LPCSTR)usbLibGetRegistryProperty(InfoSet, &DeviceData, SPDRP_DRIVER);

            /* The device's PnP Manager "address" is the port number on the parent hub. */
            Address = (LPDWORD)usbLibGetRegistryProperty(InfoSet, &DeviceData, SPDRP_ADDRESS);
            if (Address && Location)    /* NB: DriverKey may be NULL! */
            {
                usbLibDevGetDevice(HubPath, *Address, Location, DriverKey, ppDevs, pcDevs);
            }
            RTMemFree((void *)HubPath);

            if (Location)
                RTMemFree((void *)Location);
            if (DriverKey)
                RTMemFree((void *)DriverKey);
            if (Address)
                RTMemFree((void *)Address);
        }

        /* Clean up after this device. */
        if (ParentInstID)
            RTMemFree((void *)ParentInstID);

        ++DeviceIndex;
        memset(&DeviceData, 0, sizeof(DeviceData));
        DeviceData.cbSize = sizeof(DeviceData);
    }

    if (InfoSet)
        SetupDiDestroyDeviceInfoList(InfoSet);

    return VINF_SUCCESS;
}

#else
static int usbLibDevGetDevices(PUSBDEVICE *ppDevs, uint32_t *pcDevs)
{
    char CtlName[16];
    int rc = VINF_SUCCESS;

    for (int i = 0; i < 10; ++i)
    {
        RTStrPrintf(CtlName, sizeof(CtlName), "\\\\.\\HCD%d", i);
        HANDLE hCtl = CreateFile(CtlName, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hCtl != INVALID_HANDLE_VALUE)
        {
            char* lpszName;
            rc = usbLibDevStrRootHubNameGet(hCtl, &lpszName);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                rc = usbLibDevGetHubDevices(lpszName, ppDevs, pcDevs);
                AssertRC(rc);
                usbLibDevStrFree(lpszName);
            }
            CloseHandle(hCtl);
            if (RT_FAILURE(rc))
                break;
        }
    }
    return VINF_SUCCESS;
}
#endif

static int usbLibMonDevicesCmp(PUSBDEVICE pDev, PVBOXUSB_DEV pDevInfo)
{
    int iDiff;
    iDiff = strcmp(pDev->pszAddress, pDevInfo->szDriverRegName);
    return iDiff;
}

static int usbLibMonDevicesUpdate(PVBOXUSBGLOBALSTATE pGlobal, PUSBDEVICE pDevs, PVBOXUSB_DEV pDevInfos)
{

    PUSBDEVICE pDevsHead = pDevs;
    for (; pDevInfos; pDevInfos = pDevInfos->pNext)
    {
        for (pDevs = pDevsHead; pDevs; pDevs = pDevs->pNext)
        {
            if (usbLibMonDevicesCmp(pDevs, pDevInfos))
                continue;

            if (!pDevInfos->szDriverRegName[0])
            {
                AssertFailed();
                break;
            }

            USBSUP_GETDEV Dev = {0};
            HANDLE hDev = CreateFile(pDevInfos->szName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL,
                                                          OPEN_EXISTING,  FILE_ATTRIBUTE_SYSTEM, NULL);
            if (hDev == INVALID_HANDLE_VALUE)
            {
                AssertFailed();
                break;
            }

            DWORD cbReturned = 0;
            if (!DeviceIoControl(hDev, SUPUSB_IOCTL_GET_DEVICE, &Dev, sizeof (Dev), &Dev, sizeof (Dev), &cbReturned, NULL))
            {
                DWORD dwErr = GetLastError();
#ifdef VBOX_WITH_ANNOYING_USB_ASSERTIONS
                /* ERROR_DEVICE_NOT_CONNECTED -> device was removed just now */
                AsserFailed();
#endif
                LogRelFunc(("SUPUSB_IOCTL_GET_DEVICE failed on '%s' (dwErr=%u)!\n", pDevInfos->szName, dwErr));
                CloseHandle(hDev);
                break;
            }

            /* we must not close the handle until we request for the device state from the monitor to ensure
             * the device handle returned by the device driver does not disappear */
            Assert(Dev.hDevice);
            USBSUP_GETDEV_MON MonInfo;
            HVBOXUSBDEVUSR hDevice = Dev.hDevice;
            if (!DeviceIoControl(pGlobal->hMonitor, SUPUSBFLT_IOCTL_GET_DEVICE, &hDevice, sizeof (hDevice), &MonInfo, sizeof (MonInfo), &cbReturned, NULL))
            {
                DWORD dwErr = GetLastError();
                /* ERROR_DEVICE_NOT_CONNECTED -> device was removed just now */
                AssertFailed();
                LogRelFunc(("SUPUSBFLT_IOCTL_GET_DEVICE failed for '%s' (hDevice=%p, dwErr=%u)!\n", pDevInfos->szName, hDevice, dwErr));
                CloseHandle(hDev);
                break;
            }

            CloseHandle(hDev);

            /* success!! update device info */
            /* ensure the state returned is valid */
            Assert(    MonInfo.enmState == USBDEVICESTATE_USED_BY_HOST
                    || MonInfo.enmState == USBDEVICESTATE_USED_BY_HOST_CAPTURABLE
                    || MonInfo.enmState == USBDEVICESTATE_UNUSED
                    || MonInfo.enmState == USBDEVICESTATE_HELD_BY_PROXY
                    || MonInfo.enmState == USBDEVICESTATE_USED_BY_GUEST);
            pDevs->enmState = MonInfo.enmState;

            if (pDevs->enmState != USBDEVICESTATE_USED_BY_HOST)
            {
                /* only set the interface name if device can be grabbed */
                RTStrFree(pDevs->pszAltAddress);
                pDevs->pszAltAddress = (char*)pDevs->pszAddress;
                pDevs->pszAddress = RTStrDup(pDevInfos->szName);
            }
#ifdef VBOX_WITH_ANNOYING_USB_ASSERTIONS
            else
            {
                /* dbg breakpoint */
                AssertFailed();
            }
#endif

            /* we've found the device, break in any way */
            break;
        }
    }

    return VINF_SUCCESS;
}

static int usbLibGetDevices(PVBOXUSBGLOBALSTATE pGlobal, PUSBDEVICE *ppDevs, uint32_t *pcDevs)
{
    *ppDevs = NULL;
    *pcDevs = 0;

    LogRelFunc(("Starting USB device enumeration\n"));
#ifdef VBOX_WITH_NEW_USB_ENUM
    int rc = usbLibEnumDevices(ppDevs, pcDevs);
#else
    int rc = usbLibDevGetDevices(ppDevs, pcDevs);
#endif
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        PVBOXUSB_DEV pDevInfos = NULL;
        uint32_t cDevInfos = 0;
#ifdef VBOX_WITH_NEW_USB_ENUM
        rc = usbLibEnumVUsbDevices(&pDevInfos, &cDevInfos);
#else
        rc = usbLibVuGetDevices(&pDevInfos, &cDevInfos);
#endif
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            rc = usbLibMonDevicesUpdate(pGlobal, *ppDevs, pDevInfos);
            AssertRC(rc);
            usbLibVuFreeDevices(pDevInfos);
        }

        LogRelFunc(("Found %u USB devices, %u captured\n", *pcDevs, cDevInfos));
        return VINF_SUCCESS;
    }
    return rc;
}

AssertCompile(INFINITE == RT_INDEFINITE_WAIT);
static int usbLibStateWaitChange(PVBOXUSBGLOBALSTATE pGlobal, RTMSINTERVAL cMillies)
{
    HANDLE ahEvents[] = {pGlobal->hNotifyEvent, pGlobal->hInterruptEvent};
    DWORD dwResult = WaitForMultipleObjects(RT_ELEMENTS(ahEvents), ahEvents,
                        FALSE, /* BOOL bWaitAll */
                        cMillies);

    switch (dwResult)
    {
        case WAIT_OBJECT_0:
            return VINF_SUCCESS;
        case WAIT_OBJECT_0 + 1:
            return VERR_INTERRUPTED;
        case WAIT_TIMEOUT:
            return VERR_TIMEOUT;
        default:
        {
            DWORD dwErr = GetLastError(); NOREF(dwErr);
            AssertMsgFailed(("WaitForMultipleObjects failed, dwErr (%u)\n", dwErr));
            return VERR_GENERAL_FAILURE;
        }
    }
}

AssertCompile(RT_INDEFINITE_WAIT == INFINITE);
AssertCompile(sizeof (RTMSINTERVAL) == sizeof (DWORD));
USBLIB_DECL(int) USBLibWaitChange(RTMSINTERVAL msWaitTimeout)
{
    return usbLibStateWaitChange(&g_VBoxUsbGlobal, msWaitTimeout);
}

static int usbLibInterruptWaitChange(PVBOXUSBGLOBALSTATE pGlobal)
{
    BOOL fRc = SetEvent(pGlobal->hInterruptEvent);
    if (!fRc)
    {
        DWORD dwErr = GetLastError(); NOREF(dwErr);
        AssertMsgFailed(("SetEvent failed, dwErr (%u)\n", dwErr));
        return VERR_GENERAL_FAILURE;
    }
    return VINF_SUCCESS;
}

USBLIB_DECL(int) USBLibInterruptWaitChange(void)
{
    return usbLibInterruptWaitChange(&g_VBoxUsbGlobal);
}

/*
USBLIB_DECL(bool) USBLibHasPendingDeviceChanges(void)
{
    int rc = USBLibWaitChange(0);
    return rc == VINF_SUCCESS;
}
*/

USBLIB_DECL(int) USBLibGetDevices(PUSBDEVICE *ppDevices, uint32_t *pcbNumDevices)
{
    Assert(g_VBoxUsbGlobal.hMonitor != INVALID_HANDLE_VALUE);
    return usbLibGetDevices(&g_VBoxUsbGlobal, ppDevices, pcbNumDevices);
}

USBLIB_DECL(void *) USBLibAddFilter(PCUSBFILTER pFilter)
{
    USBSUP_FLTADDOUT FltAddRc;
    DWORD cbReturned = 0;

    if (g_VBoxUsbGlobal.hMonitor == INVALID_HANDLE_VALUE)
    {
#ifdef VBOX_WITH_ANNOYING_USB_ASSERTIONS
        AssertFailed();
#endif
        return NULL;
    }

    Log(("usblibInsertFilter: Manufacturer=%s Product=%s Serial=%s\n",
         USBFilterGetString(pFilter, USBFILTERIDX_MANUFACTURER_STR)  ? USBFilterGetString(pFilter, USBFILTERIDX_MANUFACTURER_STR)  : "<null>",
         USBFilterGetString(pFilter, USBFILTERIDX_PRODUCT_STR)       ? USBFilterGetString(pFilter, USBFILTERIDX_PRODUCT_STR)       : "<null>",
         USBFilterGetString(pFilter, USBFILTERIDX_SERIAL_NUMBER_STR) ? USBFilterGetString(pFilter, USBFILTERIDX_SERIAL_NUMBER_STR) : "<null>"));

    if (!DeviceIoControl(g_VBoxUsbGlobal.hMonitor, SUPUSBFLT_IOCTL_ADD_FILTER,
                (LPVOID)pFilter, sizeof(*pFilter),
                &FltAddRc, sizeof(FltAddRc),
                &cbReturned, NULL))
    {
        DWORD dwErr = GetLastError();
        AssertFailed();
        LogRelFunc(("SUPUSBFLT_IOCTL_ADD_FILTER failed (dwErr=%u)!\n", dwErr));
        return NULL;
    }

    if (RT_FAILURE(FltAddRc.rc))
    {
        AssertFailed();
        LogRelFunc(("Adding a USB filter failed with rc=%d!\n", FltAddRc.rc));
        return NULL;
    }

    LogRel(("Added USB filter (ID=%u, type=%d) for device %04X:%04X rev %04X, c/s/p %02X/%02X/%02X, Manufacturer=`%s' Product=`%s' Serial=`%s'\n", FltAddRc.uId, USBFilterGetFilterType(pFilter),
            USBFilterGetNum(pFilter, USBFILTERIDX_VENDOR_ID), USBFilterGetNum(pFilter, USBFILTERIDX_PRODUCT_ID), USBFilterGetNum(pFilter, USBFILTERIDX_DEVICE_REV),
            USBFilterGetNum(pFilter, USBFILTERIDX_DEVICE_CLASS), USBFilterGetNum(pFilter, USBFILTERIDX_DEVICE_SUB_CLASS), USBFilterGetNum(pFilter, USBFILTERIDX_DEVICE_PROTOCOL),
            USBFilterGetString(pFilter, USBFILTERIDX_MANUFACTURER_STR)  ? USBFilterGetString(pFilter, USBFILTERIDX_MANUFACTURER_STR)  : "<null>",
            USBFilterGetString(pFilter, USBFILTERIDX_PRODUCT_STR)       ? USBFilterGetString(pFilter, USBFILTERIDX_PRODUCT_STR)       : "<null>",
         USBFilterGetString(pFilter, USBFILTERIDX_SERIAL_NUMBER_STR) ? USBFilterGetString(pFilter, USBFILTERIDX_SERIAL_NUMBER_STR) : "<null>"));

    return (void *)FltAddRc.uId;
}


USBLIB_DECL(void) USBLibRemoveFilter(void *pvId)
{
    uintptr_t uId;
    DWORD cbReturned = 0;

    if (g_VBoxUsbGlobal.hMonitor == INVALID_HANDLE_VALUE)
    {
#ifdef VBOX_WITH_ANNOYING_USB_ASSERTIONS
        AssertFailed();
#endif
        return;
    }

    Log(("usblibRemoveFilter %p\n", pvId));

    uId = (uintptr_t)pvId;
    if (!DeviceIoControl(g_VBoxUsbGlobal.hMonitor, SUPUSBFLT_IOCTL_REMOVE_FILTER, &uId, sizeof(uId),  NULL, 0,&cbReturned, NULL))
    {
        DWORD dwErr = GetLastError();
        AssertFailed();
        LogRelFunc(("SUPUSBFLT_IOCTL_REMOVE_FILTER failed (dwErr=%u)!\n", dwErr));
    }
    else
        LogRel(("Removed USB filter ID=%u\n", uId));
}

USBLIB_DECL(int) USBLibRunFilters(void)
{
    DWORD cbReturned = 0;

    Assert(g_VBoxUsbGlobal.hMonitor != INVALID_HANDLE_VALUE);

    if (!DeviceIoControl(g_VBoxUsbGlobal.hMonitor, SUPUSBFLT_IOCTL_RUN_FILTERS,
                NULL, 0,
                NULL, 0,
                &cbReturned, NULL))
    {
        DWORD dwErr = GetLastError();
        AssertFailed();
        LogRelFunc(("SUPUSBFLT_IOCTL_RUN_FILTERS failed (dwErr=%u)!\n", dwErr));
        return RTErrConvertFromWin32(dwErr);
    }

    return VINF_SUCCESS;
}


static VOID CALLBACK usbLibTimerCallback(__in PVOID lpParameter, __in BOOLEAN TimerOrWaitFired) RT_NOTHROW_DEF
{
    RT_NOREF2(lpParameter, TimerOrWaitFired);
    SetEvent(g_VBoxUsbGlobal.hNotifyEvent);
}

static void usbLibOnDeviceChange(void)
{
    /* we're getting series of events like that especially on device re-attach
     * (i.e. first for device detach and then for device attach)
     * unfortunately the event does not tell us what actually happened.
     * To avoid extra notifications, we delay the SetEvent via a timer
     * and update the timer if additional notification comes before the timer fires
     * */
    if (g_VBoxUsbGlobal.hTimer)
    {
        if (!DeleteTimerQueueTimer(g_VBoxUsbGlobal.hTimerQueue, g_VBoxUsbGlobal.hTimer, NULL))
        {
            DWORD dwErr = GetLastError(); NOREF(dwErr);
            AssertMsg(dwErr == ERROR_IO_PENDING, ("DeleteTimerQueueTimer failed, dwErr (%u)\n", dwErr));
        }
    }

    if (!CreateTimerQueueTimer(&g_VBoxUsbGlobal.hTimer, g_VBoxUsbGlobal.hTimerQueue,
                               usbLibTimerCallback,
                               NULL,
                               500, /* ms*/
                               0,
                               WT_EXECUTEONLYONCE))
    {
        DWORD dwErr = GetLastError(); NOREF(dwErr);
        AssertMsgFailed(("CreateTimerQueueTimer failed, dwErr (%u)\n", dwErr));

        /* call it directly */
        usbLibTimerCallback(NULL, FALSE);
    }
}

static LRESULT CALLBACK usbLibWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_DEVICECHANGE:
            if (wParam == DBT_DEVNODES_CHANGED)
            {
                /* we notify change any device arivals/removals on the system
                 * and let the client decide whether the usb change actually happened
                 * so far this is more clean than reporting events from the Monitor
                 * because monitor sees only PDO arrivals/removals,
                 * and by the time PDO is created, device can not
                 * be yet started and fully functional,
                 * so usblib won't be able to pick it up
                 * */

                usbLibOnDeviceChange();
            }
            break;
         case WM_DESTROY:
            {
                PostQuitMessage(0);
                return 0;
            }
    }
    return DefWindowProc (hwnd, uMsg, wParam, lParam);
}

/** @todo r=bird: Use an IPRT thread!! */
static DWORD WINAPI usbLibMsgThreadProc(__in LPVOID lpParameter) RT_NOTHROW_DEF
{
    static LPCSTR   s_szVBoxUsbWndClassName = "VBoxUsbLibClass";
    const HINSTANCE hInstance               = (HINSTANCE)GetModuleHandle(NULL);
    RT_NOREF1(lpParameter);

    Assert(g_VBoxUsbGlobal.hWnd == NULL);
    g_VBoxUsbGlobal.hWnd = NULL;

    /*
     * Register the Window Class and create the hidden window.
     */
    WNDCLASS wc;
    wc.style         = 0;
    wc.lpfnWndProc   = usbLibWndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = sizeof(void *);
    wc.hInstance     = hInstance;
    wc.hIcon         = NULL;
    wc.hCursor       = NULL;
    wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND + 1);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = s_szVBoxUsbWndClassName;
    ATOM atomWindowClass = RegisterClass(&wc);
    if (atomWindowClass != 0)
        g_VBoxUsbGlobal.hWnd = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
                                              s_szVBoxUsbWndClassName, s_szVBoxUsbWndClassName,
                                              WS_POPUPWINDOW,
                                              -200, -200, 100, 100, NULL, NULL, hInstance, NULL);
    else
        AssertMsgFailed(("RegisterClass failed, last error %u\n", GetLastError()));

    /*
     * Signal the creator thread.
     */
    ASMCompilerBarrier();
    SetEvent(g_VBoxUsbGlobal.hNotifyEvent);

    if (g_VBoxUsbGlobal.hWnd)
    {
        /* Make sure it's really hidden. */
        SetWindowPos(g_VBoxUsbGlobal.hWnd, HWND_TOPMOST, -200, -200, 0, 0,
                     SWP_NOACTIVATE | SWP_HIDEWINDOW | SWP_NOCOPYBITS | SWP_NOREDRAW | SWP_NOSIZE);

        /*
         * The message pump.
         */
        MSG msg;
        BOOL fRet;
        while ((fRet = GetMessage(&msg, NULL, 0, 0)) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Assert(fRet >= 0);
    }

    if (atomWindowClass != NULL)
        UnregisterClass(s_szVBoxUsbWndClassName, hInstance);

    return 0;
}


/**
 * Initialize the USB library
 *
 * @returns VBox status code.
 */
USBLIB_DECL(int) USBLibInit(void)
{
    int rc = VERR_GENERAL_FAILURE;

    Log(("usbproxy: usbLibInit\n"));

    RT_ZERO(g_VBoxUsbGlobal);
    g_VBoxUsbGlobal.hMonitor = INVALID_HANDLE_VALUE;

    /*
     * Create the notification and interrupt event before opening the device.
     */
    g_VBoxUsbGlobal.hNotifyEvent = CreateEvent(NULL,  /* LPSECURITY_ATTRIBUTES lpEventAttributes */
                                               FALSE, /* BOOL bManualReset */
                                               FALSE, /* set to false since it will be initially used for notification thread startup sync */
                                               NULL   /* LPCTSTR lpName */);
    if (g_VBoxUsbGlobal.hNotifyEvent)
    {
        g_VBoxUsbGlobal.hInterruptEvent = CreateEvent(NULL,  /* LPSECURITY_ATTRIBUTES lpEventAttributes */
                                                      FALSE, /* BOOL bManualReset */
                                                      FALSE, /* BOOL bInitialState */
                                                      NULL   /* LPCTSTR lpName */);
        if (g_VBoxUsbGlobal.hInterruptEvent)
        {
            /*
             * Open the USB monitor device, starting if needed.
             */
            g_VBoxUsbGlobal.hMonitor = CreateFile(USBMON_DEVICE_NAME,
                                                  GENERIC_READ | GENERIC_WRITE,
                                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                  NULL,
                                                  OPEN_EXISTING,
                                                  FILE_ATTRIBUTE_SYSTEM,
                                                  NULL);

            if (g_VBoxUsbGlobal.hMonitor == INVALID_HANDLE_VALUE)
            {
                HRESULT hr = VBoxDrvCfgSvcStart(USBMON_SERVICE_NAME_W);
                if (hr == S_OK)
                {
                    g_VBoxUsbGlobal.hMonitor = CreateFile(USBMON_DEVICE_NAME,
                                                          GENERIC_READ | GENERIC_WRITE,
                                                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                          NULL,
                                                          OPEN_EXISTING,
                                                          FILE_ATTRIBUTE_SYSTEM,
                                                          NULL);
                    if (g_VBoxUsbGlobal.hMonitor == INVALID_HANDLE_VALUE)
                    {
                        DWORD dwErr = GetLastError();
                        LogRelFunc(("CreateFile failed (dwErr=%u) for `%s'\n", dwErr, USBMON_DEVICE_NAME));
                        rc = VERR_FILE_NOT_FOUND;
                    }
                }
            }

            if (g_VBoxUsbGlobal.hMonitor != INVALID_HANDLE_VALUE)
            {
                /*
                 * Check the USB monitor version.
                 *
                 * Drivers are backwards compatible within the same major
                 * number.  We consider the minor version number this library
                 * is compiled with to be the minimum required by the driver.
                 * This is by reasoning that the library uses the full feature
                 * set of the driver it's written for.
                 */
                USBSUP_VERSION  Version = {0};
                DWORD           cbReturned = 0;
                if (DeviceIoControl(g_VBoxUsbGlobal.hMonitor, SUPUSBFLT_IOCTL_GET_VERSION,
                                    NULL, 0,
                                    &Version, sizeof (Version),
                                    &cbReturned, NULL))
                {
                    if (   Version.u32Major == USBMON_MAJOR_VERSION
#if USBMON_MINOR_VERSION != 0
                        && Version.u32Minor >= USBMON_MINOR_VERSION
#endif
                        )
                    {
                        /*
                         * We can not use USB Mon for reliable device add/remove tracking
                         * since once USB Mon is notified about PDO creation and/or IRP_MN_START_DEVICE,
                         * the function device driver may still do some initialization, which might result in
                         * notifying too early.
                         * Instead we use WM_DEVICECHANGE + DBT_DEVNODES_CHANGED to make Windows notify us about
                         * device arivals/removals.
                         * Since WM_DEVICECHANGE is a window message, create a dedicated thread to be used for WndProc and stuff.
                         * The thread would create a window, track windows messages and call usbLibOnDeviceChange on WM_DEVICECHANGE arrival.
                         * See comments in usbLibOnDeviceChange function for detail about using the timer queue.
                         */
                        g_VBoxUsbGlobal.hTimerQueue = CreateTimerQueue();
                        if (g_VBoxUsbGlobal.hTimerQueue)
                        {
/** @todo r=bird: Which lunatic used CreateThread here?!?
 *  Only the CRT uses CreateThread. */
                            g_VBoxUsbGlobal.hThread = CreateThread(
                              NULL, /*__in_opt   LPSECURITY_ATTRIBUTES lpThreadAttributes, */
                              0, /*__in       SIZE_T dwStackSize, */
                              usbLibMsgThreadProc, /*__in       LPTHREAD_START_ROUTINE lpStartAddress,*/
                              NULL, /*__in_opt   LPVOID lpParameter,*/
                              0, /*__in       DWORD dwCreationFlags,*/
                              NULL /*__out_opt  LPDWORD lpThreadId*/
                            );
                            if (g_VBoxUsbGlobal.hThread)
                            {
                                DWORD dwResult = WaitForSingleObject(g_VBoxUsbGlobal.hNotifyEvent, INFINITE);
                                Assert(dwResult == WAIT_OBJECT_0);
                                if (g_VBoxUsbGlobal.hWnd)
                                {
                                    /*
                                     * We're DONE!
                                     *
                                     * Just ensure that the event is set so the
                                     * first "wait change" request is processed.
                                     */
                                    SetEvent(g_VBoxUsbGlobal.hNotifyEvent);
                                    return VINF_SUCCESS;
                                }

                                dwResult = WaitForSingleObject(g_VBoxUsbGlobal.hThread, INFINITE);
                                Assert(dwResult == WAIT_OBJECT_0);
                                BOOL fRc = CloseHandle(g_VBoxUsbGlobal.hThread); NOREF(fRc);
                                DWORD dwErr = GetLastError(); NOREF(dwErr);
                                AssertMsg(fRc, ("CloseHandle for hThread failed (dwErr=%u)\n", dwErr));
                                g_VBoxUsbGlobal.hThread = INVALID_HANDLE_VALUE;
                            }
                            else
                            {
                                DWORD dwErr = GetLastError(); NOREF(dwErr);
                                AssertMsgFailed(("CreateThread failed, (dwErr=%u)\n", dwErr));
                                rc = VERR_GENERAL_FAILURE;
                            }

                            DeleteTimerQueueEx(g_VBoxUsbGlobal.hTimerQueue, INVALID_HANDLE_VALUE /* see term */);
                            g_VBoxUsbGlobal.hTimerQueue = NULL;
                        }
                        else
                        {
                            DWORD dwErr = GetLastError(); NOREF(dwErr);
                            AssertMsgFailed(("CreateTimerQueue failed (dwErr=%u)\n", dwErr));
                        }
                    }
                    else
                    {
                        LogRelFunc(("USB Monitor driver version mismatch! driver=%u.%u library=%u.%u\n",
                                Version.u32Major, Version.u32Minor, USBMON_MAJOR_VERSION, USBMON_MINOR_VERSION));
#ifdef VBOX_WITH_ANNOYING_USB_ASSERTIONS
                        AssertFailed();
#endif
                        rc = VERR_VERSION_MISMATCH;
                    }
                }
                else
                {
                    DWORD dwErr = GetLastError(); NOREF(dwErr);
                    LogRelFunc(("SUPUSBFLT_IOCTL_GET_VERSION failed (dwErr=%u)\n", dwErr));
                    AssertFailed();
                    rc = VERR_VERSION_MISMATCH;
                }

                CloseHandle(g_VBoxUsbGlobal.hMonitor);
                g_VBoxUsbGlobal.hMonitor = INVALID_HANDLE_VALUE;
            }
            else
            {
                LogRelFunc(("USB Service not found\n"));
#ifdef VBOX_WITH_ANNOYING_USB_ASSERTIONS
                AssertFailed();
#endif
                rc = VERR_FILE_NOT_FOUND;
            }

            CloseHandle(g_VBoxUsbGlobal.hInterruptEvent);
            g_VBoxUsbGlobal.hInterruptEvent = NULL;
        }
        else
        {
            AssertMsgFailed(("CreateEvent for InterruptEvent failed (dwErr=%u)\n", GetLastError()));
            rc = VERR_GENERAL_FAILURE;
        }

        CloseHandle(g_VBoxUsbGlobal.hNotifyEvent);
        g_VBoxUsbGlobal.hNotifyEvent = NULL;
    }
    else
    {
        AssertMsgFailed(("CreateEvent for NotifyEvent failed (dwErr=%u)\n", GetLastError()));
        rc = VERR_GENERAL_FAILURE;
    }

    /* since main calls us even if USBLibInit fails,
     * we use hMonitor == INVALID_HANDLE_VALUE as a marker to indicate whether the lib is inited */

    Assert(RT_FAILURE(rc));
    return rc;
}


/**
 * Terminate the USB library
 *
 * @returns VBox status code.
 */
USBLIB_DECL(int) USBLibTerm(void)
{
    if (g_VBoxUsbGlobal.hMonitor == INVALID_HANDLE_VALUE)
    {
        Assert(g_VBoxUsbGlobal.hInterruptEvent == NULL);
        Assert(g_VBoxUsbGlobal.hNotifyEvent == NULL);
        return VINF_SUCCESS;
    }

    BOOL fRc;
    fRc = PostMessage(g_VBoxUsbGlobal.hWnd, WM_CLOSE, 0, 0);
    AssertMsg(fRc, ("PostMessage for hWnd failed (dwErr=%u)\n", GetLastError()));

    if (g_VBoxUsbGlobal.hThread != NULL)
    {
        DWORD dwResult = WaitForSingleObject(g_VBoxUsbGlobal.hThread, INFINITE);
        Assert(dwResult == WAIT_OBJECT_0); NOREF(dwResult);
        fRc = CloseHandle(g_VBoxUsbGlobal.hThread);
        AssertMsg(fRc, ("CloseHandle for hThread failed (dwErr=%u)\n", GetLastError()));
    }

    if (g_VBoxUsbGlobal.hTimer)
    {
        fRc = DeleteTimerQueueTimer(g_VBoxUsbGlobal.hTimerQueue, g_VBoxUsbGlobal.hTimer,
                                    INVALID_HANDLE_VALUE); /* <-- to block until the timer is completed */
        AssertMsg(fRc, ("DeleteTimerQueueTimer failed (dwErr=%u)\n", GetLastError()));
    }

    if (g_VBoxUsbGlobal.hTimerQueue)
    {
        fRc = DeleteTimerQueueEx(g_VBoxUsbGlobal.hTimerQueue,
                                 INVALID_HANDLE_VALUE); /* <-- to block until all timers are completed */
        AssertMsg(fRc, ("DeleteTimerQueueEx failed (dwErr=%u)\n", GetLastError()));
    }

    fRc = CloseHandle(g_VBoxUsbGlobal.hMonitor);
    AssertMsg(fRc, ("CloseHandle for hMonitor failed (dwErr=%u)\n", GetLastError()));
    g_VBoxUsbGlobal.hMonitor = INVALID_HANDLE_VALUE;

    fRc = CloseHandle(g_VBoxUsbGlobal.hInterruptEvent);
    AssertMsg(fRc, ("CloseHandle for hInterruptEvent failed (dwErr=%u)\n", GetLastError()));
    g_VBoxUsbGlobal.hInterruptEvent = NULL;

    fRc = CloseHandle(g_VBoxUsbGlobal.hNotifyEvent);
    AssertMsg(fRc, ("CloseHandle for hNotifyEvent failed (dwErr=%u)\n", GetLastError()));
    g_VBoxUsbGlobal.hNotifyEvent = NULL;

    return VINF_SUCCESS;
}

