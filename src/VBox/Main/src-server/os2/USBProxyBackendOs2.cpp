/* $Id: USBProxyBackendOs2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox USB Proxy Service, OS/2 Specialization.
 */

/*
 * Copyright (C) 2005-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_MAIN_USBPROXYBACKEND
#define INCL_BASE
#define INCL_ERRORS
#include "USBProxyBackend.h"
#include "LoggingNew.h"

#include <VBox/usb.h>
#include <iprt/errcore.h>

#include <iprt/string.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/errcore.h>


/**
 * Initialize data members.
 */
USBProxyBackendOs2::USBProxyBackendOs2(USBProxyService *aUsbProxyService, const com::Utf8Str &strId)
    : USBProxyBackend(aUsbProxyService, strId), mhev(NULLHANDLE), mhmod(NULLHANDLE),
    mpfnUsbRegisterChangeNotification(NULL), mpfnUsbDeregisterNotification(NULL),
    mpfnUsbQueryNumberDevices(NULL), mpfnUsbQueryDeviceReport(NULL)
{
    LogFlowThisFunc(("aUsbProxyService=%p\n", aUsbProxyService));

    /*
     * Try initialize the usbcalls stuff.
     */
    APIRET orc = DosCreateEventSem(NULL, &mhev, 0, FALSE);
    int vrc = RTErrConvertFromOS2(orc);
    if (RT_SUCCESS(vrc))
    {
        orc = DosLoadModule(NULL, 0, (PCSZ)"usbcalls", &mhmod);
        vrc = RTErrConvertFromOS2(orc);
        if (RT_SUCCESS(vrc))
        {
            if (    (orc = DosQueryProcAddr(mhmod, 0, (PCSZ)"UsbQueryNumberDevices",         (PPFN)&mpfnUsbQueryNumberDevices))          == NO_ERROR
                &&  (orc = DosQueryProcAddr(mhmod, 0, (PCSZ)"UsbQueryDeviceReport",          (PPFN)&mpfnUsbQueryDeviceReport))           == NO_ERROR
                &&  (orc = DosQueryProcAddr(mhmod, 0, (PCSZ)"UsbRegisterChangeNotification", (PPFN)&mpfnUsbRegisterChangeNotification))  == NO_ERROR
                &&  (orc = DosQueryProcAddr(mhmod, 0, (PCSZ)"UsbDeregisterNotification",     (PPFN)&mpfnUsbDeregisterNotification))      == NO_ERROR
               )
            {
                orc = mpfnUsbRegisterChangeNotification(&mNotifyId, mhev, mhev);
                if (!orc)
                {
                    /*
                     * Start the poller thread.
                     */
                    vrc = start();
                    if (RT_SUCCESS(vrc))
                    {
                        LogFlowThisFunc(("returns successfully - mNotifyId=%d\n", mNotifyId));
                        mLastError = VINF_SUCCESS;
                        return;
                    }
                    LogRel(("USBProxyBackendOs2: failed to start poller thread, vrc=%Rrc\n", vrc));
                }
                else
                {
                    LogRel(("USBProxyBackendOs2: failed to register change notification, orc=%d\n", orc));
                    vrc = RTErrConvertFromOS2(orc);
                }
            }
            else
            {
                LogRel(("USBProxyBackendOs2: failed to load usbcalls\n"));
                vrc = RTErrConvertFromOS2(orc);
            }

            DosFreeModule(mhmod);
        }
        else
            LogRel(("USBProxyBackendOs2: failed to load usbcalls, vrc=%d\n", vrc));
        mhmod = NULLHANDLE;
    }
    else
        mhev = NULLHANDLE;

    mLastError = vrc;
    LogFlowThisFunc(("returns failure!!! (vrc=%Rrc)\n", vrc));
}


/**
 * Stop all service threads and free the device chain.
 */
USBProxyBackendOs2::~USBProxyBackendOs2()
{
    LogFlowThisFunc(("\n"));

    /*
     * Stop the service.
     */
    if (isActive())
        stop();

    /*
     * Free resources.
     */
    if (mhmod)
    {
        if (mpfnUsbDeregisterNotification)
            mpfnUsbDeregisterNotification(mNotifyId);

        mpfnUsbRegisterChangeNotification = NULL;
        mpfnUsbDeregisterNotification = NULL;
        mpfnUsbQueryNumberDevices = NULL;
        mpfnUsbQueryDeviceReport = NULL;

        DosFreeModule(mhmod);
        mhmod = NULLHANDLE;
    }
}


int USBProxyBackendOs2::captureDevice(HostUSBDevice *aDevice)
{
    AssertReturn(aDevice, VERR_GENERAL_FAILURE);
    AssertReturn(!aDevice->isWriteLockOnCurrentThread(), VERR_GENERAL_FAILURE);

    AutoReadLock devLock(aDevice COMMA_LOCKVAL_SRC_POS);
    LogFlowThisFunc(("aDevice=%s\n", aDevice->getName().c_str()));

    /*
     * Don't think we need to do anything when the device is held... fake it.
     */
    Assert(aDevice->isStatePending());
    devLock.release();
    interruptWait();

    return VINF_SUCCESS;
}


