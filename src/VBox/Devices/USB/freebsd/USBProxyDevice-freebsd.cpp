/* $Id: USBProxyDevice-freebsd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * USB device proxy - the FreeBSD backend.
 */

/*
 * Includes contributions from Hans Petter Selasky
 *
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
#define LOG_GROUP LOG_GROUP_DRV_USBPROXY
#ifdef VBOX
# include <iprt/stdint.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usb_ioctl.h>

#include <VBox/vmm/pdm.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/vusb.h>
#include <iprt/assert.h>
#include <iprt/stream.h>
#include <iprt/alloc.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <iprt/file.h>
#include <iprt/pipe.h>
#include "../USBProxyDevice.h"

/** Maximum endpoints supported. */
#define USBFBSD_MAXENDPOINTS 127
#define USBFBSD_MAXFRAMES 56

/** This really needs to be defined in vusb.h! */
#ifndef VUSB_DIR_TO_DEV
# define VUSB_DIR_TO_DEV        0x00
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct USBENDPOINTFBSD
{
    /** Flag whether it is opened. */
    bool     fOpen;
    /** Flag whether it is cancelling. */
    bool     fCancelling;
    /** Buffer pointers. */
    void    *apvData[USBFBSD_MAXFRAMES];
    /** Buffer lengths. */
    uint32_t acbData[USBFBSD_MAXFRAMES];
    /** Initial buffer length. */
    uint32_t cbData0;
    /** Pointer to the URB. */
    PVUSBURB pUrb;
    /** Copy of endpoint number. */
    unsigned iEpNum;
    /** Maximum transfer length. */
    unsigned cMaxIo;
    /** Maximum frame count. */
    unsigned cMaxFrames;
} USBENDPOINTFBSD, *PUSBENDPOINTFBSD;

/**
 * Data for the FreeBSD usb proxy backend.
 */
typedef struct USBPROXYDEVFBSD
{
    /** The open file. */
    RTFILE                 hFile;
    /** Flag whether an URB is cancelling. */
    bool                   fCancelling;
    /** Flag whether initialised or not */
    bool                   fInit;
    /** Pipe handle for waking up - writing end. */
    RTPIPE                 hPipeWakeupW;
    /** Pipe handle for waking up - reading end. */
    RTPIPE                 hPipeWakeupR;
    /** Software endpoint structures */
    USBENDPOINTFBSD        aSwEndpoint[USBFBSD_MAXENDPOINTS];
    /** Kernel endpoint structures */
    struct usb_fs_endpoint aHwEndpoint[USBFBSD_MAXENDPOINTS];
} USBPROXYDEVFBSD, *PUSBPROXYDEVFBSD;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int usbProxyFreeBSDEndpointClose(PUSBPROXYDEV pProxyDev, int Endpoint);

/**
 * Wrapper for the ioctl call.
 *
 * This wrapper will repeat the call if we get an EINTR or EAGAIN. It can also
 * handle ENODEV (detached device) errors.
 *
 * @returns whatever ioctl returns.
 * @param   pProxyDev       The proxy device.
 * @param   iCmd            The ioctl command / function.
 * @param   pvArg           The ioctl argument / data.
 * @param   fHandleNoDev    Whether to handle ENXIO.
 * @internal
 */
static int usbProxyFreeBSDDoIoCtl(PUSBPROXYDEV pProxyDev, unsigned long iCmd,
                                  void *pvArg, bool fHandleNoDev)
{
    int rc = VINF_SUCCESS;
    PUSBPROXYDEVFBSD pDevFBSD = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVFBSD);

    LogFlow(("usbProxyFreeBSDDoIoCtl: iCmd=%#x\n", iCmd));

    do
    {
        rc = ioctl(RTFileToNative(pDevFBSD->hFile), iCmd, pvArg);
        if (rc >= 0)
            return VINF_SUCCESS;
    } while (errno == EINTR);

    if (errno == ENXIO && fHandleNoDev)
    {
        Log(("usbProxyFreeBSDDoIoCtl: ENXIO -> unplugged. pProxyDev=%s\n",
             pProxyDev->pUsbIns->pszName));
        errno = ENODEV;
    }
    else if (errno != EAGAIN)
    {
        LogFlow(("usbProxyFreeBSDDoIoCtl: Returned %d. pProxyDev=%s\n",
                 errno, pProxyDev->pUsbIns->pszName));
    }
    return RTErrConvertFromErrno(errno);
}

/**
 * Init USB subsystem.
 */
