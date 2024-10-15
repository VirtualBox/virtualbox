/* $Id: USBGetDevices.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define VBOX_USB_WITH_USBFS
#include "USBGetDevices.h"

#include <VBox/err.h>
#include <VBox/usb.h>
#include <VBox/usblib.h>

#include <iprt/linux/sysfs.h>
#include <iprt/cdefs.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/fs.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include "vector.h"

#ifdef VBOX_WITH_LINUX_COMPILER_H
# include <linux/compiler.h>
#endif
#include <linux/usbdevice_fs.h>

#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Structure describing a host USB device */
typedef struct USBDeviceInfo
{
    /** The device node of the device. */
    char *mDevice;
    /** The system identifier of the device.  Specific to the probing
     * method. */
    char *mSysfsPath;
    /** List of interfaces as sysfs paths */
    VECTOR_PTR(char *) mvecpszInterfaces;
} USBDeviceInfo;


/**
 * Does some extra checks to improve the detected device state.
 *
 * We cannot distinguish between USED_BY_HOST_CAPTURABLE and
 * USED_BY_GUEST, HELD_BY_PROXY all that well and it shouldn't be
 * necessary either.
 *
 * We will however, distinguish between the device we have permissions
 * to open and those we don't. This is necessary for two reasons.
 *
 * Firstly, because it's futile to even attempt opening a device which we
 * don't have access to, it only serves to confuse the user. (That said,
 * it might also be a bit confusing for the user to see that a USB device
 * is grayed out with no further explanation, and no way of generating an
 * error hinting at why this is the case.)
 *
 * Secondly and more importantly, we're racing against udevd with respect
 * to permissions and group settings on newly plugged devices. When we
 * detect a new device that we cannot access we will poll on it for a few
 * seconds to give udevd time to fix it. The polling is actually triggered
 * in the 'new device' case in the compare loop.
 *
 * The USBDEVICESTATE_USED_BY_HOST state is only used for this no-access
 * case, while USBDEVICESTATE_UNSUPPORTED is only used in the 'hub' case.
 * When it's neither of these, we set USBDEVICESTATE_UNUSED or
 * USBDEVICESTATE_USED_BY_HOST_CAPTURABLE depending on whether there is
 * a driver associated with any of the interfaces.
 *
 * All except the access check and a special idVendor == 0 precaution
 * is handled at parse time.
 *
 * @returns The adjusted state.
 * @param   pDevice     The device.
 */
static USBDEVICESTATE usbDeterminState(PCUSBDEVICE pDevice)
{
    /*
     * If it's already flagged as unsupported, there is nothing to do.
     */
    USBDEVICESTATE enmState = pDevice->enmState;
    if (enmState == USBDEVICESTATE_UNSUPPORTED)
        return USBDEVICESTATE_UNSUPPORTED;

    /*
     * Root hubs and similar doesn't have any vendor id, just
     * refuse these device.
     */
    if (!pDevice->idVendor)
        return USBDEVICESTATE_UNSUPPORTED;

    /*
     * Check if we've got access to the device, if we haven't flag
     * it as used-by-host.
     */
#ifndef VBOX_USB_WITH_SYSFS
    const char *pszAddress = pDevice->pszAddress;
#else
    if (pDevice->pszAddress == NULL)
        /* We can't do much with the device without an address. */
        return USBDEVICESTATE_UNSUPPORTED;
    const char *pszAddress = strstr(pDevice->pszAddress, "//device:");
    pszAddress = pszAddress != NULL
               ? pszAddress + sizeof("//device:") - 1
               : pDevice->pszAddress;
#endif
    if (    access(pszAddress, R_OK | W_OK) != 0
        &&  errno == EACCES)
        return USBDEVICESTATE_USED_BY_HOST;

#ifdef VBOX_USB_WITH_SYSFS
    /**
     * @todo Check that any other essential fields are present and mark as
     * invalid if not.  Particularly to catch the case where the device was
     * unplugged while we were reading in its properties.
     */
#endif

    return enmState;
}


/**
 * Dumps a USBDEVICE structure to the log using LogLevel 3.
 * @param   pDev        The structure to log.
 * @todo    This is really common code.
 */
static void usbLogDevice(PUSBDEVICE pDev)
{
    NOREF(pDev);
    if (LogIs3Enabled())
    {
        Log3(("USB device:\n"));
        Log3(("Product: %s (%x)\n", pDev->pszProduct, pDev->idProduct));
        Log3(("Manufacturer: %s (Vendor ID %x)\n", pDev->pszManufacturer, pDev->idVendor));
        Log3(("Serial number: %s (%llx)\n", pDev->pszSerialNumber, pDev->u64SerialHash));
        Log3(("Device revision: %d\n", pDev->bcdDevice));
        Log3(("Device class: %x\n", pDev->bDeviceClass));
        Log3(("Device subclass: %x\n", pDev->bDeviceSubClass));
        Log3(("Device protocol: %x\n", pDev->bDeviceProtocol));
        Log3(("USB version number: %d\n", pDev->bcdUSB));
        Log3(("Device speed: %s\n",
                pDev->enmSpeed == USBDEVICESPEED_UNKNOWN  ? "unknown"
              : pDev->enmSpeed == USBDEVICESPEED_LOW      ? "1.5 MBit/s"
              : pDev->enmSpeed == USBDEVICESPEED_FULL     ? "12 MBit/s"
              : pDev->enmSpeed == USBDEVICESPEED_HIGH     ? "480 MBit/s"
              : pDev->enmSpeed == USBDEVICESPEED_SUPER    ? "5.0 GBit/s"
              : pDev->enmSpeed == USBDEVICESPEED_VARIABLE ? "variable"
              :                                             "invalid"));
        Log3(("Number of configurations: %d\n", pDev->bNumConfigurations));
        Log3(("Bus number: %d\n", pDev->bBus));
        Log3(("Port number: %d\n", pDev->bPort));
        Log3(("Device number: %d\n", pDev->bDevNum));
        Log3(("Device state: %s\n",
                pDev->enmState == USBDEVICESTATE_UNSUPPORTED   ? "unsupported"
              : pDev->enmState == USBDEVICESTATE_USED_BY_HOST  ? "in use by host"
              : pDev->enmState == USBDEVICESTATE_USED_BY_HOST_CAPTURABLE ? "in use by host, possibly capturable"
              : pDev->enmState == USBDEVICESTATE_UNUSED        ? "not in use"
              : pDev->enmState == USBDEVICESTATE_HELD_BY_PROXY ? "held by proxy"
              : pDev->enmState == USBDEVICESTATE_USED_BY_GUEST ? "used by guest"
              :                                                  "invalid"));
        Log3(("OS device address: %s\n", pDev->pszAddress));
    }
}


#ifdef VBOX_USB_WITH_USBFS

/**
 * "reads" the number suffix.
 *
 * It's more like validating it and skipping the necessary number of chars.
 */
static int usbfsReadSkipSuffix(char **ppszNext)
{
    char *pszNext = *ppszNext;
    if (!RT_C_IS_SPACE(*pszNext) && *pszNext)
    {
        /* skip unit */
        if (pszNext[0] == 'm' && pszNext[1] == 's')
            pszNext += 2;
        else if (pszNext[0] == 'm' && pszNext[1] == 'A')
            pszNext += 2;

        /* skip parenthesis */
        if (*pszNext == '(')
        {
            pszNext = strchr(pszNext, ')');
            if (!pszNext++)
            {
                AssertMsgFailed(("*ppszNext=%s\n", *ppszNext));
                return VERR_PARSE_ERROR;
            }
        }

        /* blank or end of the line. */
        if (!RT_C_IS_SPACE(*pszNext) && *pszNext)
        {
            AssertMsgFailed(("pszNext=%s\n", pszNext));
            return VERR_PARSE_ERROR;
        }

        /* it's ok. */
        *ppszNext = pszNext;
    }

    return VINF_SUCCESS;
}


/**
 * Reads a USB number returning the number and the position of the next character to parse.
 */
