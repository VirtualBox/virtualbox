/* $Id: USBUninstall.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox host drivers - USB drivers - Filter & driver uninstallation.
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
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <VBox/VBoxDrvCfg-win.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The support service name. */
#define SERVICE_NAME    "VBoxUSBMon"
/** Win32 Device name. */
#define DEVICE_NAME     "\\\\.\\VBoxUSBMon"
/** NT Device name. */
#define DEVICE_NAME_NT   L"\\Device\\VBoxUSBMon"
/** Win32 Symlink name. */
#define DEVICE_NAME_DOS  L"\\DosDevices\\VBoxUSBMon"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int usblibOsStopService(void);
static int usblibOsDeleteService(void);


static DECLCALLBACK(void) vboxUsbLog(VBOXDRVCFG_LOG_SEVERITY_T enmSeverity, char *pszMsg, void *pvContext)
{
    RT_NOREF1(pvContext);
    switch (enmSeverity)
    {
        case VBOXDRVCFG_LOG_SEVERITY_FLOW:
        case VBOXDRVCFG_LOG_SEVERITY_REGULAR:
            break;
        case VBOXDRVCFG_LOG_SEVERITY_REL:
            RTMsgInfo("%s", pszMsg);
            break;
        default:
            break;
    }
}

static DECLCALLBACK(void) vboxUsbPanic(void *pvPanic)
{
    RT_NOREF1(pvPanic);
#ifndef DEBUG_bird
    AssertFailed();
#endif
}


int __cdecl main(int argc, char **argv)
{
    RTR3InitExeNoArguments(0);
    if (argc != 1)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "This utility takes no arguments\n");
    NOREF(argv);
    RTMsgInfo("USB uninstallation\n");

    VBoxDrvCfgLoggerSet(vboxUsbLog, NULL);
    VBoxDrvCfgPanicSet(vboxUsbPanic, NULL);

    usblibOsStopService();
    usblibOsDeleteService();

    HRESULT hr = VBoxDrvCfgInfUninstallAllF(L"USB", L"USB\\VID_80EE&PID_CAFE", SUOI_FORCEDELETE);
    if (hr != S_OK)
        return RTMsgErrorExitFailure("SetupUninstallOEMInf failed: %Rhrc\n", hr);

    RTMsgInfo("USB uninstallation succeeded!");
    return 0;
}


/**
 * Stops a possibly running service.
 *
 * @returns 0 on success.
 * @returns -1 on failure.
 */
static int usblibOsStopService(void)
{
    /*
     * Assume it didn't exist, so we'll create the service.
     */
    int rc = -1;
    SC_HANDLE   hSMgr = OpenSCManager(NULL, NULL, SERVICE_STOP | SERVICE_QUERY_STATUS);
    AssertMsg(hSMgr, ("OpenSCManager(,,delete) failed rc=%d\n", GetLastError()));
    if (hSMgr)
    {
        SC_HANDLE hService = OpenService(hSMgr, SERVICE_NAME, SERVICE_STOP | SERVICE_QUERY_STATUS);
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
                /*
                 * Wait for finish about 1 minute.
                 * It should be enough for work with driver verifier
                 */
                int iWait = 600;
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
 * Deletes the service.
 *
 * @returns 0 on success.
 * @returns -1 on failure.
 */
static int usblibOsDeleteService(void)
{
    /*
     * Assume it didn't exist, so we'll create the service.
     */
    int rc = -1;
    SC_HANDLE hSMgr = OpenSCManager(NULL, NULL, SERVICE_CHANGE_CONFIG);
    AssertMsg(hSMgr, ("OpenSCManager(,,delete) failed rc=%d\n", GetLastError()));
    if (hSMgr)
    {
        SC_HANDLE hService = OpenService(hSMgr, SERVICE_NAME, DELETE);
        if (hService)
        {
            /*
             * Delete the service.
             */
            if (DeleteService(hService))
                rc = 0;
            else
                AssertMsgFailed(("DeleteService failed LastError=%Rwa\n", GetLastError()));
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
