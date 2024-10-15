/* $Id: USBProxyBackendWindows.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox USB Proxy Service, Windows Specialization.
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

#include <VBox/usb.h>
#include <iprt/errcore.h>

#include <iprt/string.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/errcore.h>

#include <VBox/usblib.h>


/**
 * Initialize data members.
 */
USBProxyBackendWindows::USBProxyBackendWindows()
    : USBProxyBackend(), mhEventInterrupt(INVALID_HANDLE_VALUE)
{
    LogFlowThisFunc(("\n"));
}

USBProxyBackendWindows::~USBProxyBackendWindows()
{
}

/**
 * Initializes the object (called right after construction).
 *
 * @returns S_OK on success and non-fatal failures, some COM error otherwise.
 */
int USBProxyBackendWindows::init(USBProxyService *aUsbProxyService, const com::Utf8Str &strId,
                                 const com::Utf8Str &strAddress, bool fLoadingSettings)
{
    USBProxyBackend::init(aUsbProxyService, strId, strAddress, fLoadingSettings);

    unconst(m_strBackend) = Utf8Str("host");

    /*
     * Create the semaphore (considered fatal).
     */
    mhEventInterrupt = CreateEvent(NULL, FALSE, FALSE, NULL);
    AssertReturn(mhEventInterrupt != INVALID_HANDLE_VALUE, VERR_OUT_OF_RESOURCES);

    /*
     * Initialize the USB lib and stuff.
     */
    int vrc = USBLibInit();
    if (RT_SUCCESS(vrc))
    {
        /*
         * Start the poller thread.
         */
        vrc = start();
        if (RT_SUCCESS(vrc))
        {
            LogFlowThisFunc(("returns successfully\n"));
            return VINF_SUCCESS;
        }

        USBLibTerm();
    }

    CloseHandle(mhEventInterrupt);
    mhEventInterrupt = INVALID_HANDLE_VALUE;

    LogFlowThisFunc(("returns failure!!! (vrc=%Rrc)\n", vrc));
    return vrc;
}


/**
 * Stop all service threads and free the device chain.
 */
void USBProxyBackendWindows::uninit()
{
    LogFlowThisFunc(("\n"));

    /*
     * Stop the service.
     */
    if (isActive())
        stop();

    if (mhEventInterrupt != INVALID_HANDLE_VALUE)
        CloseHandle(mhEventInterrupt);
    mhEventInterrupt = INVALID_HANDLE_VALUE;

    /*
     * Terminate the library...
     */
    int vrc = USBLibTerm();
    AssertRC(vrc);
    USBProxyBackend::uninit();
}


void *USBProxyBackendWindows::insertFilter(PCUSBFILTER aFilter)
{
    AssertReturn(aFilter, NULL);

    LogFlow(("USBProxyBackendWindows::insertFilter()\n"));

    void *pvId = USBLibAddFilter(aFilter);

    LogFlow(("USBProxyBackendWindows::insertFilter(): returning pvId=%p\n", pvId));

    return pvId;
}


void USBProxyBackendWindows::removeFilter(void *aID)
{
    LogFlow(("USBProxyBackendWindows::removeFilter(): id=%p\n", aID));

    AssertReturnVoid(aID);

    USBLibRemoveFilter(aID);
}


int USBProxyBackendWindows::captureDevice(HostUSBDevice *aDevice)
{
    /*
     * Check preconditions.
     */
    AssertReturn(aDevice, VERR_GENERAL_FAILURE);
    AssertReturn(!aDevice->isWriteLockOnCurrentThread(), VERR_GENERAL_FAILURE);

    AutoReadLock devLock(aDevice COMMA_LOCKVAL_SRC_POS);
    LogFlowThisFunc(("aDevice=%s\n", aDevice->i_getName().c_str()));

    Assert(aDevice->i_getUnistate() == kHostUSBDeviceState_Capturing);

    /*
     * Create a one-shot ignore filter for the device
     * and trigger a re-enumeration of it.
     */
    USBFILTER Filter;
    USBFilterInit(&Filter, USBFILTERTYPE_ONESHOT_CAPTURE);
    initFilterFromDevice(&Filter, aDevice);
    Log(("USBFILTERIDX_PORT=%#x\n", USBFilterGetNum(&Filter, USBFILTERIDX_PORT)));
    Log(("USBFILTERIDX_BUS=%#x\n", USBFilterGetNum(&Filter, USBFILTERIDX_BUS)));

    void *pvId = USBLibAddFilter(&Filter);
    if (!pvId)
    {
        AssertMsgFailed(("Add one-shot Filter failed\n"));
        return VERR_GENERAL_FAILURE;
    }

    int vrc = USBLibRunFilters();
    if (!RT_SUCCESS(vrc))
    {
        AssertMsgFailed(("Run Filters failed\n"));
        USBLibRemoveFilter(pvId);
        return vrc;
    }

    return VINF_SUCCESS;
}


int USBProxyBackendWindows::releaseDevice(HostUSBDevice *aDevice)
{
    /*
     * Check preconditions.
     */
    AssertReturn(aDevice, VERR_GENERAL_FAILURE);
    AssertReturn(!aDevice->isWriteLockOnCurrentThread(), VERR_GENERAL_FAILURE);

    AutoReadLock devLock(aDevice COMMA_LOCKVAL_SRC_POS);
    LogFlowThisFunc(("aDevice=%s\n", aDevice->i_getName().c_str()));

    Assert(aDevice->i_getUnistate() == kHostUSBDeviceState_ReleasingToHost);

    /*
     * Create a one-shot ignore filter for the device
     * and trigger a re-enumeration of it.
     */
    USBFILTER Filter;
    USBFilterInit(&Filter, USBFILTERTYPE_ONESHOT_IGNORE);
    initFilterFromDevice(&Filter, aDevice);
    Log(("USBFILTERIDX_PORT=%#x\n", USBFilterGetNum(&Filter, USBFILTERIDX_PORT)));
    Log(("USBFILTERIDX_BUS=%#x\n", USBFilterGetNum(&Filter, USBFILTERIDX_BUS)));

    void *pvId = USBLibAddFilter(&Filter);
    if (!pvId)
    {
        AssertMsgFailed(("Add one-shot Filter failed\n"));
        return VERR_GENERAL_FAILURE;
    }

    int vrc = USBLibRunFilters();
    if (!RT_SUCCESS(vrc))
    {
        AssertMsgFailed(("Run Filters failed\n"));
        USBLibRemoveFilter(pvId);
        return vrc;
    }


    return VINF_SUCCESS;
}


/**
 * Returns whether devices reported by this backend go through a de/re-attach
 * and device re-enumeration cycle when they are captured or released.
 */
bool USBProxyBackendWindows::i_isDevReEnumerationRequired()
{
    return true;
}


int USBProxyBackendWindows::wait(unsigned aMillies)
{
    return USBLibWaitChange(aMillies);
}


int USBProxyBackendWindows::interruptWait(void)
{
    return USBLibInterruptWaitChange();
}

/**
 * Gets a list of all devices the VM can grab
 */
PUSBDEVICE USBProxyBackendWindows::getDevices(void)
{
    PUSBDEVICE pDevices = NULL;
    uint32_t cDevices = 0;

    Log(("USBProxyBackendWindows::getDevices\n"));
    USBLibGetDevices(&pDevices, &cDevices);
    return pDevices;
}

