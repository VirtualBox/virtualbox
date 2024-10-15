/* $Id: tstUSBProxyLinux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * USBProxyBackendLinux test case.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/

#include "USBGetDevices.h"

#include <VBox/err.h>
#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/string.h>
#include <iprt/test.h>

/*** BEGIN STUBS ***/

static struct
{
    const char *pcszEnvUsb;
    const char *pcszEnvUsbRoot;
    const char *pcszDevicesRoot;
    bool fDevicesAccessible;
    const char *pcszUsbfsRoot;
    bool fUsbfsAccessible;
    int rcMethodInit;
    const char *pcszDevicesRootExpected;
    bool fUsingUsbfsExpected;
    int rcExpected;
} s_testEnvironment[] =
{
    /* "sysfs" and valid root in the environment */
    { "sysfs", "/dev/bus/usb", "/dev/bus/usb", true, NULL, false, VINF_SUCCESS, "/dev/bus/usb", false, VINF_SUCCESS },
    /* "sysfs" and bad root in the environment */
    { "sysfs", "/dev/bus/usb", "/dev/vboxusb", false, "/proc/usb/bus", false, VINF_SUCCESS, "", true, VERR_NOT_FOUND },
    /* "sysfs" and no root in the environment */
    { "sysfs", NULL, "/dev/vboxusb", true, NULL, false, VINF_SUCCESS, "/dev/vboxusb", false, VINF_SUCCESS },
    /* "usbfs" and valid root in the environment */
    { "usbfs", "/dev/bus/usb", NULL, false, "/dev/bus/usb", true, VINF_SUCCESS, "/dev/bus/usb", true, VINF_SUCCESS },
    /* "usbfs" and bad root in the environment */
    { "usbfs", "/dev/bus/usb", "/dev/vboxusb", false, "/proc/usb/bus", false, VINF_SUCCESS, "", true, VERR_NOT_FOUND },
    /* "usbfs" and no root in the environment */
    { "usbfs", NULL, NULL, false, "/proc/bus/usb", true, VINF_SUCCESS, "/proc/bus/usb", true, VINF_SUCCESS },
    /* invalid method in the environment, sysfs available */
    { "invalid", "/dev/bus/usb", "/dev/vboxusb", true, NULL, false, VINF_SUCCESS, "/dev/vboxusb", false, VINF_SUCCESS },
    /* invalid method in the environment, usbfs available */
    { "invalid", "/dev/bus/usb", NULL, true, "/proc/bus/usb", true, VINF_SUCCESS, "/proc/bus/usb", true, VINF_SUCCESS },
    /* invalid method in the environment, sysfs inaccessible */
    { "invalid", "/dev/bus/usb", "/dev/vboxusb", false, NULL, false, VINF_SUCCESS, "", true, VERR_VUSB_USB_DEVICE_PERMISSION },
    /* invalid method in the environment, usbfs inaccessible */
    { "invalid", "/dev/bus/usb", NULL, false, "/proc/bus/usb", false, VINF_SUCCESS, "", true, VERR_VUSB_USBFS_PERMISSION },
    /* No environment, sysfs and usbfs available but without access permissions. */
    { NULL, NULL, "/dev/vboxusb", false, "/proc/bus/usb", false, VERR_NO_MEMORY, "", true, VERR_VUSB_USB_DEVICE_PERMISSION },
    /* No environment, sysfs and usbfs available, access permissions for sysfs. */
    { NULL, NULL, "/dev/vboxusb", true, "/proc/bus/usb", false, VINF_SUCCESS, "/dev/vboxusb", false, VINF_SUCCESS },
    /* No environment, sysfs and usbfs available, access permissions for usbfs. */
    { NULL, NULL, "/dev/vboxusb", false, "/proc/bus/usb", true, VINF_SUCCESS, "/proc/bus/usb", true, VINF_SUCCESS },
    /* No environment, sysfs available but without access permissions. */
    { NULL, NULL, "/dev/vboxusb", false, NULL, false, VERR_NO_MEMORY, "", true, VERR_VUSB_USB_DEVICE_PERMISSION },
    /* No environment, usbfs available but without access permissions. */
    { NULL, NULL, NULL, false, "/proc/bus/usb", false, VERR_NO_MEMORY, "", true, VERR_VUSB_USBFS_PERMISSION },
};