static int usbfsReadNum(const char *pszValue, unsigned uBase, uint32_t u32Mask, void *pvNum, char **ppszNext)
{
    /*
     * Initialize return value to zero and strip leading spaces.
     */
    switch (u32Mask)
    {
        case 0xff: *(uint8_t *)pvNum = 0; break;
        case 0xffff: *(uint16_t *)pvNum = 0; break;
        case 0xffffffff: *(uint32_t *)pvNum = 0; break;
    }
    pszValue = RTStrStripL(pszValue);
    if (*pszValue)
    {
        /*
         * Try convert the number.
         */
        char *pszNext;
        uint32_t u32 = 0;
        RTStrToUInt32Ex(pszValue, &pszNext, uBase, &u32);
        if (pszNext == pszValue)
        {
            AssertMsgFailed(("pszValue=%d\n", pszValue));
            return VERR_NO_DATA;
        }

        /*
         * Check the range.
         */
        if (u32 & ~u32Mask)
        {
            AssertMsgFailed(("pszValue=%d u32=%#x lMask=%#x\n", pszValue, u32, u32Mask));
            return VERR_OUT_OF_RANGE;
        }

        int vrc = usbfsReadSkipSuffix(&pszNext);
        if (RT_FAILURE(vrc))
            return vrc;

        *ppszNext = pszNext;

        /*
         * Set the value.
         */
        switch (u32Mask)
        {
            case 0xff: *(uint8_t *)pvNum = (uint8_t)u32; break;
            case 0xffff: *(uint16_t *)pvNum = (uint16_t)u32; break;
            case 0xffffffff: *(uint32_t *)pvNum = (uint32_t)u32; break;
        }
    }
    return VINF_SUCCESS;
}


static int usbfsRead8(const char *pszValue, unsigned uBase, uint8_t *pu8, char **ppszNext)
{
    return usbfsReadNum(pszValue, uBase, 0xff, pu8, ppszNext);
}


static int usbfsRead16(const char *pszValue, unsigned uBase, uint16_t *pu16, char **ppszNext)
{
    return usbfsReadNum(pszValue, uBase, 0xffff, pu16, ppszNext);
}


/**
 * Reads a USB BCD number returning the number and the position of the next character to parse.
 * The returned number contains the integer part in the high byte and the decimal part in the low byte.
 */
static int usbfsReadBCD(const char *pszValue, unsigned uBase, uint16_t *pu16, char **ppszNext)
{
    /*
     * Initialize return value to zero and strip leading spaces.
     */
    *pu16 = 0;
    pszValue = RTStrStripL(pszValue);
    if (*pszValue)
    {
        /*
         * Try convert the number.
         */
        /* integer part */
        char *pszNext;
        uint32_t u32Int = 0;
        RTStrToUInt32Ex(pszValue, &pszNext, uBase, &u32Int);
        if (pszNext == pszValue)
        {
            AssertMsgFailed(("pszValue=%s\n", pszValue));
            return VERR_NO_DATA;
        }
        if (u32Int & ~0xff)
        {
            AssertMsgFailed(("pszValue=%s u32Int=%#x (int)\n", pszValue, u32Int));
            return VERR_OUT_OF_RANGE;
        }

        /* skip dot and read decimal part */
        if (*pszNext != '.')
        {
            AssertMsgFailed(("pszValue=%s pszNext=%s (int)\n", pszValue, pszNext));
            return VERR_PARSE_ERROR;
        }
        char *pszValue2 = RTStrStripL(pszNext + 1);
        uint32_t u32Dec = 0;
        RTStrToUInt32Ex(pszValue2, &pszNext, uBase, &u32Dec);
        if (pszNext == pszValue)
        {
            AssertMsgFailed(("pszValue=%s\n", pszValue));
            return VERR_NO_DATA;
        }
        if (u32Dec & ~0xff)
        {
            AssertMsgFailed(("pszValue=%s u32Dec=%#x\n", pszValue, u32Dec));
            return VERR_OUT_OF_RANGE;
        }

        /*
         * Validate and skip stuff following the number.
         */
        int vrc = usbfsReadSkipSuffix(&pszNext);
        if (RT_FAILURE(vrc))
            return vrc;
        *ppszNext = pszNext;

        /*
         * Set the value.
         */
        *pu16 = (uint16_t)((u32Int << 8) | (uint16_t)u32Dec);
    }
    return VINF_SUCCESS;
}


/**
 * Reads a string, i.e. allocates memory and copies it.
 *
 * We assume that a string is Utf8 and if that's not the case
 * (pre-2.6.32-kernels used Latin-1, but so few devices return non-ASCII that
 * this usually goes unnoticed) then we mercilessly force it to be so.
 */
