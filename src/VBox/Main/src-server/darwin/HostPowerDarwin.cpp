/* $Id: HostPowerDarwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox interface to host's power notification service, darwin specifics.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_HOST
#include "HostPower.h"
#include "LoggingNew.h"
#include <iprt/errcore.h>

#include <IOKit/IOMessage.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>

#define POWER_SOURCE_OUTLET 1
#define POWER_SOURCE_BATTERY 2

HostPowerServiceDarwin::HostPowerServiceDarwin(VirtualBox *aVirtualBox)
  : HostPowerService(aVirtualBox)
  , mThread(NULL)
  , mRootPort(MACH_PORT_NULL)
  , mNotifyPort(nil)
  , mRunLoop(nil)
  , mCritical(false)
{
    /* Create the new worker thread. */
    int vrc = RTThreadCreate(&mThread, HostPowerServiceDarwin::powerChangeNotificationThread, this, 65536,
                             RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "MainPower");
    AssertLogRelRC(vrc);
}

HostPowerServiceDarwin::~HostPowerServiceDarwin()
{
    /* Jump out of the run loop. */
    CFRunLoopStop(mRunLoop);
    /* Remove the sleep notification port from the application runloop. */
    CFRunLoopRemoveSource(CFRunLoopGetCurrent(),
                          IONotificationPortGetRunLoopSource(mNotifyPort),
                          kCFRunLoopCommonModes);
    /* Deregister for system sleep notifications. */
    IODeregisterForSystemPower(&mNotifierObject);
    /* IORegisterForSystemPower implicitly opens the Root Power Domain
     * IOService so we close it here. */
    IOServiceClose(mRootPort);
    /* Destroy the notification port allocated by IORegisterForSystemPower */
    IONotificationPortDestroy(mNotifyPort);
}


DECLCALLBACK(int) HostPowerServiceDarwin::powerChangeNotificationThread(RTTHREAD /* ThreadSelf */, void *pInstance)
{
    HostPowerServiceDarwin *pPowerObj = static_cast<HostPowerServiceDarwin *>(pInstance);

    /* We have to initial set the critical state of the battery, cause we want
     * not the HostPowerService to inform about that state when a VM starts.
     * See lowPowerHandler for more info. */
    pPowerObj->checkBatteryCriticalLevel();

    /* Register to receive system sleep notifications */
    pPowerObj->mRootPort = IORegisterForSystemPower(pPowerObj, &pPowerObj->mNotifyPort,
                                                    HostPowerServiceDarwin::powerChangeNotificationHandler,
                                                    &pPowerObj->mNotifierObject);
    if (pPowerObj->mRootPort == MACH_PORT_NULL)
    {
        LogFlow(("IORegisterForSystemPower failed\n"));
        return VERR_NOT_SUPPORTED;
    }
    pPowerObj->mRunLoop = CFRunLoopGetCurrent();
    /* Add the notification port to the application runloop */
    CFRunLoopAddSource(pPowerObj->mRunLoop,
                       IONotificationPortGetRunLoopSource(pPowerObj->mNotifyPort),
                       kCFRunLoopCommonModes);

    /* Register for all battery change events. The handler will check for low
     * power events itself. */
    CFRunLoopSourceRef runLoopSource = IOPSNotificationCreateRunLoopSource(HostPowerServiceDarwin::lowPowerHandler,
                                                                           pPowerObj);
    CFRunLoopAddSource(pPowerObj->mRunLoop,
                       runLoopSource,
                       kCFRunLoopCommonModes);

    /* Start the run loop. This blocks. */
    CFRunLoopRun();
    return VINF_SUCCESS;
}

void HostPowerServiceDarwin::powerChangeNotificationHandler(void *pvData, io_service_t /* service */, natural_t messageType, void *pMessageArgument)
{
    HostPowerServiceDarwin *pPowerObj = static_cast<HostPowerServiceDarwin *>(pvData);
    Log(( "powerChangeNotificationHandler: messageType %08lx, arg %08lx\n", (long unsigned int)messageType, (long unsigned int)pMessageArgument));

    switch (messageType)
    {
        case kIOMessageCanSystemSleep:
            {
                /* Idle sleep is about to kick in. This message will not be
                 * sent for forced sleep. Applications have a chance to prevent
                 * sleep by calling IOCancelPowerChange. Most applications
                 * should not prevent idle sleep. Power Management waits up to
                 * 30 seconds for you to either allow or deny idle sleep. If
                 * you don't acknowledge this power change by calling either
                 * IOAllowPowerChange or IOCancelPowerChange, the system will
                 * wait 30 seconds then go to sleep. */
                IOAllowPowerChange(pPowerObj->mRootPort, reinterpret_cast<long>(pMessageArgument));
                break;
            }
        case kIOMessageSystemWillSleep:
            {
                /* The system will go for sleep. */
                pPowerObj->notify(Reason_HostSuspend);
                /* If you do not call IOAllowPowerChange or IOCancelPowerChange to
                 * acknowledge this message, sleep will be delayed by 30 seconds.
                 * NOTE: If you call IOCancelPowerChange to deny sleep it returns
                 * kIOReturnSuccess, however the system WILL still go to sleep. */
                IOAllowPowerChange(pPowerObj->mRootPort, reinterpret_cast<long>(pMessageArgument));
                break;
            }
        case kIOMessageSystemWillPowerOn:
            {
                /* System has started the wake up process. */
                break;
            }
        case kIOMessageSystemHasPoweredOn:
            {
                /* System has finished the wake up process. */
                pPowerObj->notify(Reason_HostResume);
                break;
            }
        default:
            break;
    }
}

