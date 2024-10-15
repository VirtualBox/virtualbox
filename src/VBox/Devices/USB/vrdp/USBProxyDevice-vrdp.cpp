/* $Id: USBProxyDevice-vrdp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * USB device proxy - the VRDP backend, calls the RemoteUSBBackend methods.
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

#define LOG_GROUP LOG_GROUP_DRV_USBPROXY

#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/vrdpusb.h>
#include <VBox/vmm/pdm.h>

#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/string.h>
#include <iprt/uuid.h>

#include "../USBProxyDevice.h"

/**
 * Backend data for the VRDP USB Proxy device backend.
 */
typedef struct USBPROXYDEVVRDP
{
    REMOTEUSBCALLBACK  *pCallback;
    PREMOTEUSBDEVICE    pDevice;
} USBPROXYDEVVRDP, *PUSBPROXYDEVVRDP;


/*
 * The USB proxy device functions.
 */

static DECLCALLBACK(int) usbProxyVrdpOpen(PUSBPROXYDEV pProxyDev, const char *pszAddress)
{
    LogFlow(("usbProxyVrdpOpen: pProxyDev=%p pszAddress=%s\n", pProxyDev, pszAddress));

    PUSBPROXYDEVVRDP pDevVrdp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVVRDP);
    PPDMUSBINS       pUsbIns  = pProxyDev->pUsbIns;
    PCPDMUSBHLP      pHlp     = pUsbIns->pHlpR3;

    PCFGMNODE pCfgBackend = pHlp->pfnCFGMGetChild(pUsbIns->pCfg, "BackendCfg");
    AssertPtrReturn(pCfgBackend, VERR_NOT_FOUND);

    uint32_t idClient = 0;
    int rc = pHlp->pfnCFGMQueryU32(pCfgBackend, "ClientId", &idClient);
    AssertRCReturn(rc, rc);

    RTUUID UuidDev;
    char *pszUuid = NULL;

    rc = pHlp->pfnCFGMQueryStringAlloc(pUsbIns->pCfg, "UUID", &pszUuid);
    AssertRCReturn(rc, rc);

    rc = RTUuidFromStr(&UuidDev, pszUuid);
    pHlp->pfnMMHeapFree(pUsbIns, pszUuid);
    AssertMsgRCReturn(rc, ("Failed to convert UUID from string! rc=%Rrc\n", rc), rc);

    if (strncmp (pszAddress, REMOTE_USB_BACKEND_PREFIX_S, REMOTE_USB_BACKEND_PREFIX_LEN) == 0)
    {
        RTUUID UuidRemoteUsbIf;
        rc = RTUuidFromStr(&UuidRemoteUsbIf, REMOTEUSBIF_OID); AssertRC(rc);

        PREMOTEUSBIF pRemoteUsbIf = (PREMOTEUSBIF)PDMUsbHlpQueryGenericUserObject(pUsbIns, &UuidRemoteUsbIf);
        AssertPtrReturn(pRemoteUsbIf, VERR_INVALID_PARAMETER);

        REMOTEUSBCALLBACK *pCallback = pRemoteUsbIf->pfnQueryRemoteUsbBackend(pRemoteUsbIf->pvUser, &UuidDev, idClient);
        AssertPtrReturn(pCallback, VERR_INVALID_PARAMETER);

        PREMOTEUSBDEVICE pDevice = NULL;
        rc = pCallback->pfnOpen(pCallback->pInstance, pszAddress, strlen (pszAddress) + 1, &pDevice);
        if (RT_SUCCESS(rc))
        {
            pDevVrdp->pCallback = pCallback;
            pDevVrdp->pDevice = pDevice;
            pProxyDev->iActiveCfg = 1; /** @todo that may not be always true. */
            pProxyDev->cIgnoreSetConfigs = 1;
            return VINF_SUCCESS;
        }
    }
    else
    {
        AssertFailed();
        rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}

static DECLCALLBACK(void) usbProxyVrdpClose(PUSBPROXYDEV pProxyDev)
{
    LogFlow(("usbProxyVrdpClose: pProxyDev = %p\n", pProxyDev));

    PUSBPROXYDEVVRDP pDevVrdp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVVRDP);

    pDevVrdp->pCallback->pfnClose (pDevVrdp->pDevice);
}