static int usbfsReadStr(const char *pszValue, const char **ppsz)
{
    char *psz;

    if (*ppsz)
        RTStrFree((char *)*ppsz);
    psz = RTStrDup(pszValue);
    if (psz)
    {
        USBLibPurgeEncoding(psz);
        *ppsz = psz;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}


/**
 * Skips the current property.
 */
static char *usbfsReadSkip(char *pszValue)
{
    char *psz = strchr(pszValue, '=');
    if (psz)
        psz = strchr(psz + 1, '=');
    if (!psz)
        return strchr(pszValue,  '\0');
    while (psz > pszValue && !RT_C_IS_SPACE(psz[-1]))
        psz--;
    Assert(psz > pszValue);
    return psz;
}


/**
 * Determine the USB speed.
 */
static int usbfsReadSpeed(const char *pszValue, USBDEVICESPEED *pSpd, char **ppszNext)
{
    pszValue = RTStrStripL(pszValue);
    /* verified with Linux 2.4.0 ... Linux 2.6.25 */
    if (!strncmp(pszValue, RT_STR_TUPLE("1.5")))
        *pSpd = USBDEVICESPEED_LOW;
    else if (!strncmp(pszValue, RT_STR_TUPLE("12 ")))
        *pSpd = USBDEVICESPEED_FULL;
    else if (!strncmp(pszValue, RT_STR_TUPLE("480")))
        *pSpd = USBDEVICESPEED_HIGH;
    else if (!strncmp(pszValue, RT_STR_TUPLE("5000")))
        *pSpd = USBDEVICESPEED_SUPER;
    else
        *pSpd = USBDEVICESPEED_UNKNOWN;
    while (pszValue[0] != '\0' && !RT_C_IS_SPACE(pszValue[0]))
        pszValue++;
    *ppszNext = (char *)pszValue;
    return VINF_SUCCESS;
}


/**
 * Compare a prefix and returns pointer to the char following it if it matches.
 */
static char *usbfsPrefix(char *psz, const char *pszPref, size_t cchPref)
{
    if (strncmp(psz, pszPref, cchPref))
        return NULL;
    return psz + cchPref;
}


/** Just a worker for USBProxyServiceLinux::getDevices that avoids some code duplication. */
static int usbfsAddDeviceToChain(PUSBDEVICE pDev, PUSBDEVICE *ppFirst, PUSBDEVICE **pppNext, const char *pszUsbfsRoot,
                                 bool fUnsupportedDevicesToo, int vrc)
{
    /* usbDeterminState requires the address. */
    PUSBDEVICE pDevNew = (PUSBDEVICE)RTMemDup(pDev, sizeof(*pDev));
    if (pDevNew)
    {
        RTStrAPrintf((char **)&pDevNew->pszAddress, "%s/%03d/%03d", pszUsbfsRoot, pDevNew->bBus, pDevNew->bDevNum);
        if (pDevNew->pszAddress)
        {
            pDevNew->enmState = usbDeterminState(pDevNew);
            if (pDevNew->enmState != USBDEVICESTATE_UNSUPPORTED || fUnsupportedDevicesToo)
            {
                if (*pppNext)
                    **pppNext = pDevNew;
                else
                    *ppFirst = pDevNew;
                *pppNext = &pDevNew->pNext;
            }
            else
                deviceFree(pDevNew);
        }
        else
        {
            deviceFree(pDevNew);
            vrc = VERR_NO_MEMORY;
        }
    }
    else
    {
        vrc = VERR_NO_MEMORY;
        deviceFreeMembers(pDev);
    }

    return vrc;
}


static int usbfsOpenDevicesFile(const char *pszUsbfsRoot, FILE **ppFile)
{
    char *pszPath;
    FILE *pFile;
    RTStrAPrintf(&pszPath, "%s/devices", pszUsbfsRoot);
    if (!pszPath)
        return VERR_NO_MEMORY;
    pFile = fopen(pszPath, "r");
    RTStrFree(pszPath);
    if (!pFile)
        return RTErrConvertFromErrno(errno);
    *ppFile = pFile;
    return VINF_SUCCESS;
}


/**
 * USBProxyService::getDevices() implementation for usbfs.
 *
 * The @a fUnsupportedDevicesToo flag tells the function to return information
 * about unsupported devices as well.  This is used as a sanity test to check
 * that a devices file is really what we expect.
 */
static PUSBDEVICE usbfsGetDevices(const char *pszUsbfsRoot, bool fUnsupportedDevicesToo)
{
    PUSBDEVICE pFirst = NULL;
    FILE *pFile = NULL;
    int vrc = usbfsOpenDevicesFile(pszUsbfsRoot, &pFile);
    if (RT_SUCCESS(vrc))
    {
        PUSBDEVICE     *ppNext = NULL;
        int             cHits = 0;
        char            szLine[1024];
        USBDEVICE       Dev;
        RT_ZERO(Dev);
        Dev.enmState = USBDEVICESTATE_UNUSED;

        /* Set close on exit and hope no one is racing us. */
        vrc = fcntl(fileno(pFile), F_SETFD, FD_CLOEXEC) >= 0
            ? VINF_SUCCESS
            : RTErrConvertFromErrno(errno);
        while (   RT_SUCCESS(vrc)
               && fgets(szLine, sizeof(szLine), pFile))
        {
            char   *psz;
            char   *pszValue;

            /* validate and remove the trailing newline. */
            psz = strchr(szLine, '\0');
            if (psz[-1] != '\n' && !feof(pFile))
            {
                AssertMsgFailed(("Line too long. (cch=%d)\n", strlen(szLine)));
                continue;
            }

            /* strip */
            psz = RTStrStrip(szLine);
            if (!*psz)
                continue;

            /*
             * Interpret the line.
             * (Ordered by normal occurrence.)
             */
            char ch = psz[0];
            if (psz[1] != ':')
                continue;
            psz = RTStrStripL(psz + 3);
#define PREFIX(str) ( (pszValue = usbfsPrefix(psz, str, sizeof(str) - 1)) != NULL )
            switch (ch)
            {
                /*
                 * T:  Bus=dd Lev=dd Prnt=dd Port=dd Cnt=dd Dev#=ddd Spd=ddd MxCh=dd
                 * |   |      |      |       |       |      |        |       |__MaxChildren
                 * |   |      |      |       |       |      |        |__Device Speed in Mbps
                 * |   |      |      |       |       |      |__DeviceNumber
                 * |   |      |      |       |       |__Count of devices at this level
                 * |   |      |      |       |__Connector/Port on Parent for this device
                 * |   |      |      |__Parent DeviceNumber
                 * |   |      |__Level in topology for this bus
                 * |   |__Bus number
                 * |__Topology info tag
                 */
                case 'T':
                    /* add */
                    AssertMsg(cHits >= 3 || cHits == 0, ("cHits=%d\n", cHits));
                    if (cHits >= 3)
                        vrc = usbfsAddDeviceToChain(&Dev, &pFirst, &ppNext, pszUsbfsRoot, fUnsupportedDevicesToo, vrc);
                    else
                        deviceFreeMembers(&Dev);

                    /* Reset device state */
                    RT_ZERO(Dev);
                    Dev.enmState = USBDEVICESTATE_UNUSED;
                    cHits = 1;

                    /* parse the line. */
                    while (*psz && RT_SUCCESS(vrc))
                    {
                        if (PREFIX("Bus="))
                            vrc = usbfsRead8(pszValue, 10, &Dev.bBus, &psz);
                        else if (PREFIX("Port="))
                            vrc = usbfsRead8(pszValue, 10, &Dev.bPort, &psz);
                        else if (PREFIX("Spd="))
                            vrc = usbfsReadSpeed(pszValue, &Dev.enmSpeed, &psz);
                        else if (PREFIX("Dev#="))
                            vrc = usbfsRead8(pszValue, 10, &Dev.bDevNum, &psz);
                        else
                            psz = usbfsReadSkip(psz);
                        psz = RTStrStripL(psz);
                    }
                    break;

                /*
                 * Bandwidth info:
                 * B:  Alloc=ddd/ddd us (xx%), #Int=ddd, #Iso=ddd
                 * |   |                       |         |__Number of isochronous requests
                 * |   |                       |__Number of interrupt requests
                 * |   |__Total Bandwidth allocated to this bus
                 * |__Bandwidth info tag
                 */
                case 'B':
                    break;

                /*
                 * D:  Ver=x.xx Cls=xx(sssss) Sub=xx Prot=xx MxPS=dd #Cfgs=dd
                 * |   |        |             |      |       |       |__NumberConfigurations
                 * |   |        |             |      |       |__MaxPacketSize of Default Endpoint
                 * |   |        |             |      |__DeviceProtocol
                 * |   |        |             |__DeviceSubClass
                 * |   |        |__DeviceClass
                 * |   |__Device USB version
                 * |__Device info tag #1
                 */
                case 'D':
                    while (*psz && RT_SUCCESS(vrc))
                    {
                        if (PREFIX("Ver="))
                            vrc = usbfsReadBCD(pszValue, 16, &Dev.bcdUSB, &psz);
                        else if (PREFIX("Cls="))
                        {
                            vrc = usbfsRead8(pszValue, 16, &Dev.bDeviceClass, &psz);
                            if (RT_SUCCESS(vrc) && Dev.bDeviceClass == 9 /* HUB */)
                                Dev.enmState = USBDEVICESTATE_UNSUPPORTED;
                        }
                        else if (PREFIX("Sub="))
                            vrc = usbfsRead8(pszValue, 16, &Dev.bDeviceSubClass, &psz);
                        else if (PREFIX("Prot="))
                            vrc = usbfsRead8(pszValue, 16, &Dev.bDeviceProtocol, &psz);
                        //else if (PREFIX("MxPS="))
                        //    vrc = usbRead16(pszValue, 10, &Dev.wMaxPacketSize, &psz);
                        else if (PREFIX("#Cfgs="))
                            vrc = usbfsRead8(pszValue, 10, &Dev.bNumConfigurations, &psz);
                        else
                            psz = usbfsReadSkip(psz);
                        psz = RTStrStripL(psz);
                    }
                    cHits++;
                    break;

                /*
                 * P:  Vendor=xxxx ProdID=xxxx Rev=xx.xx
                 * |   |           |           |__Product revision number
                 * |   |           |__Product ID code
                 * |   |__Vendor ID code
                 * |__Device info tag #2
                 */
                case 'P':
                    while (*psz && RT_SUCCESS(vrc))
                    {
                        if (PREFIX("Vendor="))
                            vrc = usbfsRead16(pszValue, 16, &Dev.idVendor, &psz);
                        else if (PREFIX("ProdID="))
                            vrc = usbfsRead16(pszValue, 16, &Dev.idProduct, &psz);
                        else if (PREFIX("Rev="))
                            vrc = usbfsReadBCD(pszValue, 16, &Dev.bcdDevice, &psz);
                        else
                            psz = usbfsReadSkip(psz);
                        psz = RTStrStripL(psz);
                    }
                    cHits++;
                    break;

                /*
                 * String.
                 */
                case 'S':
                    if (PREFIX("Manufacturer="))
                        vrc = usbfsReadStr(pszValue, &Dev.pszManufacturer);
                    else if (PREFIX("Product="))
                        vrc = usbfsReadStr(pszValue, &Dev.pszProduct);
                    else if (PREFIX("SerialNumber="))
                    {
                        vrc = usbfsReadStr(pszValue, &Dev.pszSerialNumber);
                        if (RT_SUCCESS(vrc))
                            Dev.u64SerialHash = USBLibHashSerial(pszValue);
                    }
                    break;

                /*
                 * C:* #Ifs=dd Cfg#=dd Atr=xx MPwr=dddmA
                 * | | |       |       |      |__MaxPower in mA
                 * | | |       |       |__Attributes
                 * | | |       |__ConfiguratioNumber
                 * | | |__NumberOfInterfaces
                 * | |__ "*" indicates the active configuration (others are " ")
                 * |__Config info tag
                 */
                case 'C':
                    break;

                /*
                 * I:  If#=dd Alt=dd #EPs=dd Cls=xx(sssss) Sub=xx Prot=xx Driver=ssss
                 * |   |      |      |       |             |      |       |__Driver name
                 * |   |      |      |       |             |      |          or "(none)"
                 * |   |      |      |       |             |      |__InterfaceProtocol
                 * |   |      |      |       |             |__InterfaceSubClass
                 * |   |      |      |       |__InterfaceClass
                 * |   |      |      |__NumberOfEndpoints
                 * |   |      |__AlternateSettingNumber
                 * |   |__InterfaceNumber
                 * |__Interface info tag
                 */
                case 'I':
                {
                    /* Check for thing we don't support.  */
                    while (*psz && RT_SUCCESS(vrc))
                    {
                        if (PREFIX("Driver="))
                        {
                            const char *pszDriver = NULL;
                            vrc = usbfsReadStr(pszValue, &pszDriver);
                            if (   !pszDriver
                                || !*pszDriver
                                || !strcmp(pszDriver, "(none)")
                                || !strcmp(pszDriver, "(no driver)"))
                                /* no driver */;
                            else if (!strcmp(pszDriver, "hub"))
                                Dev.enmState = USBDEVICESTATE_UNSUPPORTED;
                            else if (Dev.enmState == USBDEVICESTATE_UNUSED)
                                Dev.enmState = USBDEVICESTATE_USED_BY_HOST_CAPTURABLE;
                            RTStrFree((char *)pszDriver);
                            break; /* last attrib */
                        }
                        else if (PREFIX("Cls="))
                        {
                            uint8_t bInterfaceClass;
                            vrc = usbfsRead8(pszValue, 16, &bInterfaceClass, &psz);
                            if (RT_SUCCESS(vrc) && bInterfaceClass == 9 /* HUB */)
                                Dev.enmState = USBDEVICESTATE_UNSUPPORTED;
                        }
                        else
                            psz = usbfsReadSkip(psz);
                        psz = RTStrStripL(psz);
                    }
                    break;
                }


                /*
                 * E:  Ad=xx(s) Atr=xx(ssss) MxPS=dddd Ivl=dddms
                 * |   |        |            |         |__Interval (max) between transfers
                 * |   |        |            |__EndpointMaxPacketSize
                 * |   |        |__Attributes(EndpointType)
                 * |   |__EndpointAddress(I=In,O=Out)
                 * |__Endpoint info tag
                 */
                case 'E':
                    break;

            }
#undef PREFIX
        } /* parse loop */
        fclose(pFile);

        /*
         * Add the current entry.
         */
        AssertMsg(cHits >= 3 || cHits == 0, ("cHits=%d\n", cHits));
        if (cHits >= 3)
            vrc = usbfsAddDeviceToChain(&Dev, &pFirst, &ppNext, pszUsbfsRoot, fUnsupportedDevicesToo, vrc);

        /*
         * Success?
         */
        if (RT_FAILURE(vrc))
        {
            while (pFirst)
            {
                PUSBDEVICE pFree = pFirst;
                pFirst = pFirst->pNext;
                deviceFree(pFree);
            }
        }
    }
    if (RT_FAILURE(vrc))
        LogFlow(("USBProxyServiceLinux::getDevices: vrc=%Rrc\n", vrc));
    return pFirst;
}

#endif /* VBOX_USB_WITH_USBFS */
#ifdef VBOX_USB_WITH_SYSFS

static void usbsysfsCleanupDevInfo(USBDeviceInfo *pSelf)
{
    RTStrFree(pSelf->mDevice);
    RTStrFree(pSelf->mSysfsPath);
    pSelf->mDevice = pSelf->mSysfsPath = NULL;
    VEC_CLEANUP_PTR(&pSelf->mvecpszInterfaces);
}


static int usbsysfsInitDevInfo(USBDeviceInfo *pSelf, const char *aDevice, const char *aSystemID)
{
    pSelf->mDevice = aDevice ? RTStrDup(aDevice) : NULL;
    pSelf->mSysfsPath = aSystemID ? RTStrDup(aSystemID) : NULL;
    VEC_INIT_PTR(&pSelf->mvecpszInterfaces, char *, RTStrFree);
    if ((aDevice && !pSelf->mDevice) || (aSystemID && ! pSelf->mSysfsPath))
    {
        usbsysfsCleanupDevInfo(pSelf);
        return 0;
    }
    return 1;
}

# define USBDEVICE_MAJOR 189

/**
 * Calculate the bus (a.k.a root hub) number of a USB device from it's sysfs
 * path.
 *
 * sysfs nodes representing root hubs have file names of the form
 * usb<n>, where n is the bus number; other devices start with that number.
 * See [http://www.linux-usb.org/FAQ.html#i6] and
 * [http://www.kernel.org/doc/Documentation/usb/proc_usb_info.txt] for
 * equivalent information about usbfs.
 *
 * @returns a bus number greater than 0 on success or 0 on failure.
 */
static unsigned usbsysfsGetBusFromPath(const char *pszPath)
{
    const char *pszFile = strrchr(pszPath, '/');
    if (!pszFile)
        return 0;
    unsigned bus = RTStrToUInt32(pszFile + 1);
    if (   !bus
        && pszFile[1] == 'u' && pszFile[2] == 's' && pszFile[3] == 'b')
    bus = RTStrToUInt32(pszFile + 4);
    return bus;
}


/**
 * Calculate the device number of a USB device.
 *
 * See drivers/usb/core/hub.c:usb_new_device as of Linux 2.6.20.
 */
static dev_t usbsysfsMakeDevNum(unsigned bus, unsigned device)
{
    AssertReturn(bus > 0, 0);
    AssertReturn(((device - 1) & ~127) == 0, 0);
    AssertReturn(device > 0, 0);
    return makedev(USBDEVICE_MAJOR, ((bus - 1) << 7) + device - 1);
}


/**
 * If a file @a pszNode from /sys/bus/usb/devices is a device rather than an
 * interface add an element for the device to @a pvecDevInfo.
 */
static int usbsysfsAddIfDevice(const char *pszDevicesRoot, const char *pszNode, VECTOR_OBJ(USBDeviceInfo) *pvecDevInfo)
{
    const char *pszFile = strrchr(pszNode, '/');
    if (!pszFile)
        return VERR_INVALID_PARAMETER;
    if (strchr(pszFile, ':'))
        return VINF_SUCCESS;

    unsigned bus = usbsysfsGetBusFromPath(pszNode);
    if (!bus)
        return VINF_SUCCESS;

    int64_t device;
    int vrc = RTLinuxSysFsReadIntFile(10, &device, "%s/devnum", pszNode);
    if (RT_FAILURE(vrc))
        return VINF_SUCCESS;

    dev_t devnum = usbsysfsMakeDevNum(bus, (int)device);
    if (!devnum)
        return VINF_SUCCESS;

    char szDevPath[RTPATH_MAX];
    vrc = RTLinuxCheckDevicePath(devnum, RTFS_TYPE_DEV_CHAR, szDevPath, sizeof(szDevPath),
                                 "%s/%.3d/%.3d", pszDevicesRoot, bus, device);
    if (RT_FAILURE(vrc))
        return VINF_SUCCESS;

    USBDeviceInfo info;
    if (usbsysfsInitDevInfo(&info, szDevPath, pszNode))
    {
        vrc = VEC_PUSH_BACK_OBJ(pvecDevInfo, USBDeviceInfo, &info);
        if (RT_SUCCESS(vrc))
            return VINF_SUCCESS;
    }
    usbsysfsCleanupDevInfo(&info);
    return VERR_NO_MEMORY;
}


/**
 * The logic for testing whether a sysfs address corresponds to an interface of
 * a device.
 *
 * Both must be referenced by their canonical sysfs paths.  This is not tested,
 * as the test requires file-system interaction.
 */
static bool usbsysfsMuiIsAnInterfaceOf(const char *pszIface, const char *pszDev)
{
    size_t cchDev = strlen(pszDev);

    AssertPtr(pszIface);
    AssertPtr(pszDev);
    Assert(pszIface[0] == '/');
    Assert(pszDev[0] == '/');
    Assert(pszDev[cchDev - 1] != '/');

    /* If this passes, pszIface is at least cchDev long */
    if (strncmp(pszIface, pszDev, cchDev))
        return false;

    /* If this passes, pszIface is longer than cchDev */
    if (pszIface[cchDev] != '/')
        return false;

    /* In sysfs an interface is an immediate subdirectory of the device */
    if (strchr(pszIface + cchDev + 1, '/'))
        return false;

    /* And it always has a colon in its name */
    if (!strchr(pszIface + cchDev + 1, ':'))
        return false;

    /* And hopefully we have now elimitated everything else */
    return true;
}


# ifdef DEBUG
#  ifdef __cplusplus
/** Unit test the logic in muiIsAnInterfaceOf in debug builds. */
class testIsAnInterfaceOf
{
public:
    testIsAnInterfaceOf()
    {
        Assert(usbsysfsMuiIsAnInterfaceOf("/sys/devices/pci0000:00/0000:00:1a.0/usb3/3-0:1.0",
               "/sys/devices/pci0000:00/0000:00:1a.0/usb3"));
        Assert(!usbsysfsMuiIsAnInterfaceOf("/sys/devices/pci0000:00/0000:00:1a.0/usb3/3-1",
               "/sys/devices/pci0000:00/0000:00:1a.0/usb3"));
        Assert(!usbsysfsMuiIsAnInterfaceOf("/sys/devices/pci0000:00/0000:00:1a.0/usb3/3-0:1.0/driver",
               "/sys/devices/pci0000:00/0000:00:1a.0/usb3"));
    }
};
static testIsAnInterfaceOf testIsAnInterfaceOfInst;
#  endif /* __cplusplus */
# endif /* DEBUG */


/**
 * Tell whether a file in /sys/bus/usb/devices is an interface rather than a
 * device.
 */
static int usbsysfsAddIfInterfaceOf(const char *pszNode, USBDeviceInfo *pInfo)
{
    if (!usbsysfsMuiIsAnInterfaceOf(pszNode, pInfo->mSysfsPath))
        return VINF_SUCCESS;

    char *pszDup = (char *)RTStrDup(pszNode);
    if (pszDup)
    {
        int vrc = VEC_PUSH_BACK_PTR(&pInfo->mvecpszInterfaces, char *, pszDup);
        if (RT_SUCCESS(vrc))
            return VINF_SUCCESS;
        RTStrFree(pszDup);
    }
    return VERR_NO_MEMORY;
}


/**
 * Helper for usbsysfsReadFilePaths().
 *
 * Adds the entries from the open directory @a pDir to the vector @a pvecpchDevs
 * using either the full path or the realpath() and skipping hidden files and
 * files on which realpath() fails.
 */
static int usbsysfsReadFilePathsFromDir(const char *pszPath, DIR *pDir, VECTOR_PTR(char *) *pvecpchDevs)
{
    struct dirent entry, *pResult;
    int err;

#if RT_GNUC_PREREQ(4, 6)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    for (err = readdir_r(pDir, &entry, &pResult); pResult;
         err = readdir_r(pDir, &entry, &pResult))
#if RT_GNUC_PREREQ(4, 6)
# pragma GCC diagnostic pop
#endif
    {
        char szPath[RTPATH_MAX + 1];
        char szRealPath[RTPATH_MAX + 1];
        if (entry.d_name[0] == '.')
            continue;
        if (snprintf(szPath, sizeof(szPath), "%s/%s", pszPath, entry.d_name) < 0)
            return RTErrConvertFromErrno(errno); /** @todo r=bird: snprintf isn't document to set errno. Also, wouldn't it be better to continue on errors? Finally, you don't need to copy pszPath each time... */
        if (!realpath(szPath, szRealPath))
            return RTErrConvertFromErrno(errno);
        char *pszPathCopy = RTStrDup(szRealPath);
        if (!pszPathCopy)
            return VERR_NO_MEMORY;
        int vrc = VEC_PUSH_BACK_PTR(pvecpchDevs, char *, pszPathCopy);
        if (RT_FAILURE(vrc))
            return vrc;
    }
    return RTErrConvertFromErrno(err);
}