static int usbProxyFreeBSDFsInit(PUSBPROXYDEV pProxyDev)
{
    struct usb_fs_init UsbFsInit;
    PUSBPROXYDEVFBSD pDevFBSD = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVFBSD);
    int rc;

    LogFlow(("usbProxyFreeBSDFsInit: pProxyDev=%p\n", (void *)pProxyDev));

    /* Sanity check */
    AssertPtrReturn(pDevFBSD, VERR_INVALID_PARAMETER);

    if (pDevFBSD->fInit == true)
        return VINF_SUCCESS;

    /* Zero default */
    memset(&UsbFsInit, 0, sizeof(UsbFsInit));

    UsbFsInit.pEndpoints = pDevFBSD->aHwEndpoint;
    UsbFsInit.ep_index_max = USBFBSD_MAXENDPOINTS;

    /* Init USB subsystem */
    rc = usbProxyFreeBSDDoIoCtl(pProxyDev, USB_FS_INIT, &UsbFsInit, false);
    if (RT_SUCCESS(rc))
        pDevFBSD->fInit = true;

    return rc;
}

/**
 * Uninit USB subsystem.
 */
static int usbProxyFreeBSDFsUnInit(PUSBPROXYDEV pProxyDev)
{
    struct usb_fs_uninit UsbFsUninit;
    PUSBPROXYDEVFBSD pDevFBSD = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVFBSD);
    int rc;

    LogFlow(("usbProxyFreeBSDFsUnInit: ProxyDev=%p\n", (void *)pProxyDev));

    /* Sanity check */
    AssertPtrReturn(pDevFBSD, VERR_INVALID_PARAMETER);

    if (pDevFBSD->fInit != true)
        return VINF_SUCCESS;

    /* Close any open endpoints. */
    for (unsigned n = 0; n != USBFBSD_MAXENDPOINTS; n++)
        usbProxyFreeBSDEndpointClose(pProxyDev, n);

    /* Zero default */
    memset(&UsbFsUninit, 0, sizeof(UsbFsUninit));

    /* Uninit USB subsystem */
    rc = usbProxyFreeBSDDoIoCtl(pProxyDev, USB_FS_UNINIT, &UsbFsUninit, false);
    if (RT_SUCCESS(rc))
        pDevFBSD->fInit = false;

    return rc;
}

/**
 * Setup a USB request packet.
 */
static void usbProxyFreeBSDSetupReq(struct usb_device_request *pSetupData,
                                    uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue,
                                    uint16_t wIndex, uint16_t wLength)
{
    LogFlow(("usbProxyFreeBSDSetupReq: pSetupData=%p bmRequestType=%x "
             "bRequest=%x wValue=%x wIndex=%x wLength=%x\n", (void *)pSetupData,
             bmRequestType, bRequest, wValue, wIndex, wLength));

    pSetupData->bmRequestType = bmRequestType;
    pSetupData->bRequest = bRequest;

    /* Handle endianess here. Currently no swapping is needed. */
    pSetupData->wValue[0] = wValue & 0xff;
    pSetupData->wValue[1] = (wValue >> 8) & 0xff;
    pSetupData->wIndex[0] = wIndex & 0xff;
    pSetupData->wIndex[1] = (wIndex >> 8) & 0xff;
    pSetupData->wLength[0] = wLength & 0xff;
    pSetupData->wLength[1] = (wLength >> 8) & 0xff;
}

static int usbProxyFreeBSDEndpointOpen(PUSBPROXYDEV pProxyDev, int Endpoint, bool fIsoc, int index)
{
    PUSBPROXYDEVFBSD pDevFBSD = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVFBSD);
    PUSBENDPOINTFBSD pEndpointFBSD = NULL; /* shut up gcc */
    struct usb_fs_endpoint *pXferEndpoint;
    struct usb_fs_open UsbFsOpen;
    int rc;

    LogFlow(("usbProxyFreeBSDEndpointOpen: pProxyDev=%p Endpoint=%d\n",
             (void *)pProxyDev, Endpoint));

    for (; index < USBFBSD_MAXENDPOINTS; index++)
    {
        pEndpointFBSD = &pDevFBSD->aSwEndpoint[index];
        if (pEndpointFBSD->fCancelling)
            continue;
        if (   pEndpointFBSD->fOpen
            && !pEndpointFBSD->pUrb
            && (int)pEndpointFBSD->iEpNum == Endpoint)
            return index;
    }

    if (index == USBFBSD_MAXENDPOINTS)
    {
        for (index = 0; index != USBFBSD_MAXENDPOINTS; index++)
        {
            pEndpointFBSD = &pDevFBSD->aSwEndpoint[index];
            if (pEndpointFBSD->fCancelling)
                continue;
            if (!pEndpointFBSD->fOpen)
                break;
        }
        if (index == USBFBSD_MAXENDPOINTS)
            return -1;
    }
    /* set ppBuffer and pLength */

    pXferEndpoint = &pDevFBSD->aHwEndpoint[index];
    pXferEndpoint->ppBuffer = &pEndpointFBSD->apvData[0];
    pXferEndpoint->pLength = &pEndpointFBSD->acbData[0];

    LogFlow(("usbProxyFreeBSDEndpointOpen: ep_index=%d ep_num=%d\n",
             index, Endpoint));

    memset(&UsbFsOpen, 0, sizeof(UsbFsOpen));

    UsbFsOpen.ep_index = index;
    UsbFsOpen.ep_no = Endpoint;
    UsbFsOpen.max_bufsize = 256 * 1024;
    /* Hardcoded assumption about the URBs we get. */

    UsbFsOpen.max_frames = fIsoc ? USBFBSD_MAXFRAMES : 2;

    rc = usbProxyFreeBSDDoIoCtl(pProxyDev, USB_FS_OPEN, &UsbFsOpen, true);
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_RESOURCE_BUSY)
            LogFlow(("usbProxyFreeBSDEndpointOpen: EBUSY\n"));

        return -1;
    }
    pEndpointFBSD->fOpen = true;
    pEndpointFBSD->pUrb = NULL;
    pEndpointFBSD->iEpNum = Endpoint;
    pEndpointFBSD->cMaxIo = UsbFsOpen.max_bufsize;
    pEndpointFBSD->cMaxFrames = UsbFsOpen.max_frames;

    return index;
}