static DECLCALLBACK(int) usbProxyVrdpReset(PUSBPROXYDEV pProxyDev, bool fResetOnLinux)
{
    RT_NOREF(fResetOnLinux);
    LogFlow(("usbProxyVrdpReset: pProxyDev = %p\n", pProxyDev));

    PUSBPROXYDEVVRDP pDevVrdp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVVRDP);

    int rc = pDevVrdp->pCallback->pfnReset (pDevVrdp->pDevice);

    if (rc == VERR_VUSB_DEVICE_NOT_ATTACHED)
    {
        Log(("usb-vrdp: remote device %p unplugged!!\n", pDevVrdp->pDevice));
        pProxyDev->fDetached = true;
    }

    pProxyDev->iActiveCfg = -1;
    pProxyDev->cIgnoreSetConfigs = 2;

    return rc;
}

static DECLCALLBACK(int) usbProxyVrdpSetConfig(PUSBPROXYDEV pProxyDev, int cfg)
{
    LogFlow(("usbProxyVrdpSetConfig: pProxyDev=%s cfg=%#x\n", pProxyDev->pUsbIns->pszName, cfg));

    PUSBPROXYDEVVRDP pDevVrdp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVVRDP);

    int rc = pDevVrdp->pCallback->pfnSetConfig (pDevVrdp->pDevice, (uint8_t)cfg);

    if (rc == VERR_VUSB_DEVICE_NOT_ATTACHED)
    {
        Log(("usb-vrdp: remote device %p unplugged!!\n", pDevVrdp->pDevice));
        pProxyDev->fDetached = true;
    }

    return rc;
}

static DECLCALLBACK(int) usbProxyVrdpClaimInterface(PUSBPROXYDEV pProxyDev, int ifnum)
{
    LogFlow(("usbProxyVrdpClaimInterface: pProxyDev=%s ifnum=%#x\n", pProxyDev->pUsbIns->pszName, ifnum));

    PUSBPROXYDEVVRDP pDevVrdp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVVRDP);

    int rc = pDevVrdp->pCallback->pfnClaimInterface (pDevVrdp->pDevice, (uint8_t)ifnum);

    if (rc == VERR_VUSB_DEVICE_NOT_ATTACHED)
    {
        Log(("usb-vrdp: remote device %p unplugged!!\n", pDevVrdp->pDevice));
        pProxyDev->fDetached = true;
    }

    return rc;
}

static DECLCALLBACK(int) usbProxyVrdpReleaseInterface(PUSBPROXYDEV pProxyDev, int ifnum)
{
    LogFlow(("usbProxyVrdpReleaseInterface: pProxyDev=%s ifnum=%#x\n", pProxyDev->pUsbIns->pszName, ifnum));

    PUSBPROXYDEVVRDP pDevVrdp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVVRDP);

    int rc = pDevVrdp->pCallback->pfnReleaseInterface (pDevVrdp->pDevice, (uint8_t)ifnum);

    if (rc == VERR_VUSB_DEVICE_NOT_ATTACHED)
    {
        Log(("usb-vrdp: remote device %p unplugged!!\n", pDevVrdp->pDevice));
        pProxyDev->fDetached = true;
    }

    return rc;
}

static DECLCALLBACK(int) usbProxyVrdpSetInterface(PUSBPROXYDEV pProxyDev, int ifnum, int setting)
{
    LogFlow(("usbProxyVrdpSetInterface: pProxyDev=%p ifnum=%#x setting=%#x\n", pProxyDev, ifnum, setting));

    PUSBPROXYDEVVRDP pDevVrdp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVVRDP);

    int rc = pDevVrdp->pCallback->pfnInterfaceSetting (pDevVrdp->pDevice, (uint8_t)ifnum, (uint8_t)setting);

    if (rc == VERR_VUSB_DEVICE_NOT_ATTACHED)
    {
        Log(("usb-vrdp: remote device %p unplugged!!\n", pDevVrdp->pDevice));
        pProxyDev->fDetached = true;
    }

    return rc;
}

static DECLCALLBACK(int) usbProxyVrdpClearHaltedEp(PUSBPROXYDEV pProxyDev, unsigned int ep)
{
    LogFlow(("usbProxyVrdpClearHaltedEp: pProxyDev=%s ep=%u\n", pProxyDev->pUsbIns->pszName, ep));

    PUSBPROXYDEVVRDP pDevVrdp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVVRDP);

    int rc = pDevVrdp->pCallback->pfnClearHaltedEP (pDevVrdp->pDevice, (uint8_t)ep);

    if (rc == VERR_VUSB_DEVICE_NOT_ATTACHED)
    {
        Log(("usb-vrdp: remote device %p unplugged!!\n", pDevVrdp->pDevice));
        pProxyDev->fDetached = true;
    }

    return rc;
}