void HostPowerServiceDarwin::lowPowerHandler(void *pvData)
{
    HostPowerServiceDarwin *pPowerObj = static_cast<HostPowerServiceDarwin *>(pvData);

    /* Following role for sending the BatteryLow event(5% is critical):
     * - Not at VM start even if the battery is in an critical state already.
     * - When the power cord is removed so the power supply change from AC to
     *   battery & the battery is in an critical state nothing is triggered.
     *   This has to be discussed.
     * - When the power supply is the battery & the state of the battery level
     *   changed from normal to critical. The state transition from critical to
     *   normal triggers nothing. */
    bool fCriticalStateChanged = false;
    pPowerObj->checkBatteryCriticalLevel(&fCriticalStateChanged);
    if (fCriticalStateChanged)
        pPowerObj->notify(Reason_HostBatteryLow);
}

void HostPowerServiceDarwin::checkBatteryCriticalLevel(bool *pfCriticalChanged)
{
    CFTypeRef pBlob = IOPSCopyPowerSourcesInfo();
    CFArrayRef pSources = IOPSCopyPowerSourcesList(pBlob);

    CFDictionaryRef pSource = NULL;
    const void *psValue;
    bool result;
    int powerSource = POWER_SOURCE_OUTLET;
    bool critical = false;

    if (CFArrayGetCount(pSources) > 0)
    {
        for (int i = 0; i < CFArrayGetCount(pSources); ++i)
        {
            pSource = IOPSGetPowerSourceDescription(pBlob, CFArrayGetValueAtIndex(pSources, i));
            /* If the source is empty skip over to the next one. */
            if (!pSource)
                continue;
            /* Skip all power sources which are currently not present like a
             * second battery. */
            if (CFDictionaryGetValue(pSource, CFSTR(kIOPSIsPresentKey)) == kCFBooleanFalse)
                continue;
            /* Only internal power types are of interest. */
            result = CFDictionaryGetValueIfPresent(pSource, CFSTR(kIOPSTransportTypeKey), &psValue);
            if (result &&
                CFStringCompare((CFStringRef)psValue, CFSTR(kIOPSInternalType), 0) == kCFCompareEqualTo)
            {
                /* First check which power source we are connect on. */
                result = CFDictionaryGetValueIfPresent(pSource, CFSTR(kIOPSPowerSourceStateKey), &psValue);
                if (result &&
                    CFStringCompare((CFStringRef)psValue, CFSTR(kIOPSACPowerValue), 0) == kCFCompareEqualTo)
                    powerSource = POWER_SOURCE_OUTLET;
                else if (result &&
                         CFStringCompare((CFStringRef)psValue, CFSTR(kIOPSBatteryPowerValue), 0) == kCFCompareEqualTo)
                    powerSource = POWER_SOURCE_BATTERY;


                /* Fetch the current capacity value of the power source */
                int curCapacity = 0;
                result = CFDictionaryGetValueIfPresent(pSource, CFSTR(kIOPSCurrentCapacityKey), &psValue);
                if (result)
                    CFNumberGetValue((CFNumberRef)psValue, kCFNumberSInt32Type, &curCapacity);

                /* Fetch the maximum capacity value of the power source */
                int maxCapacity = 1;
                result = CFDictionaryGetValueIfPresent(pSource, CFSTR(kIOPSMaxCapacityKey), &psValue);
                if (result)
                    CFNumberGetValue((CFNumberRef)psValue, kCFNumberSInt32Type, &maxCapacity);

                /* Calculate the remaining capacity in percent */
                float remCapacity = ((float)curCapacity/(float)maxCapacity * 100.0f);

                /* Check for critical. 5 percent is default. */
                int criticalValue = 5;
                result = CFDictionaryGetValueIfPresent(pSource, CFSTR(kIOPSDeadWarnLevelKey), &psValue);
                if (result)
                    CFNumberGetValue((CFNumberRef)psValue, kCFNumberSInt32Type, &criticalValue);
                critical = remCapacity < criticalValue;

                /* We have to take action only if we are on battery, the
                 * previous state wasn't critical, the state has changed & the
                 * user requested that info. */
                if (powerSource == POWER_SOURCE_BATTERY &&
                    mCritical == false &&
                    mCritical != critical &&
                    pfCriticalChanged)
                    *pfCriticalChanged = true;
                Log(("checkBatteryCriticalLevel: Remains: %d.%d%% Critical: %d Critical State Changed: %d\n", (int)remCapacity, (int)(remCapacity * 10) % 10, critical, pfCriticalChanged?*pfCriticalChanged:-1));
            }
        }
    }
    /* Save the new state */
    mCritical = critical;

    CFRelease(pBlob);
    CFRelease(pSources);
}