/**
 * Dump the names of a directory's entries into a vector of char pointers.
 *
 * @returns zero on success or (positive) posix error value.
 * @param   pszPath      the path to dump.
 * @param   pvecpchDevs   an empty vector of char pointers - must be cleaned up
 *                        by the caller even on failure.
 * @param   withRealPath  whether to canonicalise the filename with realpath
 */
static int usbsysfsReadFilePaths(const char *pszPath, VECTOR_PTR(char *) *pvecpchDevs)
{
    AssertPtrReturn(pvecpchDevs, EINVAL);
    AssertReturn(VEC_SIZE_PTR(pvecpchDevs) == 0, EINVAL);
    AssertPtrReturn(pszPath, EINVAL);

    DIR *pDir = opendir(pszPath);
    if (!pDir)
        return RTErrConvertFromErrno(errno);
    int vrc = usbsysfsReadFilePathsFromDir(pszPath, pDir, pvecpchDevs);
    if (closedir(pDir) < 0 && RT_SUCCESS(vrc))
        vrc = RTErrConvertFromErrno(errno);
    return vrc;
}


/**
 * Logic for USBSysfsEnumerateHostDevices.
 *
 * @param pvecDevInfo  vector of device information structures to add device
 *                     information to
 * @param pvecpchDevs  empty scratch vector which will be freed by the caller,
 *                     to simplify exit logic
 */