/**
 * Close an endpoint.
 *
 * @returns VBox status code.
 */
static int usbProxyFreeBSDEndpointClose(PUSBPROXYDEV pProxyDev, int Endpoint)
{
    PUSBPROXYDEVFBSD pDevFBSD = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVFBSD);
    PUSBENDPOINTFBSD pEndpointFBSD = &pDevFBSD->aSwEndpoint[Endpoint];
    struct usb_fs_close UsbFsClose;
    int rc = VINF_SUCCESS;

    LogFlow(("usbProxyFreeBSDEndpointClose: pProxyDev=%p Endpoint=%d\n",
             (void *)pProxyDev, Endpoint));

    /* check for cancelling */
    if (pEndpointFBSD->pUrb != NULL)
    {
        pEndpointFBSD->fCancelling = true;
        pDevFBSD->fCancelling = true;
    }

    /* check for opened */
    if (pEndpointFBSD->fOpen)
    {
        pEndpointFBSD->fOpen = false;

        /* Zero default */
        memset(&UsbFsClose, 0, sizeof(UsbFsClose));

        /* Set endpoint index */
        UsbFsClose.ep_index = Endpoint;

        /* Close endpoint */
        rc = usbProxyFreeBSDDoIoCtl(pProxyDev, USB_FS_CLOSE, &UsbFsClose, true);
    }
    return rc;
}

/**
 * Opens the device file.
 *
 * @returns VBox status code.
 * @param   pProxyDev       The device instance.
 * @param   pszAddress      If we are using usbfs, this is the path to the
 *                          device.  If we are using sysfs, this is a string of
 *                          the form "sysfs:<sysfs path>//device:<device node>".
 *                          In the second case, the two paths are guaranteed
 *                          not to contain the substring "//".
 */
static DECLCALLBACK(int) usbProxyFreeBSDOpen(PUSBPROXYDEV pProxyDev, const char *pszAddress)
{
    PUSBPROXYDEVFBSD pDevFBSD = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVFBSD);
    int rc;

    LogFlow(("usbProxyFreeBSDOpen: pProxyDev=%p pszAddress=%s\n", pProxyDev, pszAddress));

    /*
     * Try open the device node.
     */
    RTFILE hFile;
    rc = RTFileOpen(&hFile, pszAddress, RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
    if (RT_SUCCESS(rc))
    {
        /*
         * Initialize the FreeBSD backend data.
         */
        pDevFBSD->hFile = hFile;
        rc = usbProxyFreeBSDFsInit(pProxyDev);
        if (RT_SUCCESS(rc))
        {
            /*
             * Create wakeup pipe.
             */
            rc = RTPipeCreate(&pDevFBSD->hPipeWakeupR, &pDevFBSD->hPipeWakeupW, 0);
            if (RT_SUCCESS(rc))
            {
                LogFlow(("usbProxyFreeBSDOpen(%p, %s): returns successfully hFile=%RTfile iActiveCfg=%d\n",
                         pProxyDev, pszAddress, pDevFBSD->hFile, pProxyDev->iActiveCfg));

                return VINF_SUCCESS;
            }
        }

        RTFileClose(hFile);
    }
    else if (rc == VERR_ACCESS_DENIED)
        rc = VERR_VUSB_USBFS_PERMISSION;

    Log(("usbProxyFreeBSDOpen(%p, %s) failed, rc=%d!\n",
         pProxyDev, pszAddress, rc));

    return rc;
}