static void testInit(RTTEST hTest)
{
    RTTestSub(hTest, "Testing USBProxyLinuxChooseMethod");
    for (unsigned i = 0; i < RT_ELEMENTS(s_testEnvironment); ++i)
    {
        bool fUsingUsbfs = true;
        const char *pcszDevicesRoot = "";

        TestUSBSetEnv(s_testEnvironment[i].pcszEnvUsb,
                      s_testEnvironment[i].pcszEnvUsbRoot);
        TestUSBSetupInit(s_testEnvironment[i].pcszUsbfsRoot,
                         s_testEnvironment[i].fUsbfsAccessible,
                         s_testEnvironment[i].pcszDevicesRoot,
                         s_testEnvironment[i].fDevicesAccessible,
                         s_testEnvironment[i].rcMethodInit);
        int rc = USBProxyLinuxChooseMethod(&fUsingUsbfs, &pcszDevicesRoot);
        RTTESTI_CHECK_MSG(rc == s_testEnvironment[i].rcExpected,
                          ("rc=%Rrc (test index %i) instead of %Rrc!\n",
                           rc, i, s_testEnvironment[i].rcExpected));
        RTTESTI_CHECK_MSG(!RTStrCmp(pcszDevicesRoot,
                               s_testEnvironment[i].pcszDevicesRootExpected),
                          ("testGetDevicesRoot() returned %s (test index %i) instead of %s!\n",
                           pcszDevicesRoot, i,
                           s_testEnvironment[i].pcszDevicesRootExpected));
        RTTESTI_CHECK_MSG(   fUsingUsbfs
                          == s_testEnvironment[i].fUsingUsbfsExpected,
                          ("testGetUsingUsbfs() returned %RTbool (test index %i) instead of %RTbool!\n",
                           fUsingUsbfs, i,
                           s_testEnvironment[i].fUsingUsbfsExpected));
    }
}

static struct
{
    const char *pacszDeviceAddresses[16];
    const char *pacszAccessibleFiles[16];
    const char *pcszRoot;
    bool fIsDeviceNodes;
    bool fAvailableExpected;
} s_testCheckDeviceRoot[] =
{
    /* /dev/vboxusb accessible -> device nodes method available */
    { { NULL }, { "/dev/vboxusb" }, "/dev/vboxusb", true, true },
    /* /dev/vboxusb present but not accessible -> device nodes method not
     * available */
    { { NULL }, { NULL }, "/dev/vboxusb", true, false },
    /* /proc/bus/usb available but empty -> usbfs method available (we can't
     * really check in this case) */
    { { NULL }, { "/proc/bus/usb" }, "/proc/bus/usb", false, true },
    /* /proc/bus/usb not available or not accessible -> usbfs method not available */
    { { NULL }, { NULL }, "/proc/bus/usb", false, false },
    /* /proc/bus/usb available, one inaccessible device -> usbfs method not
     * available */
    { { "/proc/bus/usb/001/001" }, { "/proc/bus/usb" }, "/proc/bus/usb", false, false },
    /* /proc/bus/usb available, one device of two inaccessible -> usbfs method
     * not available */
    { { "/proc/bus/usb/001/001", "/proc/bus/usb/002/002" },
      { "/proc/bus/usb", "/proc/bus/usb/001/001" }, "/proc/bus/usb", false, false },
    /* /proc/bus/usb available, two accessible devices -> usbfs method
     * available */
    { { "/proc/bus/usb/001/001", "/proc/bus/usb/002/002" },
      { "/proc/bus/usb", "/proc/bus/usb/001/001", "/proc/bus/usb/002/002" },
      "/proc/bus/usb", false, true }
};

static void testCheckDeviceRoot(RTTEST hTest)
{
    RTTestSub(hTest, "Testing the USBProxyLinuxCheckDeviceRoot API");
    for (unsigned i = 0; i < RT_ELEMENTS(s_testCheckDeviceRoot); ++i)
    {
        TestUSBSetAvailableUsbfsDevices(s_testCheckDeviceRoot[i]
                                                .pacszDeviceAddresses);
        TestUSBSetAccessibleFiles(s_testCheckDeviceRoot[i]
                                                .pacszAccessibleFiles);
        bool fAvailable = USBProxyLinuxCheckDeviceRoot
                                  (s_testCheckDeviceRoot[i].pcszRoot,
                                   s_testCheckDeviceRoot[i].fIsDeviceNodes);
        RTTESTI_CHECK_MSG(   fAvailable
                          == s_testCheckDeviceRoot[i].fAvailableExpected,
                           ("USBProxyLinuxCheckDeviceRoot() returned %RTbool (test index %i) instead of %RTbool!\n",
                            fAvailable, i,
                            s_testCheckDeviceRoot[i].fAvailableExpected));
    }
}

int main(void)
{
    /*
     * Init the runtime, test and say hello.
     */
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstUSBProxyLinux", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    /*
     * Run the tests.
     */
    testInit(hTest);
    testCheckDeviceRoot(hTest);

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(hTest);
}