static int usbsysfsEnumerateHostDevicesWorker(const char *pszDevicesRoot,
                                              VECTOR_OBJ(USBDeviceInfo) *pvecDevInfo,
                                              VECTOR_PTR(char *) *pvecpchDevs)
{

    AssertPtrReturn(pvecDevInfo, VERR_INVALID_POINTER);
    LogFlowFunc (("pvecDevInfo=%p\n", pvecDevInfo));

    int vrc = usbsysfsReadFilePaths("/sys/bus/usb/devices", pvecpchDevs);
    if (RT_FAILURE(vrc))
        return vrc;

    char **ppszEntry;
    VEC_FOR_EACH(pvecpchDevs, char *, ppszEntry)
    {
        vrc = usbsysfsAddIfDevice(pszDevicesRoot, *ppszEntry, pvecDevInfo);
        if (RT_FAILURE(vrc))
            return vrc;
    }

    USBDeviceInfo *pInfo;
    VEC_FOR_EACH(pvecDevInfo, USBDeviceInfo, pInfo)
        VEC_FOR_EACH(pvecpchDevs, char *, ppszEntry)
        {
            vrc = usbsysfsAddIfInterfaceOf(*ppszEntry, pInfo);
            if (RT_FAILURE(vrc))
                return vrc;
        }
    return VINF_SUCCESS;
}


static int usbsysfsEnumerateHostDevices(const char *pszDevicesRoot, VECTOR_OBJ(USBDeviceInfo) *pvecDevInfo)
{
    VECTOR_PTR(char *) vecpchDevs;

    AssertReturn(VEC_SIZE_OBJ(pvecDevInfo) == 0, VERR_INVALID_PARAMETER);
    LogFlowFunc(("entered\n"));
    VEC_INIT_PTR(&vecpchDevs, char *, RTStrFree);
    int vrc = usbsysfsEnumerateHostDevicesWorker(pszDevicesRoot, pvecDevInfo, &vecpchDevs);
    VEC_CLEANUP_PTR(&vecpchDevs);
    LogFlowFunc(("vrc=%Rrc\n", vrc));
    return vrc;
}


/**
 * Helper function for extracting the port number on the parent device from
 * the sysfs path value.
 *
 * The sysfs path is a chain of elements separated by forward slashes, and for
 * USB devices, the last element in the chain takes the form
 *   <port>-<port>.[...].<port>[:<config>.<interface>]
 * where the first <port> is the port number on the root hub, and the following
 * (optional) ones are the port numbers on any other hubs between the device
 * and the root hub.  The last part (:<config.interface>) is only present for
 * interfaces, not for devices.  This API should only be called for devices.
 * For compatibility with usbfs, which enumerates from zero up, we subtract one
 * from the port number.
 *
 * For root hubs, the last element in the chain takes the form
 *   usb<hub number>
 * and usbfs always returns port number zero.
 *
 * @returns VBox status code. pu8Port is set on success.
 * @param   pszPath     The sysfs path to parse.
 * @param   pu8Port     Where to store the port number.
 */