/**
 * Claims all the interfaces and figures out the
 * current configuration.
 *
 * @returns VINF_SUCCESS.
 * @param   pProxyDev       The proxy device.
 */
static DECLCALLBACK(int) usbProxyFreeBSDInit(PUSBPROXYDEV pProxyDev)
{
    PUSBPROXYDEVFBSD pDevFBSD = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVFBSD);
    int rc;

    LogFlow(("usbProxyFreeBSDInit: pProxyDev=%s\n",
             pProxyDev->pUsbIns->pszName));

    /* Retrieve current active configuration. */
    rc = usbProxyFreeBSDDoIoCtl(pProxyDev, USB_GET_CONFIG,
                                &pProxyDev->iActiveCfg, true);
    if (RT_FAILURE(rc) || pProxyDev->iActiveCfg == 255)
    {
        pProxyDev->cIgnoreSetConfigs = 0;
        pProxyDev->iActiveCfg = -1;
    }
    else
    {
        pProxyDev->cIgnoreSetConfigs = 1;
        pProxyDev->iActiveCfg++;
    }

    Log(("usbProxyFreeBSDInit: iActiveCfg=%d\n", pProxyDev->iActiveCfg));

    return rc;
}

/**
 * Closes the proxy device.
 */
static DECLCALLBACK(void) usbProxyFreeBSDClose(PUSBPROXYDEV pProxyDev)
{
    PUSBPROXYDEVFBSD pDevFBSD = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVFBSD);

    LogFlow(("usbProxyFreeBSDClose: pProxyDev=%s\n", pProxyDev->pUsbIns->pszName));

    /* sanity check */
    AssertPtrReturnVoid(pDevFBSD);

    usbProxyFreeBSDFsUnInit(pProxyDev);

    RTPipeClose(pDevFBSD->hPipeWakeupR);
    RTPipeClose(pDevFBSD->hPipeWakeupW);

    RTFileClose(pDevFBSD->hFile);
    pDevFBSD->hFile = NIL_RTFILE;

    LogFlow(("usbProxyFreeBSDClose: returns\n"));
}

/**
 * Reset a device.
 *
 * @returns VBox status code.
 * @param   pDev    The device to reset.
 */
static DECLCALLBACK(int) usbProxyFreeBSDReset(PUSBPROXYDEV pProxyDev, bool fResetOnFreeBSD)
{
    PUSBPROXYDEVFBSD pDevFBSD = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVFBSD);
    int iParm;
    int rc = VINF_SUCCESS;

    LogFlow(("usbProxyFreeBSDReset: pProxyDev=%s\n",
             pProxyDev->pUsbIns->pszName));

    if (!fResetOnFreeBSD)
        goto done;

    /* We need to release kernel ressources first. */
    rc = usbProxyFreeBSDFsUnInit(pProxyDev);
    if (RT_FAILURE(rc))
        goto done;

    /* Resetting is only possible as super-user, ignore any failures: */
    iParm = 0;
    rc = usbProxyFreeBSDDoIoCtl(pProxyDev, USB_DEVICEENUMERATE, &iParm, true);
    if (RT_FAILURE(rc))
    {
        /* Set the config instead of bus reset */
        iParm = 255;
        rc = usbProxyFreeBSDDoIoCtl(pProxyDev, USB_SET_CONFIG, &iParm, true);
        if (RT_SUCCESS(rc))
        {
            iParm = 0;
            rc = usbProxyFreeBSDDoIoCtl(pProxyDev, USB_SET_CONFIG, &iParm, true);
        }
    }
    usleep(10000); /* nice it! */

    /* Allocate kernel ressources again. */
    rc = usbProxyFreeBSDFsInit(pProxyDev);
    if (RT_FAILURE(rc))
        goto done;

    /* Retrieve current active configuration. */
    rc = usbProxyFreeBSDInit(pProxyDev);

done:
    pProxyDev->cIgnoreSetConfigs = 2;

    return rc;
}

/**
 * SET_CONFIGURATION.
 *
 * The caller makes sure that it's not called first time after open or reset
 * with the active interface.
 *
 * @returns success indicator.
 * @param   pProxyDev       The device instance data.
 * @param   iCfg            The configuration to set.
 */
