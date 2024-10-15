/* $Id: USBProxyDevice.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * USBPROXY - USB proxy header
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

#ifndef VBOX_INCLUDED_SRC_USB_USBProxyDevice_h
#define VBOX_INCLUDED_SRC_USB_USBProxyDevice_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/vusb.h>

RT_C_DECLS_BEGIN


/** Pointer to a USB proxy device. */
typedef struct USBPROXYDEV *PUSBPROXYDEV;

/**
 * USB Proxy Device Backend
 */
typedef struct USBPROXYBACK
{
    /** Name of the backend. */
    const char *pszName;
    /** Size of the backend specific data. */
    size_t      cbBackend;

    /**
     * Opens the USB device specfied by pszAddress.
     *
     * This method will initialize backend private data. If the backend has
     * already selected a configuration for the device, this must be indicated
     * in USBPROXYDEV::iActiveCfg.
     *
     * @returns VBox status code.
     * @param   pProxyDev   The USB Proxy Device instance.
     * @param   pszAddress  Host specific USB device address.
     */
    DECLR3CALLBACKMEMBER(int, pfnOpen, (PUSBPROXYDEV pProxyDev, const char *pszAddress));

    /**
     * Optional callback for initializing the device after the configuration
     * has been established.
     *
     * @returns VBox status code.
     * @param   pProxyDev       The USB Proxy Device instance.
     */
    DECLR3CALLBACKMEMBER(int, pfnInit, (PUSBPROXYDEV pProxyDev));

    /**
     * Closes handle to the host USB device.
     *
     * @param   pProxyDev       The USB Proxy Device instance.
     */
    DECLR3CALLBACKMEMBER(void, pfnClose, (PUSBPROXYDEV pProxyDev));

    /**
     * Reset a device.
     *
     * The backend must update iActualCfg and fIgnoreEqualSetConfig.
     *
     * @returns VBox status code.
     * @param   pProxyDev       The USB Proxy Device instance.
     * @param   fResetOnLinux   It's safe to do reset on linux, we can deal with devices
     *                          being logically reconnected.
     */
    DECLR3CALLBACKMEMBER(int, pfnReset, (PUSBPROXYDEV pProxyDev, bool fResetOnLinux));

    /**
     * Sets the given configuration of the device.
     *
     * @returns VBox status code.
     * @param   pProxyDev       The USB Proxy Device instance.
     * @param   iCfg            The configuration to set.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetConfig, (PUSBPROXYDEV pProxyDev, int iCfg));

    /**
     * Claim an interface for use by the prox device.
     *
     * @returns VBox status code.
     * @param   pProxyDev       The USB Proxy Device instance.
     * @param   iIf             Interface number to claim.
     */
    DECLR3CALLBACKMEMBER(int, pfnClaimInterface, (PUSBPROXYDEV pProxyDev, int iIf));

    /**
     * Releases an interface which was claimed before.
     *
     * @returns VBox status code.
     * @param   pProxyDev       The USB Proxy Device instance.
     * @param   iIf             Interface number to release.
     */
    DECLR3CALLBACKMEMBER(int, pfnReleaseInterface, (PUSBPROXYDEV pProxyDev, int iIf));

    /**
     * Sets the given alternate interface for the device.
     *
     * @returns VBox status code.
     * @param   pProxyDev       The USB Proxy Device instance.
     * @param   iIf             Interface to use.
     * @param   iSetting        The alternate setting to use.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetInterface, (PUSBPROXYDEV pProxyDev, int iIf, int iSetting));

    /**
     * Clears the given halted endpoint.
     *
     * @returns VBox status code.
     * @param   pProxyDev       The USB Proxy Device instance.
     * @param   iEp             The endpoint to clear.
     */
    DECLR3CALLBACKMEMBER(int, pfnClearHaltedEndpoint, (PUSBPROXYDEV  pDev, unsigned int iEp));

    /**
     * Queue a new URB.
     *
     * @returns VBox status code.
     * @param   pProxyDev       The USB Proxy Device instance.
     * @param   pUrb            The URB to queue.
     */
    DECLR3CALLBACKMEMBER(int, pfnUrbQueue, (PUSBPROXYDEV pProxyDev, PVUSBURB pUrb));

    /**
     * Cancel an in-flight URB.
     *
     * @returns VBox status code.
     * @param   pProxyDev       The USB Proxy Device instance.
     * @param   pUrb            The URB to cancel.
     */
    DECLR3CALLBACKMEMBER(int, pfnUrbCancel, (PUSBPROXYDEV pProxyDev, PVUSBURB pUrb));

    /**
     * Reap URBs in-flight on a device.
     *
     * @returns Pointer to a completed URB.
     * @returns NULL if no URB was completed.
     * @param   pProxyDev       The USB Proxy Device instance.
     * @param   cMillies        Number of milliseconds to wait. Use 0 to not
     *                          wait at all.
     */
    DECLR3CALLBACKMEMBER(PVUSBURB, pfnUrbReap, (PUSBPROXYDEV pProxyDev, RTMSINTERVAL cMillies));

    /**
     * Kicks the thread waiting in pfnUrbReap to make it return.
     *
     * @returns VBox status code.
     * @param   pProxyDev       The USB Proxy Device instance.
     */
    DECLR3CALLBACKMEMBER(int, pfnWakeup, (PUSBPROXYDEV pProxyDev));

    /** Dummy entry for making sure we've got all members initialized. */
    uint32_t uDummy;
} USBPROXYBACK;
/** Pointer to a USB Proxy Device Backend. */
typedef USBPROXYBACK *PUSBPROXYBACK;
/** Pointer to a const USB Proxy Device Backend. */
typedef const USBPROXYBACK *PCUSBPROXYBACK;

