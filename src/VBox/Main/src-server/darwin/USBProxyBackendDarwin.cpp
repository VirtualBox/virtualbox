/* $Id: USBProxyBackendDarwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox USB Proxy Service (in VBoxSVC), Darwin Specialization.
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
#include "USBProxyBackend.h"
#include "LoggingNew.h"
#include "iokit.h"

#include <VBox/usb.h>
#include <VBox/usblib.h>
#include <iprt/errcore.h>

#include <iprt/string.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/errcore.h>
#include <iprt/asm.h>


/**
 * Initialize data members.
 */
USBProxyBackendDarwin::USBProxyBackendDarwin()
    : USBProxyBackend(), mServiceRunLoopRef(NULL), mNotifyOpaque(NULL), mWaitABitNextTime(false)
{
}

USBProxyBackendDarwin::~USBProxyBackendDarwin()
{
}

/**
 * Initializes the object (called right after construction).
 *
 * @returns VBox status code.
 */
int USBProxyBackendDarwin::init(USBProxyService *pUsbProxyService, const com::Utf8Str &strId,
                                const com::Utf8Str &strAddress, bool fLoadingSettings)
{
    USBProxyBackend::init(pUsbProxyService, strId, strAddress, fLoadingSettings);

    unconst(m_strBackend) = Utf8Str("host");

    /*
     * Start the poller thread.
     */
    start();
    return VINF_SUCCESS;
}


/**
 * Stop all service threads and free the device chain.
 */
void USBProxyBackendDarwin::uninit()
{
    LogFlowThisFunc(("\n"));

    /*
     * Stop the service.
     */
    if (isActive())
        stop();

    USBProxyBackend::uninit();
}


int USBProxyBackendDarwin::captureDevice(HostUSBDevice *aDevice)
{
    /*
     * Check preconditions.
     */
    AssertReturn(aDevice, VERR_GENERAL_FAILURE);
    AssertReturn(!aDevice->isWriteLockOnCurrentThread(), VERR_GENERAL_FAILURE);

    AutoReadLock devLock(aDevice COMMA_LOCKVAL_SRC_POS);
    LogFlowThisFunc(("aDevice=%s\n", aDevice->i_getName().c_str()));

    Assert(aDevice->i_getUnistate() == kHostUSBDeviceState_Capturing);

    devLock.release();
    interruptWait();
    return VINF_SUCCESS;
}


int USBProxyBackendDarwin::releaseDevice(HostUSBDevice *aDevice)
{
    /*
     * Check preconditions.
     */
    AssertReturn(aDevice, VERR_GENERAL_FAILURE);
    AssertReturn(!aDevice->isWriteLockOnCurrentThread(), VERR_GENERAL_FAILURE);

    AutoReadLock devLock(aDevice COMMA_LOCKVAL_SRC_POS);
    LogFlowThisFunc(("aDevice=%s\n", aDevice->i_getName().c_str()));

    Assert(aDevice->i_getUnistate() == kHostUSBDeviceState_ReleasingToHost);

    devLock.release();
    interruptWait();
    return VINF_SUCCESS;
}


bool USBProxyBackendDarwin::isFakeUpdateRequired()
{
    return true;
}


int USBProxyBackendDarwin::wait(RTMSINTERVAL aMillies)
{
    SInt32 rc = CFRunLoopRunInMode(CFSTR(VBOX_IOKIT_MODE_STRING),
                                   mWaitABitNextTime && aMillies >= 1000
                                   ? 1.0 /* seconds */
                                   : aMillies >= 5000 /* Temporary measure to poll for status changes (MSD). */
                                   ? 5.0 /* seconds */
                                   : aMillies / 1000.0,
                                   true);
    mWaitABitNextTime = rc != kCFRunLoopRunTimedOut;

    return VINF_SUCCESS;
}


int USBProxyBackendDarwin::interruptWait(void)
{
    if (mServiceRunLoopRef)
        CFRunLoopStop(mServiceRunLoopRef);
    return 0;
}


PUSBDEVICE USBProxyBackendDarwin::getDevices(void)
{
    /* call iokit.cpp */
    return DarwinGetUSBDevices();
}


void USBProxyBackendDarwin::serviceThreadInit(void)
{
    mServiceRunLoopRef = CFRunLoopGetCurrent();
    mNotifyOpaque = DarwinSubscribeUSBNotifications();
}


void USBProxyBackendDarwin::serviceThreadTerm(void)
{
    DarwinUnsubscribeUSBNotifications(mNotifyOpaque);
    mServiceRunLoopRef = NULL;
}


/**
 * Wrapper called from iokit.cpp.
 *
 * @param   pCur    The USB device to free.
 */
void DarwinFreeUSBDeviceFromIOKit(PUSBDEVICE pCur)
{
    USBProxyBackend::freeDevice(pCur);
}