static DECLCALLBACK(int) usbProxyFreeBSDSetConfig(PUSBPROXYDEV pProxyDev, int iCfg)
{
    PUSBPROXYDEVFBSD pDevFBSD = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVFBSD);
    int iCfgIndex;
    int rc;

    LogFlow(("usbProxyFreeBSDSetConfig: pProxyDev=%s cfg=%x\n",
             pProxyDev->pUsbIns->pszName, iCfg));

    /* We need to release kernel ressources first. */
    rc = usbProxyFreeBSDFsUnInit(pProxyDev);
    if (RT_FAILURE(rc))
    {
        LogFlow(("usbProxyFreeBSDSetInterface: Freeing kernel resources "
                 "failed failed rc=%d\n", rc));
        return rc;
    }

    if (iCfg == 0)
    {
        /* Unconfigure */
        iCfgIndex = 255;
    }
    else
    {
        /* Get the configuration index matching the value. */
        for (iCfgIndex = 0; iCfgIndex < pProxyDev->DevDesc.bNumConfigurations; iCfgIndex++)
        {
            if (pProxyDev->paCfgDescs[iCfgIndex].Core.bConfigurationValue == iCfg)
                break;
        }

        if (iCfgIndex == pProxyDev->DevDesc.bNumConfigurations)
        {
            LogFlow(("usbProxyFreeBSDSetConfig: configuration "
                     "%d not found\n", iCfg));
            return VERR_NOT_FOUND;
        }
    }

    /* Set the config */
    rc = usbProxyFreeBSDDoIoCtl(pProxyDev, USB_SET_CONFIG, &iCfgIndex, true);
    if (RT_FAILURE(rc))
        return rc;

    /* Allocate kernel ressources again. */
    return usbProxyFreeBSDFsInit(pProxyDev);
}

/**
 * Claims an interface.
 * @returns success indicator.
 */
static DECLCALLBACK(int) usbProxyFreeBSDClaimInterface(PUSBPROXYDEV pProxyDev, int iIf)
{
    int rc;

    LogFlow(("usbProxyFreeBSDClaimInterface: pProxyDev=%s "
             "ifnum=%x\n", pProxyDev->pUsbIns->pszName, iIf));

    /*
     * Try to detach kernel driver on this interface, ignore any
     * failures
     */
    usbProxyFreeBSDDoIoCtl(pProxyDev, USB_IFACE_DRIVER_DETACH, &iIf, true);

    /* Try to claim interface */
    return usbProxyFreeBSDDoIoCtl(pProxyDev, USB_CLAIM_INTERFACE, &iIf, true);
}

/**
 * Releases an interface.
 * @returns success indicator.
 */
static DECLCALLBACK(int) usbProxyFreeBSDReleaseInterface(PUSBPROXYDEV pProxyDev, int iIf)
{
    int rc;

    LogFlow(("usbProxyFreeBSDReleaseInterface: pProxyDev=%s "
        "ifnum=%x\n", pProxyDev->pUsbIns->pszName, iIf));

    return usbProxyFreeBSDDoIoCtl(pProxyDev, USB_RELEASE_INTERFACE, &iIf, true);
}

/**
 * SET_INTERFACE.
 *
 * @returns success indicator.
 */
static DECLCALLBACK(int) usbProxyFreeBSDSetInterface(PUSBPROXYDEV pProxyDev, int iIf, int iAlt)
{
    PUSBPROXYDEVFBSD pDevFBSD = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVFBSD);
    struct usb_alt_interface UsbIntAlt;
    int rc;

    LogFlow(("usbProxyFreeBSDSetInterface: pProxyDev=%p iIf=%x iAlt=%x\n",
             pProxyDev, iIf, iAlt));

    /* We need to release kernel ressources first. */
    rc = usbProxyFreeBSDFsUnInit(pProxyDev);
    if (RT_FAILURE(rc))
    {
        LogFlow(("usbProxyFreeBSDSetInterface: Freeing kernel resources "
                 "failed failed rc=%d\n", rc));
        return rc;
    }
    memset(&UsbIntAlt, 0, sizeof(UsbIntAlt));
    UsbIntAlt.uai_interface_index = iIf;
    UsbIntAlt.uai_alt_index = iAlt;

    rc = usbProxyFreeBSDDoIoCtl(pProxyDev, USB_SET_ALTINTERFACE, &UsbIntAlt, true);
    if (RT_FAILURE(rc))
    {
        LogFlow(("usbProxyFreeBSDSetInterface: Setting interface %d %d "
                 "failed rc=%d\n", iIf, iAlt, rc));
        return rc;
    }

    return usbProxyFreeBSDFsInit(pProxyDev);
}

/**
 * Clears the halted endpoint 'ep_num'.
 */
