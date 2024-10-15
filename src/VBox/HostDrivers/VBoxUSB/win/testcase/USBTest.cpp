/* $Id: USBTest.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox host drivers - USB drivers - Filter & driver installation
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/win/windows.h>
#include <iprt/win/setupapi.h>
#include <newdev.h>

#include <iprt/assert.h>
#include <VBox/err.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <VBox/usblib.h>
#include <VBox/VBoxDrvCfg-win.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Handle to the open device. */
static HANDLE   g_hUSBMonitor = INVALID_HANDLE_VALUE;
/** Flags whether or not we started the service. */
static bool     g_fStartedService = false;


/**
 * Attempts to start the service, creating it if necessary.
 *
 * @returns 0 on success.
 * @returns -1 on failure.
 * @param   fRetry  Indicates retry call.
 */
int usbMonStartService(void)
{
    HRESULT hr = VBoxDrvCfgSvcStart(USBMON_SERVICE_NAME_W);
    if (hr != S_OK)
    {
        AssertMsgFailed(("couldn't start service, hr (0x%x)\n", hr));
        return -1;
    }
    return 0;
}

/**
 * Stops a possibly running service.
 *
 * @returns 0 on success.
 * @returns -1 on failure.
 */
int usbMonStopService(void)
{
    RTPrintf("usbMonStopService\n");

    /*
     * Assume it didn't exist, so we'll create the service.
     */
    int rc = -1;
    SC_HANDLE   hSMgr = OpenSCManager(NULL, NULL, SERVICE_STOP | SERVICE_QUERY_STATUS);
    AssertMsg(hSMgr, ("OpenSCManager(,,delete) failed rc=%d\n", GetLastError()));
    if (hSMgr)
    {
        SC_HANDLE hService = OpenServiceW(hSMgr, USBMON_SERVICE_NAME_W, SERVICE_STOP | SERVICE_QUERY_STATUS);
        if (hService)
        {
            /*
             * Stop the service.
             */
            SERVICE_STATUS  Status;
            QueryServiceStatus(hService, &Status);
            if (Status.dwCurrentState == SERVICE_STOPPED)
                rc = 0;
            else if (ControlService(hService, SERVICE_CONTROL_STOP, &Status))
            {
                int iWait = 100;
                while (Status.dwCurrentState == SERVICE_STOP_PENDING && iWait-- > 0)
                {
                    Sleep(100);
                    QueryServiceStatus(hService, &Status);
                }
                if (Status.dwCurrentState == SERVICE_STOPPED)
                    rc = 0;
                else
                   AssertMsgFailed(("Failed to stop service. status=%d\n", Status.dwCurrentState));
            }
            else
                AssertMsgFailed(("ControlService failed with LastError=%Rwa. status=%d\n", GetLastError(), Status.dwCurrentState));
            CloseServiceHandle(hService);
        }
        else if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST)
            rc = 0;
        else
            AssertMsgFailed(("OpenService failed LastError=%Rwa\n", GetLastError()));
        CloseServiceHandle(hSMgr);
    }
    return rc;
}

/**
 * Release specified USB device to the host.
 *
 * @returns VBox status code
 * @param usVendorId        Vendor id
 * @param usProductId       Product id
 * @param usRevision        Revision
 */
int usbMonReleaseDevice(USHORT usVendorId, USHORT usProductId, USHORT usRevision)
{
    USBSUP_RELEASE release;
    DWORD          cbReturned = 0;

    RTPrintf("usbLibReleaseDevice %x %x %x\n", usVendorId, usProductId, usRevision);

    release.usVendorId  = usVendorId;
    release.usProductId = usProductId;
    release.usRevision  = usRevision;

    if (!DeviceIoControl(g_hUSBMonitor, SUPUSBFLT_IOCTL_RELEASE_DEVICE, &release, sizeof(release),  NULL, 0, &cbReturned, NULL))
    {
        AssertMsgFailed(("DeviceIoControl failed with %d\n", GetLastError()));
        return RTErrConvertFromWin32(GetLastError());
    }

    return VINF_SUCCESS;
}


/**
 * Add USB device filter
 *
 * @returns VBox status code.
 * @param   usVendorId      Vendor id
 * @param   usProductId     Product id
 * @param   usRevision      Revision
 * @param   ppID            Pointer to filter id
 */
int usbMonInsertFilter(USHORT usVendorId, USHORT usProductId, USHORT usRevision, void **ppID)
{
    USBFILTER           filter;
    USBSUP_FLTADDOUT    flt_add;
    DWORD               cbReturned = 0;

    Assert(g_hUSBMonitor);

    RTPrintf("usblibInsertFilter %04X %04X %04X\n", usVendorId, usProductId, usRevision);

    USBFilterInit(&filter, USBFILTERTYPE_CAPTURE);
    USBFilterSetNumExact(&filter, USBFILTERIDX_VENDOR_ID, usVendorId, true);
    USBFilterSetNumExact(&filter, USBFILTERIDX_PRODUCT_ID, usProductId, true);
    USBFilterSetNumExact(&filter, USBFILTERIDX_DEVICE_REV, usRevision, true);

    if (!DeviceIoControl(g_hUSBMonitor, SUPUSBFLT_IOCTL_ADD_FILTER, &filter, sizeof(filter), &flt_add, sizeof(flt_add), &cbReturned, NULL))
    {
        AssertMsgFailed(("DeviceIoControl failed with %d\n", GetLastError()));
        return RTErrConvertFromWin32(GetLastError());
    }
    *ppID = (void *)flt_add.uId;
    return VINF_SUCCESS;
}