/** The Host backend. */
extern const USBPROXYBACK g_USBProxyDeviceHost;
/** The remote desktop backend. */
extern const USBPROXYBACK g_USBProxyDeviceVRDP;
/** The USB/IP backend. */
extern const USBPROXYBACK g_USBProxyDeviceUsbIp;

#ifdef RDESKTOP
typedef struct VUSBDEV
{
    char* pszName;
} VUSBDEV, *PVUSBDEV;
#endif

/**
 * USB Proxy device.
 */
typedef struct USBPROXYDEV
{
    /** The device descriptor. */
    VUSBDESCDEVICE      DevDesc;
    /** The configuration descriptor array. */
    PVUSBDESCCONFIGEX   paCfgDescs;
#ifndef RDESKTOP
    /** The descriptor cache.
     * Contains &DevDesc and paConfigDescs. */
    PDMUSBDESCCACHE     DescCache;
    /** Pointer to the PDM USB device instance. */
    PPDMUSBINS          pUsbIns;
#endif

    /** Pointer to the backend. */
    PCUSBPROXYBACK      pOps;
    /** The currently active configuration.
     * It's -1 if no configuration is active. This is set to -1 before open and reset,
     * the backend will change it if open or reset implies SET_CONFIGURATION. */
    int                 iActiveCfg;
    /** Ignore one or two SET_CONFIGURATION operation.
     * See usbProxyDevSetCfg for details. */
    int                 cIgnoreSetConfigs;
    /** Mask of the interfaces that the guest shall not see. */
    uint32_t            fMaskedIfs;
    /** Whether we've opened the device or not.
     * For dealing with failed construction (the destruct method is always called). */
    bool                fOpened;
    /** Whether we've called pfnInit or not.
     * For dealing with failed construction (the destruct method is always called). */
    bool                fInited;
    /** Whether the device has been detached.
     * This is hack for making PDMUSBREG::pfnUsbQueue return the right status code. */
    bool                fDetached;
    /** Backend specific data, the size is stored in pOps::cbBackend. */
    void               *pvInstanceDataR3;

#ifdef RDESKTOP
    /** @name VRDP client (rdesktop) related members.
     * @{ */
    /** The vrdp device ID. */
    uint32_t            idVrdp;
    /** The VUSB device structure - must be the first structure member. */
    VUSBDEV             Dev;
    /** The next device in rdesktop-vrdp's linked list. */
    PUSBPROXYDEV        pNext;
    /** The previous device in rdesktop-vrdp's linked list. */
    PUSBPROXYDEV        pPrev;
    /** Linked list of in-flight URBs */
    PVUSBURB            pUrbs;
    /** @} */
#endif
} USBPROXYDEV;

/** @def USBPROXYDEV_2_DATA
 * Converts a USB proxy Device, pointer to a pointer to the backend specific instance data.
 */
#define USBPROXYDEV_2_DATA(a_pProxyDev, a_Type)   ( (a_Type)(a_pProxyDev)->pvInstanceDataR3 )


DECLINLINE(const char *) usbProxyGetName(PUSBPROXYDEV pProxyDev)
{
#ifndef RDESKTOP
    return pProxyDev->pUsbIns->pszName;
#else
    return pProxyDev->Dev.pszName;
#endif
}

#ifdef RDESKTOP
DECLINLINE(PUSBPROXYDEV) usbProxyFromVusbDev(PVUSBDEV pDev)
{
    return RT_FROM_MEMBER(pDev, USBPROXYDEV, Dev);
}
#endif

#ifdef RT_OS_LINUX
RTDECL(int) USBProxyDeviceLinuxGetFD(PUSBPROXYDEV pProxyDev);
#endif

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_USB_USBProxyDevice_h */