int USBProxyBackendOs2::releaseDevice(HostUSBDevice *aDevice)
{
    AssertReturn(aDevice, VERR_GENERAL_FAILURE);
    AssertReturn(!aDevice->isWriteLockOnCurrentThread(), VERR_GENERAL_FAILURE);

    AutoReadLock devLock(aDevice COMMA_LOCKVAL_SRC_POS);
    LogFlowThisFunc(("aDevice=%s\n", aDevice->getName().c_str()));

    /*
     * We're not really holding it atm., just fake it.
     */
    Assert(aDevice->isStatePending());
    devLock.release();
    interruptWait();

    return VINF_SUCCESS;
}


#if 0
bool USBProxyBackendOs2::updateDeviceState(HostUSBDevice *aDevice, PUSBDEVICE aUSBDevice, bool *aRunFilters,
                                           SessionMachine **aIgnoreMachine)
{
    AssertReturn(aDevice, false);
    AssertReturn(!aDevice->isWriteLockOnCurrentThread(), false);
    return updateDeviceStateFake(aDevice, aUSBDevice, aRunFilters, aIgnoreMachine);
}
#endif



int USBProxyBackendOs2::wait(RTMSINTERVAL aMillies)
{
    int orc = DosWaitEventSem(mhev, aMillies);
    return RTErrConvertFromOS2(orc);
}


int USBProxyBackendOs2::interruptWait(void)
{
    int orc = DosPostEventSem(mhev);
    return orc == NO_ERROR || orc == ERROR_ALREADY_POSTED
         ? VINF_SUCCESS
         : RTErrConvertFromOS2(orc);
}

#include <stdio.h>

PUSBDEVICE USBProxyBackendOs2::getDevices(void)
{
    /*
     * Count the devices.
     */
    ULONG cDevices = 0;
    int orc = mpfnUsbQueryNumberDevices((PULONG)&cDevices); /* Thanks to com/xpcom, PULONG and ULONG * aren't the same. */
    if (orc)
        return NULL;

    /*
     * Retrieve information about each device.
     */
    PUSBDEVICE pFirst = NULL;
    PUSBDEVICE *ppNext = &pFirst;
    for (ULONG i = 0; i < cDevices; i++)
    {
        /*
         * Query the device and config descriptors.
         */
        uint8_t abBuf[1024];
        ULONG cb = sizeof(abBuf);
        orc = mpfnUsbQueryDeviceReport(i + 1, (PULONG)&cb, &abBuf[0]); /* see above (PULONG) */
        if (orc)
            continue;
        PUSBDEVICEDESC pDevDesc = (PUSBDEVICEDESC)&abBuf[0];
        if (    cb < sizeof(*pDevDesc)
            ||  pDevDesc->bDescriptorType != USB_DT_DEVICE
            ||  pDevDesc->bLength < sizeof(*pDevDesc)
            ||  pDevDesc->bLength > sizeof(*pDevDesc) * 2)
            continue;
        PUSBCONFIGDESC pCfgDesc = (PUSBCONFIGDESC)&abBuf[pDevDesc->bLength];
        if (    pCfgDesc->bDescriptorType != USB_DT_CONFIG
            ||  pCfgDesc->bLength >= sizeof(*pCfgDesc))
            pCfgDesc = NULL;

        /*
         * Skip it if it's some kind of hub.
         */
        if (pDevDesc->bDeviceClass == USB_HUB_CLASSCODE)
            continue;

        /*
         * Allocate a new device node and initialize it with the basic stuff.
         */
        PUSBDEVICE pCur = (PUSBDEVICE)RTMemAlloc(sizeof(*pCur));
        pCur->bcdUSB = pDevDesc->bcdUSB;
        pCur->bDeviceClass = pDevDesc->bDeviceClass;
        pCur->bDeviceSubClass = pDevDesc->bDeviceSubClass;
        pCur->bDeviceProtocol = pDevDesc->bDeviceProtocol;
        pCur->idVendor = pDevDesc->idVendor;
        pCur->idProduct = pDevDesc->idProduct;
        pCur->bcdDevice = pDevDesc->bcdDevice;
        pCur->pszManufacturer = RTStrDup("");
        pCur->pszProduct = RTStrDup("");
        pCur->pszSerialNumber = NULL;
        pCur->u64SerialHash = 0;
        //pCur->bNumConfigurations = pDevDesc->bNumConfigurations;
        pCur->bNumConfigurations = 0;
        pCur->paConfigurations = NULL;
        pCur->enmState = USBDEVICESTATE_USED_BY_HOST_CAPTURABLE;
        pCur->enmSpeed = USBDEVICESPEED_UNKNOWN;
        pCur->pszAddress = NULL;
        RTStrAPrintf((char **)&pCur->pszAddress, "p=0x%04RX16;v=0x%04RX16;r=0x%04RX16;e=0x%08RX32",
                     pDevDesc->idProduct, pDevDesc->idVendor, pDevDesc->bcdDevice, i);

        pCur->bBus = 0;
        pCur->bLevel = 0;
        pCur->bDevNum = 0;
        pCur->bDevNumParent = 0;
        pCur->bPort = 0;
        pCur->bNumDevices = 0;
        pCur->bMaxChildren = 0;

        /* link it */
        pCur->pNext = NULL;
        pCur->pPrev = *ppNext;
        *ppNext = pCur;
        ppNext = &pCur->pNext;
    }

    return pFirst;
}

