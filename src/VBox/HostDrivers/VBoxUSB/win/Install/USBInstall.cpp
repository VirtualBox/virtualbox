/* $Id: USBInstall.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox host drivers - USB drivers - Filter & driver installation, Installation code.
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
#include <iprt/process.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/utf16.h>

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
int usblibOsCreateService(void);


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
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    RTMsgInfo("USB installation");

    VBoxDrvCfgLoggerSet(vboxUsbLog, NULL);
    VBoxDrvCfgPanicSet(vboxUsbPanic, NULL);

    rc = usblibOsCreateService();
    if (RT_SUCCESS(rc))
    {
        /* Build the path to the INF file: */
        char szInfFile[RTPATH_MAX];
        rc = RTProcGetExecutablePath(szInfFile, sizeof(szInfFile)) ? VINF_SUCCESS : VERR_BUFFER_OVERFLOW;
        if (RT_SUCCESS(rc))
        {
            RTPathStripFilename(szInfFile);
            rc = RTPathAppend(szInfFile, sizeof(szInfFile), "VBoxUSB.inf");
        }
        PRTUTF16 pwszInfFile = NULL;
        if (RT_SUCCESS(rc))
            rc = RTStrToUtf16(szInfFile, &pwszInfFile);
        if (RT_SUCCESS(rc))
        {
            /* Install the INF file: */
            HRESULT hr = VBoxDrvCfgInfInstall(pwszInfFile);
            if (hr == S_OK)
                RTMsgInfo("Installation successful!");
            else
            {
                RTMsgError("Installation failed: %Rhrc", hr);
                rc = VERR_GENERAL_FAILURE;
            }

            RTUtf16Free(pwszInfFile);
        }
        else
            RTMsgError("Failed to construct INF path: %Rrc", rc);
    }
    else
        RTMsgError("Service creation failed: %Rrc", rc);

    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/**
 * Changes the USB driver service to specified driver path.
 *
 * @returns 0 on success.
 * @returns < 0 on failure.
 */
int usblibOsChangeService(const char *pszDriverPath)
{
    SC_HANDLE hSMgrCreate = OpenSCManager(NULL, NULL, SERVICE_CHANGE_CONFIG);
    DWORD dwLastError = GetLastError();
    int rc = RTErrConvertFromWin32(dwLastError);
    AssertPtr(pszDriverPath);
    AssertMsg(hSMgrCreate, ("OpenSCManager(,,create) failed rc=%d\n", dwLastError));
    if (hSMgrCreate)
    {
        SC_HANDLE hService = OpenService(hSMgrCreate,
                                         SERVICE_NAME,
                                         GENERIC_ALL);
        dwLastError = GetLastError();
        if (hService == NULL)
        {
            AssertMsg(hService, ("OpenService failed! LastError=%Rwa, pszDriver=%s\n", dwLastError, pszDriverPath));
            rc = RTErrConvertFromWin32(dwLastError);
        }
        else
        {
            /* We only gonna change the driver image path, the rest remains like it already is */
            if (ChangeServiceConfig(hService,
                                    SERVICE_NO_CHANGE,
                                    SERVICE_NO_CHANGE,
                                    SERVICE_NO_CHANGE,
                                    pszDriverPath,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL))
            {
                RTPrintf("Changed service config to new driver path: %s\n", pszDriverPath);
            }
            else
            {
                AssertMsg(hService, ("ChangeServiceConfig failed! LastError=%Rwa, pszDriver=%s\n", dwLastError, pszDriverPath));
                rc = RTErrConvertFromWin32(dwLastError);
            }
            if (hService != NULL)
                CloseServiceHandle(hService);
        }

        CloseServiceHandle(hSMgrCreate);
    }
    return rc;
}


/**
 * Creates the service.
 *
 * @returns 0 on success.
 * @returns < 0 on failure.
 */
int usblibOsCreateService(void)
{
    /*
     * Assume it didn't exist, so we'll create the service.
     */
    SC_HANDLE hSMgrCreate = OpenSCManager(NULL, NULL, SERVICE_CHANGE_CONFIG);
    DWORD dwLastError = GetLastError();
    int rc = RTErrConvertFromWin32(dwLastError);
    AssertMsg(hSMgrCreate, ("OpenSCManager(,,create) failed rc=%d\n", dwLastError));
    if (hSMgrCreate)
    {
        char szDriver[RTPATH_MAX];
        rc = RTPathExecDir(szDriver, sizeof(szDriver) - sizeof("\\VBoxUSBMon.sys"));
        if (RT_SUCCESS(rc))
        {
            strcat(szDriver, "\\VBoxUSBMon.sys");
            RTPrintf("Creating USB monitor driver service with path %s ...\n", szDriver);
            SC_HANDLE hService = CreateService(hSMgrCreate,
                                               SERVICE_NAME,
                                               "VBox USB Monitor Driver",
                                               SERVICE_QUERY_STATUS,
                                               SERVICE_KERNEL_DRIVER,
                                               SERVICE_DEMAND_START,
                                               SERVICE_ERROR_NORMAL,
                                               szDriver,
                                               NULL, NULL, NULL, NULL, NULL);
            dwLastError = GetLastError();
            if (dwLastError == ERROR_SERVICE_EXISTS)
            {
                RTPrintf("USB monitor driver service already exists, skipping creation.\n");
                rc = usblibOsChangeService(szDriver);
            }
            else
            {
                AssertMsg(hService, ("CreateService failed! LastError=%Rwa, szDriver=%s\n", dwLastError, szDriver));
                rc = RTErrConvertFromWin32(dwLastError);
                if (hService != NULL)
                    CloseServiceHandle(hService);
            }
        }
        CloseServiceHandle(hSMgrCreate);
    }
    return rc;
}