static int usbsysfsGetPortFromStr(const char *pszPath, uint8_t *pu8Port)
{
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertPtrReturn(pu8Port, VERR_INVALID_POINTER);

    /*
     * This should not be possible until we get PCs with USB as their primary bus.
     * Note: We don't assert this, as we don't expect the caller to validate the
     *       sysfs path.
     */
    const char *pszLastComp = strrchr(pszPath, '/');
    if (!pszLastComp)
    {
        Log(("usbGetPortFromSysfsPath(%s): failed [1]\n", pszPath));
        return VERR_INVALID_PARAMETER;
    }
    pszLastComp++; /* skip the slash */

    /*
     * This API should not be called for interfaces, so the last component
     * of the path should not contain a colon.  We *do* assert this, as it
     * might indicate a caller bug.
     */
    AssertMsgReturn(strchr(pszLastComp, ':') == NULL, ("%s\n", pszPath), VERR_INVALID_PARAMETER);

    /*
     * Look for the start of the last number.
     */
    const char *pchDash = strrchr(pszLastComp, '-');
    const char *pchDot  = strrchr(pszLastComp, '.');
    if (!pchDash && !pchDot)
    {
        /* No -/. so it must be a root hub. Check that it's usb<something>. */
        if (strncmp(pszLastComp, RT_STR_TUPLE("usb")) != 0)
        {
            Log(("usbGetPortFromSysfsPath(%s): failed [2]\n", pszPath));
            return VERR_INVALID_PARAMETER;
        }
        return VERR_NOT_SUPPORTED;
    }

    const char *pszLastPort = pchDot != NULL
                            ? pchDot  + 1
                            : pchDash + 1;
    int vrc = RTStrToUInt8Full(pszLastPort, 10, pu8Port);
    if (vrc != VINF_SUCCESS)
    {
        Log(("usbGetPortFromSysfsPath(%s): failed [3], vrc=%Rrc\n", pszPath, vrc));
        return VERR_INVALID_PARAMETER;
    }
    if (*pu8Port == 0)
    {
        Log(("usbGetPortFromSysfsPath(%s): failed [4]\n", pszPath));
        return VERR_INVALID_PARAMETER;
    }

    /* usbfs compatibility, 0-based port number. */
    *pu8Port = (uint8_t)(*pu8Port - 1);
    return VINF_SUCCESS;
}


/**
 * Converts a sysfs BCD value into a uint16_t.
 *
 * In contrast to usbReadBCD() this function can handle BCD values without
 * a decimal separator. This is necessary for parsing bcdDevice.
 *
 * @param   pszBuf      Pointer to the string buffer.
 * @param   pu15        Pointer to the return value.
 * @returns IPRT status code.
 */
static int usbsysfsConvertStrToBCD(const char *pszBuf, uint16_t *pu16)
{
    char *pszNext;
    int32_t i32;

    pszBuf = RTStrStripL(pszBuf);
    int vrc = RTStrToInt32Ex(pszBuf, &pszNext, 16, &i32);
    if (   RT_FAILURE(vrc)
        || vrc == VWRN_NUMBER_TOO_BIG
        || i32 < 0)
        return VERR_NUMBER_TOO_BIG;
    if (*pszNext == '.')
    {
        if (i32 > 255)
            return VERR_NUMBER_TOO_BIG;
        int32_t i32Lo;
        vrc = RTStrToInt32Ex(pszNext+1, &pszNext, 16, &i32Lo);
        if (   RT_FAILURE(vrc)
            || vrc == VWRN_NUMBER_TOO_BIG
            || i32Lo > 255
            || i32Lo < 0)
            return VERR_NUMBER_TOO_BIG;
        i32 = (i32 << 8) | i32Lo;
    }
    if (   i32 > 65535
        || (*pszNext != '\0' && *pszNext != ' '))
        return VERR_NUMBER_TOO_BIG;

    *pu16 = (uint16_t)i32;
    return VINF_SUCCESS;
}


/**
 * Returns the byte value for the given device property or sets the given default if an
 * error occurs while obtaining it.
 *
 * @returns uint8_t value of the given property.
 * @param   uBase       The base of the number in the sysfs property.
 * @param   fDef        The default to set on error.
 * @param   pszFormat   The format string for the property.
 * @param   ...         Arguments for the format string.
 */
static uint8_t usbsysfsReadDevicePropertyU8Def(unsigned uBase, uint8_t fDef, const char *pszFormat, ...)
{
    int64_t i64Tmp = 0;

    va_list va;
    va_start(va, pszFormat);
    int vrc = RTLinuxSysFsReadIntFileV(uBase, &i64Tmp, pszFormat, va);
    va_end(va);
    if (RT_SUCCESS(vrc))
        return (uint8_t)i64Tmp;
    return fDef;
}


/**
 * Returns the uint16_t value for the given device property or sets the given default if an
 * error occurs while obtaining it.
 *
 * @returns uint16_t value of the given property.
 * @param   uBase       The base of the number in the sysfs property.
 * @param   u16Def      The default to set on error.
 * @param   pszFormat   The format string for the property.
 * @param   ...         Arguments for the format string.
 */
static uint16_t usbsysfsReadDevicePropertyU16Def(unsigned uBase, uint16_t u16Def, const char *pszFormat, ...)
{
    int64_t i64Tmp = 0;

    va_list va;
    va_start(va, pszFormat);
    int vrc = RTLinuxSysFsReadIntFileV(uBase, &i64Tmp, pszFormat, va);
    va_end(va);
    if (RT_SUCCESS(vrc))
        return (uint16_t)i64Tmp;
    return u16Def;
}


static void usbsysfsFillInDevice(USBDEVICE *pDev, USBDeviceInfo *pInfo)
{
    int vrc;
    const char *pszSysfsPath = pInfo->mSysfsPath;

    /* Fill in the simple fields */
    pDev->enmState           = USBDEVICESTATE_UNUSED;
    pDev->bBus               = (uint8_t)usbsysfsGetBusFromPath(pszSysfsPath);
    pDev->bDeviceClass       = usbsysfsReadDevicePropertyU8Def(16, 0, "%s/bDeviceClass", pszSysfsPath);
    pDev->bDeviceSubClass    = usbsysfsReadDevicePropertyU8Def(16, 0, "%s/bDeviceSubClass", pszSysfsPath);
    pDev->bDeviceProtocol    = usbsysfsReadDevicePropertyU8Def(16, 0, "%s/bDeviceProtocol", pszSysfsPath);
    pDev->bNumConfigurations = usbsysfsReadDevicePropertyU8Def(10, 0, "%s/bNumConfigurations", pszSysfsPath);
    pDev->idVendor           = usbsysfsReadDevicePropertyU16Def(16, 0, "%s/idVendor", pszSysfsPath);
    pDev->idProduct          = usbsysfsReadDevicePropertyU16Def(16, 0, "%s/idProduct", pszSysfsPath);
    pDev->bDevNum            = usbsysfsReadDevicePropertyU8Def(10, 0, "%s/devnum", pszSysfsPath);

    /* Now deal with the non-numeric bits. */
    char szBuf[1024];  /* Should be larger than anything a sane device
                        * will need, and insane devices can be unsupported
                        * until further notice. */
    size_t cchRead;

    /* For simplicity, we just do strcmps on the next one. */
    vrc = RTLinuxSysFsReadStrFile(szBuf, sizeof(szBuf), &cchRead, "%s/speed", pszSysfsPath);
    if (RT_FAILURE(vrc) || cchRead == sizeof(szBuf))
        pDev->enmState = USBDEVICESTATE_UNSUPPORTED;
    else
        pDev->enmSpeed = !strcmp(szBuf, "1.5")  ? USBDEVICESPEED_LOW
                       : !strcmp(szBuf, "12")   ? USBDEVICESPEED_FULL
                       : !strcmp(szBuf, "480")  ? USBDEVICESPEED_HIGH
                       : !strcmp(szBuf, "5000") ? USBDEVICESPEED_SUPER
                       : USBDEVICESPEED_UNKNOWN;

    vrc = RTLinuxSysFsReadStrFile(szBuf, sizeof(szBuf), &cchRead, "%s/version", pszSysfsPath);
    if (RT_FAILURE(vrc) || cchRead == sizeof(szBuf))
        pDev->enmState = USBDEVICESTATE_UNSUPPORTED;
    else
    {
        vrc = usbsysfsConvertStrToBCD(szBuf, &pDev->bcdUSB);
        if (RT_FAILURE(vrc))
        {
            pDev->enmState = USBDEVICESTATE_UNSUPPORTED;
            pDev->bcdUSB   = UINT16_MAX;
        }
    }

    vrc = RTLinuxSysFsReadStrFile(szBuf, sizeof(szBuf), &cchRead, "%s/bcdDevice", pszSysfsPath);
    if (RT_FAILURE(vrc) || cchRead == sizeof(szBuf))
        pDev->bcdDevice = UINT16_MAX;
    else
    {
        vrc = usbsysfsConvertStrToBCD(szBuf, &pDev->bcdDevice);
        if (RT_FAILURE(vrc))
            pDev->bcdDevice = UINT16_MAX;
    }

    /* Now do things that need string duplication */
    vrc = RTLinuxSysFsReadStrFile(szBuf, sizeof(szBuf), &cchRead, "%s/product", pszSysfsPath);
    if (RT_SUCCESS(vrc) && cchRead < sizeof(szBuf))
    {
        USBLibPurgeEncoding(szBuf);
        pDev->pszProduct = RTStrDup(szBuf);
    }

    vrc = RTLinuxSysFsReadStrFile(szBuf, sizeof(szBuf), &cchRead, "%s/serial", pszSysfsPath);
    if (RT_SUCCESS(vrc) && cchRead < sizeof(szBuf))
    {
        USBLibPurgeEncoding(szBuf);
        pDev->pszSerialNumber = RTStrDup(szBuf);
        pDev->u64SerialHash = USBLibHashSerial(szBuf);
    }

    vrc = RTLinuxSysFsReadStrFile(szBuf, sizeof(szBuf), &cchRead, "%s/manufacturer", pszSysfsPath);
    if (RT_SUCCESS(vrc) && cchRead < sizeof(szBuf))
    {
        USBLibPurgeEncoding(szBuf);
        pDev->pszManufacturer = RTStrDup(szBuf);
    }

    /* Work out the port number */
    if (RT_FAILURE(usbsysfsGetPortFromStr(pszSysfsPath, &pDev->bPort)))
        pDev->enmState = USBDEVICESTATE_UNSUPPORTED;

    /* Check the interfaces to see if we can support the device. */
    char **ppszIf;
    VEC_FOR_EACH(&pInfo->mvecpszInterfaces, char *, ppszIf)
    {
        vrc = RTLinuxSysFsGetLinkDest(szBuf, sizeof(szBuf), NULL, "%s/driver", *ppszIf);
        if (RT_SUCCESS(vrc) && pDev->enmState != USBDEVICESTATE_UNSUPPORTED)
            pDev->enmState = (strcmp(szBuf, "hub") == 0)
                           ? USBDEVICESTATE_UNSUPPORTED
                           : USBDEVICESTATE_USED_BY_HOST_CAPTURABLE;
        if (usbsysfsReadDevicePropertyU8Def(16, 9 /* bDev */, "%s/bInterfaceClass", *ppszIf) == 9 /* hub */)
            pDev->enmState = USBDEVICESTATE_UNSUPPORTED;
    }

    /* We use a double slash as a separator in the pszAddress field.  This is
     * alright as the two paths can't contain a slash due to the way we build
     * them. */
    char *pszAddress = NULL;
    RTStrAPrintf(&pszAddress, "sysfs:%s//device:%s", pszSysfsPath, pInfo->mDevice);
    pDev->pszAddress = pszAddress;
    pDev->pszBackend = RTStrDup("host");

    /* Work out from the data collected whether we can support this device. */
    pDev->enmState = usbDeterminState(pDev);
    usbLogDevice(pDev);
}