static DECLCALLBACK(int) usbProxyFreeBSDClearHaltedEp(PUSBPROXYDEV pProxyDev, unsigned int ep_num)
{
    LogFlow(("usbProxyFreeBSDClearHaltedEp: pProxyDev=%s ep_num=%u\n",
             pProxyDev->pUsbIns->pszName, ep_num));

    /*
     * Clearing the zero control pipe doesn't make sense.
     * Just ignore it.
     */
    if ((ep_num & 0xF) == 0)
        return VINF_SUCCESS;

    struct usb_ctl_request Req;
    memset(&Req, 0, sizeof(Req));
    usbProxyFreeBSDSetupReq(&Req.ucr_request,
                            VUSB_DIR_TO_DEV | VUSB_TO_ENDPOINT,
                            VUSB_REQ_CLEAR_FEATURE, 0, ep_num, 0);

    int rc = usbProxyFreeBSDDoIoCtl(pProxyDev, USB_DO_REQUEST, &Req, true);

    LogFlow(("usbProxyFreeBSDClearHaltedEp: rc=%Rrc\n", rc));
    return rc;
}

/**
 * @interface_method_impl{USBPROXYBACK,pfnUrbQueue}
 */
static DECLCALLBACK(int) usbProxyFreeBSDUrbQueue(PUSBPROXYDEV pProxyDev, PVUSBURB pUrb)
{
    PUSBPROXYDEVFBSD pDevFBSD = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVFBSD);
    PUSBENDPOINTFBSD pEndpointFBSD;
    struct usb_fs_endpoint *pXferEndpoint;
    struct usb_fs_start UsbFsStart;
    unsigned cFrames;
    uint8_t *pbData;
    int index;
    int ep_num;
    int rc;

    LogFlow(("usbProxyFreeBSDUrbQueue: pUrb=%p EndPt=%u Dir=%u\n",
             pUrb, (unsigned)pUrb->EndPt, (unsigned)pUrb->enmDir));

    ep_num = pUrb->EndPt;
    if ((pUrb->enmType != VUSBXFERTYPE_MSG) && (pUrb->enmDir == VUSBDIRECTION_IN)) {
        /* set IN-direction bit */
        ep_num |= 0x80;
    }

    index = 0;

retry:

    index = usbProxyFreeBSDEndpointOpen(pProxyDev, ep_num,
                                        (pUrb->enmType == VUSBXFERTYPE_ISOC),
                                        index);

    if (index < 0)
        return VERR_INVALID_PARAMETER;

    pEndpointFBSD = &pDevFBSD->aSwEndpoint[index];
    pXferEndpoint = &pDevFBSD->aHwEndpoint[index];

    pbData = pUrb->abData;

    switch (pUrb->enmType)
    {
        case VUSBXFERTYPE_MSG:
        {
            pEndpointFBSD->apvData[0] = pbData;
            pEndpointFBSD->acbData[0] = 8;

            /* check wLength */
            if (pbData[6] || pbData[7])
            {
                pEndpointFBSD->apvData[1] = pbData + 8;
                pEndpointFBSD->acbData[1] = pbData[6] | (pbData[7] << 8);
                cFrames = 2;
            }
            else
            {
                pEndpointFBSD->apvData[1] = NULL;
                pEndpointFBSD->acbData[1] = 0;
                cFrames = 1;
            }

            LogFlow(("usbProxyFreeBSDUrbQueue: pUrb->cbData=%u, 0x%02x, "
                     "0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
                     pUrb->cbData, pbData[0], pbData[1], pbData[2], pbData[3],
                     pbData[4], pbData[5], pbData[6], pbData[7]));

            pXferEndpoint->timeout = USB_FS_TIMEOUT_NONE;
            pXferEndpoint->flags = USB_FS_FLAG_MULTI_SHORT_OK;
            break;
        }
        case VUSBXFERTYPE_ISOC:
        {
            unsigned i;

            for (i = 0; i < pUrb->cIsocPkts; i++)
            {
                if (i >= pEndpointFBSD->cMaxFrames)
                    break;
                pEndpointFBSD->apvData[i] = pbData + pUrb->aIsocPkts[i].off;
                pEndpointFBSD->acbData[i] = pUrb->aIsocPkts[i].cb;
            }
            /* Timeout handling will be done during reap. */
            pXferEndpoint->timeout = USB_FS_TIMEOUT_NONE;
            pXferEndpoint->flags = USB_FS_FLAG_MULTI_SHORT_OK;
            cFrames = i;
            break;
        }
        default:
        {
            pEndpointFBSD->apvData[0] = pbData;
            pEndpointFBSD->cbData0 = pUrb->cbData;

            /* XXX maybe we have to loop */
            if (pUrb->cbData > pEndpointFBSD->cMaxIo)
                pEndpointFBSD->acbData[0] = pEndpointFBSD->cMaxIo;
            else
                pEndpointFBSD->acbData[0] = pUrb->cbData;

            /* Timeout handling will be done during reap. */
            pXferEndpoint->timeout = USB_FS_TIMEOUT_NONE;
            pXferEndpoint->flags = pUrb->fShortNotOk ? 0 : USB_FS_FLAG_MULTI_SHORT_OK;
            cFrames = 1;
            break;
        }
    }

    /* store number of frames */
    pXferEndpoint->nFrames = cFrames;

    /* zero-default */
    memset(&UsbFsStart, 0, sizeof(UsbFsStart));

    /* Start the transfer */
    UsbFsStart.ep_index = index;

    rc = usbProxyFreeBSDDoIoCtl(pProxyDev, USB_FS_START, &UsbFsStart, true);

    LogFlow(("usbProxyFreeBSDUrbQueue: USB_FS_START returned rc=%d "
             "len[0]=%u len[1]=%u cbData=%u index=%u ep_num=%u\n", rc,
             (unsigned)pEndpointFBSD->acbData[0],
             (unsigned)pEndpointFBSD->acbData[1],
             (unsigned)pUrb->cbData,
             (unsigned)index, (unsigned)ep_num));

    if (RT_FAILURE(rc))
    {
        if (rc == VERR_RESOURCE_BUSY)
        {
            index++;
            goto retry;
        }
        return rc;
    }
    pUrb->Dev.pvPrivate = (void *)(long)(index + 1);
    pEndpointFBSD->pUrb = pUrb;

    return rc;
}