static DECLCALLBACK(int) usbProxyVrdpUrbQueue(PUSBPROXYDEV pProxyDev, PVUSBURB pUrb)
{
    LogFlow(("usbProxyVrdpUrbQueue: pUrb=%p\n", pUrb));

    /** @todo implement isochronous transfers for USB over VRDP. */
    if (pUrb->enmType == VUSBXFERTYPE_ISOC)
    {
        Log(("usbproxy: isochronous transfers aren't implemented yet.\n"));
        return VERR_NOT_IMPLEMENTED;
    }

    PUSBPROXYDEVVRDP pDevVrdp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVVRDP);
    int rc = pDevVrdp->pCallback->pfnQueueURB (pDevVrdp->pDevice, pUrb->enmType, pUrb->EndPt, pUrb->enmDir, pUrb->cbData,
                                               pUrb->abData, pUrb, (PREMOTEUSBQURB *)&pUrb->Dev.pvPrivate);

    if (rc == VERR_VUSB_DEVICE_NOT_ATTACHED)
    {
        Log(("usb-vrdp: remote device %p unplugged!!\n", pDevVrdp->pDevice));
        pProxyDev->fDetached = true;
    }

    return rc;
}

static DECLCALLBACK(PVUSBURB) usbProxyVrdpUrbReap(PUSBPROXYDEV pProxyDev, RTMSINTERVAL cMillies)
{
    LogFlow(("usbProxyVrdpUrbReap: pProxyDev=%s\n", pProxyDev->pUsbIns->pszName));

    PUSBPROXYDEVVRDP pDevVrdp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVVRDP);

    PVUSBURB pUrb = NULL;
    uint32_t cbData = 0;
    uint32_t u32Err = VUSBSTATUS_OK;

    int rc = pDevVrdp->pCallback->pfnReapURB (pDevVrdp->pDevice, cMillies, (void **)&pUrb, &cbData, &u32Err);

    LogFlow(("usbProxyVrdpUrbReap: rc = %Rrc, pUrb = %p\n", rc, pUrb));

    if (RT_SUCCESS(rc) && pUrb)
    {
        pUrb->enmStatus = (VUSBSTATUS)u32Err;
        pUrb->cbData = cbData;
        pUrb->Dev.pvPrivate = NULL;
    }

    if (rc == VERR_VUSB_DEVICE_NOT_ATTACHED)
    {
        Log(("usb-vrdp: remote device %p unplugged!!\n", pDevVrdp->pDevice));
        pProxyDev->fDetached = true;
    }

    return pUrb;
}

static DECLCALLBACK(int) usbProxyVrdpUrbCancel(PUSBPROXYDEV pProxyDev, PVUSBURB pUrb)
{
    LogFlow(("usbProxyVrdpUrbCancel: pUrb=%p\n", pUrb));

    PUSBPROXYDEVVRDP pDevVrdp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVVRDP);
    pDevVrdp->pCallback->pfnCancelURB (pDevVrdp->pDevice, (PREMOTEUSBQURB)pUrb->Dev.pvPrivate);
    return VINF_SUCCESS; /** @todo Enhance remote interface to pass a status code. */
}

static DECLCALLBACK(int) usbProxyVrdpWakeup(PUSBPROXYDEV pProxyDev)
{
    LogFlow(("usbProxyVrdpWakeup: pProxyDev=%s\n", pProxyDev->pUsbIns->pszName));

    PUSBPROXYDEVVRDP pDevVrdp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVVRDP);
    return pDevVrdp->pCallback->pfnWakeup (pDevVrdp->pDevice);
}

/**
 * The VRDP USB Proxy Backend operations.
 */
extern const USBPROXYBACK g_USBProxyDeviceVRDP =
{
    /* pszName */
    "vrdp",
    /* cbBackend */
    sizeof(USBPROXYDEVVRDP),
    usbProxyVrdpOpen,
    NULL,
    usbProxyVrdpClose,
    usbProxyVrdpReset,
    usbProxyVrdpSetConfig,
    usbProxyVrdpClaimInterface,
    usbProxyVrdpReleaseInterface,
    usbProxyVrdpSetInterface,
    usbProxyVrdpClearHaltedEp,
    usbProxyVrdpUrbQueue,
    usbProxyVrdpUrbCancel,
    usbProxyVrdpUrbReap,
    usbProxyVrdpWakeup,
    0
};

