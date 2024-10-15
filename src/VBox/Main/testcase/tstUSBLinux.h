/* $Id: tstUSBLinux.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox USB Proxy Service class, test version for Linux hosts.
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

#ifndef MAIN_INCLUDED_SRC_testcase_tstUSBLinux_h
#define MAIN_INCLUDED_SRC_testcase_tstUSBLinux_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

typedef int HRESULT;
enum { S_OK = 0, E_NOTIMPL = 1 };

#include <VBox/usb.h>
#include <VBox/usbfilter.h>

#include <VBox/err.h>

#ifdef VBOX_USB_WITH_SYSFS
# include <libhal.h>
#endif

#include <stdio.h>
/**
 * The Linux hosted USB Proxy Service.
 */
class USBProxyServiceLinux
{
public:
    USBProxyServiceLinux()
        : mLastError(VINF_SUCCESS)
    {}

    HRESULT initSysfs(void);
    PUSBDEVICE getDevicesFromSysfs(void);
    int getLastError(void)
    {
        return mLastError;
    }

private:
    int start(void) { return VINF_SUCCESS; }
    static void freeDevice(PUSBDEVICE) {}  /* We don't care about leaks in a test. */
    int usbProbeInterfacesFromLibhal(const char *pszHalUuid, PUSBDEVICE pDev);
    int mLastError;
#  ifdef VBOX_USB_WITH_SYSFS
    /** Our connection to DBus for getting information from hal.  This will be
     * NULL if the initialisation failed. */
    DBusConnection *mDBusConnection;
    /** Handle to libhal. */
    LibHalContext *mLibHalContext;
#  endif
};

#endif /* !MAIN_INCLUDED_SRC_testcase_tstUSBLinux_h */