/**
 * Applies existing filters to currently plugged-in USB devices
 *
 * @returns VBox status code.
 */
int usbMonRunFilters(void)
{
    DWORD               cbReturned = 0;

    Assert(g_hUSBMonitor);

    if (!DeviceIoControl(g_hUSBMonitor, SUPUSBFLT_IOCTL_RUN_FILTERS, NULL, 0, NULL, 0, &cbReturned, NULL))
    {
        AssertMsgFailed(("DeviceIoControl failed with %d\n", GetLastError()));
        return RTErrConvertFromWin32(GetLastError());
    }
    return VINF_SUCCESS;
}

/**
 * Remove USB device filter
 *
 * @returns VBox status code.
 * @param   aID             Filter id
 */
int usbMonRemoveFilter (void *aID)
{
    uintptr_t   uId;
    DWORD       cbReturned = 0;

    Assert(g_hUSBMonitor);

    RTPrintf("usblibRemoveFilter %p\n", aID);

    uId = (uintptr_t)aID;
    if (!DeviceIoControl(g_hUSBMonitor, SUPUSBFLT_IOCTL_REMOVE_FILTER, &uId, sizeof(uId),  NULL, 0,&cbReturned, NULL))
    {
        AssertMsgFailed(("DeviceIoControl failed with %d\n", GetLastError()));
        return RTErrConvertFromWin32(GetLastError());
    }
    return VINF_SUCCESS;
}

/**
 * Initialize the USB monitor
 *
 * @returns VBox status code.
 */
int usbMonitorInit()
{
    int            rc;
    USBSUP_VERSION version = {0};
    DWORD          cbReturned;

    RTPrintf("usbproxy: usbLibInit\n");

    g_hUSBMonitor = CreateFile (USBMON_DEVICE_NAME,
                               GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, // no SECURITY_ATTRIBUTES structure
                               OPEN_EXISTING, // No special create flags
                               FILE_ATTRIBUTE_SYSTEM,
                               NULL); // No template file

    if (g_hUSBMonitor == INVALID_HANDLE_VALUE)
    {
        usbMonStartService();

        g_hUSBMonitor = CreateFile (USBMON_DEVICE_NAME,
                                   GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, // no SECURITY_ATTRIBUTES structure
                                   OPEN_EXISTING, // No special create flags
                                   FILE_ATTRIBUTE_SYSTEM,
                                   NULL); // No template file

        if (g_hUSBMonitor == INVALID_HANDLE_VALUE)
        {
            /* AssertFailed(); */
            RTPrintf("usbproxy: Unable to open filter driver!! (rc=%lu)\n", GetLastError());
            rc = VERR_FILE_NOT_FOUND;
            goto failure;
        }
    }

    /*
     * Check the version
     */
    cbReturned = 0;
    if (!DeviceIoControl(g_hUSBMonitor, SUPUSBFLT_IOCTL_GET_VERSION, NULL, 0,&version, sizeof(version),  &cbReturned, NULL))
    {
        RTPrintf("usbproxy: Unable to query filter version!! (rc=%lu)\n", GetLastError());
        rc = VERR_VERSION_MISMATCH;
        goto failure;
    }

    if (   version.u32Major != USBMON_MAJOR_VERSION
#if USBMON_MINOR_VERSION != 0
        || version.u32Minor < USBMON_MINOR_VERSION
#endif
        )
    {
        RTPrintf("usbproxy: Filter driver version mismatch!!\n");
        rc = VERR_VERSION_MISMATCH;
        goto failure;
    }

    return VINF_SUCCESS;

failure:
    if (g_hUSBMonitor != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hUSBMonitor);
        g_hUSBMonitor = INVALID_HANDLE_VALUE;
    }
    return rc;
}



/**
 * Terminate the USB monitor
 *
 * @returns VBox status code.
 */
int usbMonitorTerm()
{
    if (g_hUSBMonitor != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hUSBMonitor);
        g_hUSBMonitor = INVALID_HANDLE_VALUE;
    }
    /*
     * If we started the service we might consider stopping it too.
     *
     * Since this won't work unless the process starting it is the
     * last user we might wanna skip this...
     */
    if (g_fStartedService)
    {
        usbMonStopService();
        g_fStartedService = false;
    }

    return VINF_SUCCESS;
}


int __cdecl main(int argc, char **argv)
{
    int rc;
    int c;
    RT_NOREF2(argc, argv);

    RTPrintf("USB test\n");

    rc = usbMonitorInit();
    AssertRC(rc);

    void *pId1, *pId2, *pId3;

    usbMonInsertFilter(0x0529, 0x0514, 0x0100, &pId1);
    usbMonInsertFilter(0x0A16, 0x2499, 0x0100, &pId2);
    usbMonInsertFilter(0x80EE, 0x0030, 0x0110, &pId3);

    RTPrintf("Waiting to capture devices... enter 'r' to run filters\n");
    c = RTStrmGetCh(g_pStdIn);
    if (c == 'r')
    {
        usbMonRunFilters();
        RTPrintf("Waiting to capture devices...\n");
        RTStrmGetCh(g_pStdIn);  /* eat the '\n' */
        RTStrmGetCh(g_pStdIn);  /* wait for more input */
    }

    RTPrintf("Releasing device\n");
    usbMonReleaseDevice(0xA16, 0x2499, 0x100);

    usbMonRemoveFilter(pId1);
    usbMonRemoveFilter(pId2);
    usbMonRemoveFilter(pId3);

    rc = usbMonitorTerm();

    return 0;
}