/**
 * USBProxyService::getDevices() implementation for sysfs.
 */
static PUSBDEVICE usbsysfsGetDevices(const char *pszDevicesRoot, bool fUnsupportedDevicesToo)
{
    /* Add each of the devices found to the chain. */
    PUSBDEVICE pFirst = NULL;
    PUSBDEVICE pLast  = NULL;
    VECTOR_OBJ(USBDeviceInfo) vecDevInfo;
    USBDeviceInfo *pInfo;

    VEC_INIT_OBJ(&vecDevInfo, USBDeviceInfo, usbsysfsCleanupDevInfo);
    int vrc = usbsysfsEnumerateHostDevices(pszDevicesRoot, &vecDevInfo);
    if (RT_FAILURE(vrc))
        return NULL;
    VEC_FOR_EACH(&vecDevInfo, USBDeviceInfo, pInfo)
    {
        USBDEVICE *pDev = (USBDEVICE *)RTMemAllocZ(sizeof(USBDEVICE));
        if (!pDev)
            vrc = VERR_NO_MEMORY;
        if (RT_SUCCESS(vrc))
            usbsysfsFillInDevice(pDev, pInfo);
        if (   RT_SUCCESS(vrc)
            && (   pDev->enmState != USBDEVICESTATE_UNSUPPORTED
                || fUnsupportedDevicesToo)
            && pDev->pszAddress != NULL
           )
        {
            if (pLast != NULL)
            {
                pLast->pNext = pDev;
                pLast = pLast->pNext;
            }
            else
                pFirst = pLast = pDev;
        }
        else
            deviceFree(pDev);
        if (RT_FAILURE(vrc))
            break;
    }
    if (RT_FAILURE(vrc))
        deviceListFree(&pFirst);

    VEC_CLEANUP_OBJ(&vecDevInfo);
    return pFirst;
}

#endif /* VBOX_USB_WITH_SYSFS */
#ifdef UNIT_TEST

/* Set up mock functions for USBProxyLinuxCheckDeviceRoot - here dlsym and close
 * for the inotify presence check. */
static int testInotifyInitGood(void) { return 0; }
static int testInotifyInitBad(void) { return -1; }
static bool s_fHaveInotifyLibC = true;
static bool s_fHaveInotifyKernel = true;

static void *testDLSym(void *handle, const char *symbol)
{
    RT_NOREF(handle, symbol);
    Assert(handle == RTLD_DEFAULT);
    Assert(!RTStrCmp(symbol, "inotify_init"));
    if (!s_fHaveInotifyLibC)
        return NULL;
    if (s_fHaveInotifyKernel)
        return (void *)(uintptr_t)testInotifyInitGood;
    return (void *)(uintptr_t)testInotifyInitBad;
}

# define dlsym testDLSym
# define close(a) do {} while (0)

#endif /* UNIT_TEST */

/**
 * Is inotify available and working on this system?
 *
 * This is a requirement for using USB with sysfs
 */
static bool usbsysfsInotifyAvailable(void)
{
    int (*inotify_init)(void);

    *(void **)(&inotify_init) = dlsym(RTLD_DEFAULT, "inotify_init");
    if (!inotify_init)
        return false;
    int fd = inotify_init();
    if (fd == -1)
        return false;
    close(fd);
    return true;
}

#ifdef UNIT_TEST

# undef dlsym
# undef close

/** Unit test list of usbfs addresses of connected devices. */
static const char **g_papszUsbfsDeviceAddresses = NULL;

static PUSBDEVICE testGetUsbfsDevices(const char *pszUsbfsRoot, bool fUnsupportedDevicesToo)
{
    RT_NOREF(pszUsbfsRoot, fUnsupportedDevicesToo);
    const char **psz;
    PUSBDEVICE pList = NULL, pTail = NULL;
    for (psz = g_papszUsbfsDeviceAddresses; psz && *psz; ++psz)
    {
        PUSBDEVICE pNext = (PUSBDEVICE)RTMemAllocZ(sizeof(USBDEVICE));
        if (pNext)
            pNext->pszAddress = RTStrDup(*psz);
        if (!pNext || !pNext->pszAddress)
        {
            if (pNext)
                RTMemFree(pNext);
            deviceListFree(&pList);
            return NULL;
        }
        if (pTail)
            pTail->pNext = pNext;
        else
            pList = pNext;
        pTail = pNext;
    }
    return pList;
}
# define usbfsGetDevices testGetUsbfsDevices

/**
 * Specify the list of devices that will appear to be available through
 * usbfs during unit testing (of USBProxyLinuxGetDevices)
 * @param  pacszDeviceAddresses  NULL terminated array of usbfs device addresses
 */
void TestUSBSetAvailableUsbfsDevices(const char **papszDeviceAddresses)
{
    g_papszUsbfsDeviceAddresses = papszDeviceAddresses;
}

/** Unit test list of files reported as accessible by access(3).  We only do
 * accessible or not accessible. */
static const char **g_papszAccessibleFiles = NULL;

static int testAccess(const char *pszPath, int mode)
{
    RT_NOREF(mode);
    const char **psz;
    for (psz = g_papszAccessibleFiles; psz && *psz; ++psz)
        if (!RTStrCmp(pszPath, *psz))
            return 0;
    return -1;
}
# define access testAccess


/**
 * Specify the list of files that access will report as accessible (at present
 * we only do accessible or not accessible) during unit testing (of
 * USBProxyLinuxGetDevices)
 * @param  papszAccessibleFiles  NULL terminated array of file paths to be
 *                               reported accessible
 */
void TestUSBSetAccessibleFiles(const char **papszAccessibleFiles)
{
    g_papszAccessibleFiles = papszAccessibleFiles;
}