/**
 * Reap URBs in-flight on a device.
 *
 * @returns Pointer to a completed URB.
 * @returns NULL if no URB was completed.
 * @param   pProxyDev   The device.
 * @param   cMillies    Number of milliseconds to wait. Use 0 to not wait at all.
 */
static DECLCALLBACK(PVUSBURB) usbProxyFreeBSDUrbReap(PUSBPROXYDEV pProxyDev, RTMSINTERVAL cMillies)
{
    struct usb_fs_endpoint *pXferEndpoint;
    PUSBPROXYDEVFBSD pDevFBSD = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVFBSD);
    PUSBENDPOINTFBSD pEndpointFBSD;
    PVUSBURB pUrb;
    struct usb_fs_complete UsbFsComplete;
    struct pollfd pfd[2];
    int rc;

    LogFlow(("usbProxyFreeBSDUrbReap: pProxyDev=%p, cMillies=%u\n",
             pProxyDev, cMillies));

repeat:

    pUrb = NULL;

    /* check for cancelled transfers */
    if (pDevFBSD->fCancelling)
    {
        for (unsigned n = 0; n < USBFBSD_MAXENDPOINTS; n++)
        {
            pEndpointFBSD = &pDevFBSD->aSwEndpoint[n];
            if (pEndpointFBSD->fCancelling)
            {
                pEndpointFBSD->fCancelling = false;
                pUrb = pEndpointFBSD->pUrb;
                pEndpointFBSD->pUrb = NULL;

                if (pUrb != NULL)
                    break;
            }
        }

        if (pUrb != NULL)
        {
            pUrb->enmStatus = VUSBSTATUS_INVALID;
            pUrb->Dev.pvPrivate = NULL;

            switch (pUrb->enmType)
            {
                case VUSBXFERTYPE_MSG:
                    pUrb->cbData = 0;
                    break;
                case VUSBXFERTYPE_ISOC:
                    pUrb->cbData = 0;
                    for (int n = 0; n < (int)pUrb->cIsocPkts; n++)
                        pUrb->aIsocPkts[n].cb = 0;
                    break;
                default:
                    pUrb->cbData = 0;
                    break;
            }
            return pUrb;
        }
        pDevFBSD->fCancelling = false;
    }
    /* Zero default */

    memset(&UsbFsComplete, 0, sizeof(UsbFsComplete));

    /* Check if any endpoints are complete */
    rc = usbProxyFreeBSDDoIoCtl(pProxyDev, USB_FS_COMPLETE, &UsbFsComplete, true);
    if (RT_SUCCESS(rc))
    {
        pXferEndpoint = &pDevFBSD->aHwEndpoint[UsbFsComplete.ep_index];
        pEndpointFBSD = &pDevFBSD->aSwEndpoint[UsbFsComplete.ep_index];

        LogFlow(("usbProxyFreeBSDUrbReap: Reaped "
                 "URB %#p\n", pEndpointFBSD->pUrb));

        if (pXferEndpoint->status == USB_ERR_CANCELLED)
            goto repeat;

        pUrb = pEndpointFBSD->pUrb;
        pEndpointFBSD->pUrb = NULL;
        if (pUrb == NULL)
            goto repeat;

        switch (pXferEndpoint->status)
        {
            case USB_ERR_NORMAL_COMPLETION:
                pUrb->enmStatus = VUSBSTATUS_OK;
                break;
            case USB_ERR_STALLED:
                pUrb->enmStatus = VUSBSTATUS_STALL;
                break;
            default:
                pUrb->enmStatus = VUSBSTATUS_INVALID;
                break;
        }

        pUrb->Dev.pvPrivate = NULL;

        switch (pUrb->enmType)
        {
            case VUSBXFERTYPE_MSG:
                pUrb->cbData = pEndpointFBSD->acbData[0] + pEndpointFBSD->acbData[1];
                break;
            case VUSBXFERTYPE_ISOC:
            {
                int n;

                if (pUrb->enmDir == VUSBDIRECTION_OUT)
                    break;
                pUrb->cbData = 0;
                for (n = 0; n < (int)pUrb->cIsocPkts; n++)
                {
                    if (n >= (int)pEndpointFBSD->cMaxFrames)
                        break;
                    pUrb->cbData += pEndpointFBSD->acbData[n];
                    pUrb->aIsocPkts[n].cb = pEndpointFBSD->acbData[n];
                }
                for (; n < (int)pUrb->cIsocPkts; n++)
                    pUrb->aIsocPkts[n].cb = 0;

                break;
            }
            default:
                pUrb->cbData = pEndpointFBSD->acbData[0];
                break;
        }

        LogFlow(("usbProxyFreeBSDUrbReap: Status=%d epindex=%u "
                 "len[0]=%d len[1]=%d\n",
                 (int)pXferEndpoint->status,
                 (unsigned)UsbFsComplete.ep_index,
                 (unsigned)pEndpointFBSD->acbData[0],
                 (unsigned)pEndpointFBSD->acbData[1]));

    }
    else if (cMillies != 0 && rc == VERR_RESOURCE_BUSY)
    {
        for (;;)
        {
            pfd[0].fd = RTFileToNative(pDevFBSD->hFile);
            pfd[0].events = POLLIN | POLLRDNORM;
            pfd[0].revents = 0;

            pfd[1].fd = RTPipeToNative(pDevFBSD->hPipeWakeupR);
            pfd[1].events = POLLIN | POLLRDNORM;
            pfd[1].revents = 0;

            rc = poll(pfd, 2, (cMillies == RT_INDEFINITE_WAIT) ? INFTIM : cMillies);
            if (rc > 0)
            {
                if (pfd[1].revents & POLLIN)
                {
                    /* Got woken up, drain pipe. */
                    uint8_t bRead;
                    size_t cbIgnored = 0;
                    RTPipeRead(pDevFBSD->hPipeWakeupR, &bRead, 1, &cbIgnored);
                    /* Make sure we return from this function */
                    cMillies = 0;
                }
                break;
            }
            if (rc == 0)
                return NULL;
            if (errno != EAGAIN)
                return NULL;
        }
        goto repeat;
    }
    return pUrb;
}