/** The path we pretend the usbfs root is located at, or NULL. */
const char *s_pszTestUsbfsRoot;
/** Should usbfs be accessible to the current user? */
bool s_fTestUsbfsAccessible;
/** The path we pretend the device node tree root is located at, or NULL. */
const char *s_pszTestDevicesRoot;
/** Should the device node tree be accessible to the current user? */
bool s_fTestDevicesAccessible;
/** The result of the usbfs/inotify-specific init */
int s_vrcTestMethodInitResult;
/** The value of the VBOX_USB environment variable. */
const char *s_pszTestEnvUsb;
/** The value of the VBOX_USB_ROOT environment variable. */
const char *s_pszTestEnvUsbRoot;


/** Select which access methods will be available to the @a init method
 * during unit testing, and (hack!) what return code it will see from
 * the access method-specific initialisation. */
void TestUSBSetupInit(const char *pszUsbfsRoot, bool fUsbfsAccessible,
                      const char *pszDevicesRoot, bool fDevicesAccessible,
                      int vrcMethodInitResult)
{
    s_pszTestUsbfsRoot = pszUsbfsRoot;
    s_fTestUsbfsAccessible = fUsbfsAccessible;
    s_pszTestDevicesRoot = pszDevicesRoot;
    s_fTestDevicesAccessible = fDevicesAccessible;
    s_vrcTestMethodInitResult = vrcMethodInitResult;
}


/** Specify the environment that the @a init method will see during unit
 * testing. */
void TestUSBSetEnv(const char *pszEnvUsb, const char *pszEnvUsbRoot)
{
    s_pszTestEnvUsb = pszEnvUsb;
    s_pszTestEnvUsbRoot = pszEnvUsbRoot;
}

/* For testing we redefine anything that accesses the outside world to
 * return test values. */
# define RTEnvGet(a) \
    (  !RTStrCmp(a, "VBOX_USB") ? s_pszTestEnvUsb \
     : !RTStrCmp(a, "VBOX_USB_ROOT") ? s_pszTestEnvUsbRoot \
     : NULL)
# define USBProxyLinuxCheckDeviceRoot(pszPath, fUseNodes) \
    (   ((fUseNodes) && s_fTestDevicesAccessible \
         && !RTStrCmp(pszPath, s_pszTestDevicesRoot)) \
     || (!(fUseNodes) && s_fTestUsbfsAccessible \
         && !RTStrCmp(pszPath, s_pszTestUsbfsRoot)))
# define RTDirExists(pszDir) \
    (   (pszDir) \
     && (   !RTStrCmp(pszDir, s_pszTestDevicesRoot) \
         || !RTStrCmp(pszDir, s_pszTestUsbfsRoot)))
# define RTFileExists(pszFile) \
    (   (pszFile) \
     && s_pszTestUsbfsRoot \
     && !RTStrNCmp(pszFile, s_pszTestUsbfsRoot, strlen(s_pszTestUsbfsRoot)) \
     && !RTStrCmp(pszFile + strlen(s_pszTestUsbfsRoot), "/devices"))

#endif /* UNIT_TEST */

/**
 * Use USBFS-like or sysfs/device node-like access method?
 *
 * Selects the access method that will be used to access USB devices based on
 * what is available on the host and what if anything the user has specified
 * in the environment.
 *
 * @returns iprt status value
 * @param  pfUsingUsbfsDevices  on success this will be set to true if
 *                              the prefered access method is USBFS-like and to
 *                              false if it is sysfs/device node-like
 * @param  ppszDevicesRoot     on success the root of the tree of USBFS-like
 *                              device nodes will be stored here
 */
int USBProxyLinuxChooseMethod(bool *pfUsingUsbfsDevices, const char **ppszDevicesRoot)
{
    /*
     * We have two methods available for getting host USB device data - using
     * USBFS and using sysfs.  The default choice is sysfs; if that is not
     * available we fall back to USBFS.
     * In the event of both failing, an appropriate error will be returned.
     * The user may also specify a method and root using the VBOX_USB and
     * VBOX_USB_ROOT environment variables.  In this case we don't check
     * the root they provide for validity.
     */
    bool fUsbfsChosen = false;
    bool fSysfsChosen = false;
    const char *pszUsbFromEnv = RTEnvGet("VBOX_USB");
    const char *pszUsbRoot = NULL;
    if (pszUsbFromEnv)
    {
        bool fValidVBoxUSB = true;

        pszUsbRoot = RTEnvGet("VBOX_USB_ROOT");
        if (!RTStrICmp(pszUsbFromEnv, "USBFS"))
        {
            LogRel(("Default USB access method set to \"usbfs\" from environment\n"));
            fUsbfsChosen = true;
        }
        else if (!RTStrICmp(pszUsbFromEnv, "SYSFS"))
        {
            LogRel(("Default USB method set to \"sysfs\" from environment\n"));
            fSysfsChosen = true;
        }
        else
        {
            LogRel(("Invalid VBOX_USB environment variable setting \"%s\"\n", pszUsbFromEnv));
            fValidVBoxUSB = false;
            pszUsbFromEnv = NULL;
        }
        if (!fValidVBoxUSB && pszUsbRoot)
            pszUsbRoot = NULL;
    }
    if (!pszUsbRoot)
    {
        if (   !fUsbfsChosen
            && USBProxyLinuxCheckDeviceRoot("/dev/vboxusb", true))
        {
            fSysfsChosen = true;
            pszUsbRoot = "/dev/vboxusb";
        }
        else if (   !fSysfsChosen
                 && USBProxyLinuxCheckDeviceRoot("/proc/bus/usb", false))
        {
            fUsbfsChosen = true;
            pszUsbRoot = "/proc/bus/usb";
        }
    }
    else if (!USBProxyLinuxCheckDeviceRoot(pszUsbRoot, fSysfsChosen))
        pszUsbRoot = NULL;
    if (pszUsbRoot)
    {
        *pfUsingUsbfsDevices = fUsbfsChosen;
        *ppszDevicesRoot = pszUsbRoot;
        return VINF_SUCCESS;
    }
    /* else */
    return pszUsbFromEnv ? VERR_NOT_FOUND
         : RTDirExists("/dev/vboxusb") ? VERR_VUSB_USB_DEVICE_PERMISSION
         : RTFileExists("/proc/bus/usb/devices") ? VERR_VUSB_USBFS_PERMISSION
         : VERR_NOT_FOUND;
}

#ifdef UNIT_TEST
# undef RTEnvGet
# undef USBProxyLinuxCheckDeviceRoot
# undef RTDirExists
# undef RTFileExists
#endif

/**
 * Check whether a USB device tree root is usable.
 *
 * @param pszRoot        the path to the root of the device tree
 * @param fIsDeviceNodes  whether this is a device node (or usbfs) tree
 * @note  returns a pointer into a static array so it will stay valid
 */
bool USBProxyLinuxCheckDeviceRoot(const char *pszRoot, bool fIsDeviceNodes)
{
    bool fOK = false;
    if (!fIsDeviceNodes)  /* usbfs */
    {
#ifdef VBOX_USB_WITH_USBFS
        if (!access(pszRoot, R_OK | X_OK))
        {
            fOK = true;
            PUSBDEVICE pDevices = usbfsGetDevices(pszRoot, true);
            if (pDevices)
            {
                PUSBDEVICE pDevice;
                for (pDevice = pDevices; pDevice && fOK; pDevice = pDevice->pNext)
                    if (access(pDevice->pszAddress, R_OK | W_OK))
                        fOK = false;
                deviceListFree(&pDevices);
            }
        }
#endif
    }
#ifdef VBOX_USB_WITH_SYSFS
    /* device nodes */
    else if (usbsysfsInotifyAvailable() && !access(pszRoot, R_OK | X_OK))
        fOK = true;
#endif
    return fOK;
}

#ifdef UNIT_TEST
# undef usbfsGetDevices
# undef access
#endif

/**
 * Get the list of USB devices supported by the system.
 *
 * Result should be freed using #deviceFree or something equivalent.
 *
 * @param pszDevicesRoot  the path to the root of the device tree
 * @param fUseSysfs        whether to use sysfs (or usbfs) for enumeration
 */
PUSBDEVICE USBProxyLinuxGetDevices(const char *pszDevicesRoot, bool fUseSysfs)
{
    if (!fUseSysfs)
    {
#ifdef VBOX_USB_WITH_USBFS
        return usbfsGetDevices(pszDevicesRoot, false);
#else
        return NULL;
#endif
    }

#ifdef VBOX_USB_WITH_SYSFS
    return usbsysfsGetDevices(pszDevicesRoot, false);
#else
    return NULL;
#endif
}