/**
 * Cancels the URB.
 * The URB requires reaping, so we don't change its state.
 */
static DECLCALLBACK(int) usbProxyFreeBSDUrbCancel(PUSBPROXYDEV pProxyDev, PVUSBURB pUrb)
{
    int index;

    index = (int)(long)pUrb->Dev.pvPrivate - 1;

    if (index < 0 || index >= USBFBSD_MAXENDPOINTS)
        return VINF_SUCCESS; /* invalid index, pretend success. */

    LogFlow(("usbProxyFreeBSDUrbCancel: epindex=%u\n", (unsigned)index));
    return usbProxyFreeBSDEndpointClose(pProxyDev, index);
}

static DECLCALLBACK(int) usbProxyFreeBSDWakeup(PUSBPROXYDEV pProxyDev)
{
    PUSBPROXYDEVFBSD pDevFBSD = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVFBSD);
    size_t cbIgnored;

    LogFlowFunc(("pProxyDev=%p\n", pProxyDev));

    return RTPipeWrite(pDevFBSD->hPipeWakeupW, "", 1, &cbIgnored);
}

/**
 * The FreeBSD USB Proxy Backend.
 */
extern const USBPROXYBACK g_USBProxyDeviceHost =
{
    /* pszName */
    "host",
    /* cbBackend */
    sizeof(USBPROXYDEVFBSD),
    usbProxyFreeBSDOpen,
    usbProxyFreeBSDInit,
    usbProxyFreeBSDClose,
    usbProxyFreeBSDReset,
    usbProxyFreeBSDSetConfig,
    usbProxyFreeBSDClaimInterface,
    usbProxyFreeBSDReleaseInterface,
    usbProxyFreeBSDSetInterface,
    usbProxyFreeBSDClearHaltedEp,
    usbProxyFreeBSDUrbQueue,
    usbProxyFreeBSDUrbCancel,
    usbProxyFreeBSDUrbReap,
    usbProxyFreeBSDWakeup,
    0
};

/*
 * Local Variables:
 *  mode: c
 *  c-file-style: "bsd"
 *  c-basic-offset: 4
 *  tab-width: 4
 *  indent-tabs-mode: s
 * End:
 */

