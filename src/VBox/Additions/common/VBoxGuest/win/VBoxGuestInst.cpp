/* $Id: VBoxGuestInst.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Small tool to (un)install the VBoxGuest device driver (for testing).
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

#include <VBox/VBoxGuest.h> /* for VBOXGUEST_SERVICE_NAME */
#include <iprt/errcore.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/utf16.h>




static RTEXITCODE installDriver(bool fStartIt)
{
    /*
     * Assume it didn't exist, so we'll create the service.
     */
    SC_HANDLE   hSMgrCreate = OpenSCManagerW(NULL, NULL, SERVICE_CHANGE_CONFIG);
    if (!hSMgrCreate)
        return RTMsgErrorExitFailure("OpenSCManager(,,create) failed: %u", GetLastError());

    const wchar_t  *pwszSlashName = L"\\VBoxGuest.sys";
    wchar_t         wszDriver[MAX_PATH * 2];
    GetCurrentDirectoryW(MAX_PATH, wszDriver);
    RTUtf16Cat(wszDriver, RT_ELEMENTS(wszDriver), pwszSlashName);
    if (GetFileAttributesW(wszDriver) == INVALID_FILE_ATTRIBUTES)
    {
        GetSystemDirectoryW(wszDriver, MAX_PATH);
        RTUtf16Cat(wszDriver, RT_ELEMENTS(wszDriver), L"\\drivers");
        RTUtf16Cat(wszDriver, RT_ELEMENTS(wszDriver), pwszSlashName);

        /* Try FAT name abbreviation. */
        if (GetFileAttributesW(wszDriver) == INVALID_FILE_ATTRIBUTES)
        {
            pwszSlashName = L"\\VBoxGst.sys";
            GetCurrentDirectoryW(MAX_PATH, wszDriver);
            RTUtf16Cat(wszDriver, RT_ELEMENTS(wszDriver), pwszSlashName);
            if (GetFileAttributesW(wszDriver) == INVALID_FILE_ATTRIBUTES)
            {
                GetSystemDirectoryW(wszDriver, MAX_PATH);
                RTUtf16Cat(wszDriver, RT_ELEMENTS(wszDriver), L"\\drivers");
                RTUtf16Cat(wszDriver, RT_ELEMENTS(wszDriver), pwszSlashName);
            }
        }
    }

    RTEXITCODE rcExit;
    SC_HANDLE hService = CreateServiceW(hSMgrCreate,
                                        RT_CONCAT(L,VBOXGUEST_SERVICE_NAME),
                                        L"VBoxGuest Support Driver",
                                        SERVICE_QUERY_STATUS | (fStartIt ? SERVICE_START : 0),
                                        SERVICE_KERNEL_DRIVER,
                                        SERVICE_BOOT_START,
                                        SERVICE_ERROR_NORMAL,
                                        wszDriver,
                                        L"System",
                                        NULL, NULL, NULL, NULL);
    if (hService)
    {
        RTMsgInfo("Successfully created service '%s' for driver '%ls'.\n", VBOXGUEST_SERVICE_NAME, wszDriver);
        rcExit = RTEXITCODE_SUCCESS;
        if (fStartIt)
        {
            if (StartService(hService, 0, NULL))
                RTMsgInfo("successfully started driver '%ls'\n", wszDriver);
            else
                rcExit = RTMsgErrorExitFailure("StartService failed: %u", GetLastError());
        }
        CloseServiceHandle(hService);
    }
    else
        rcExit = RTMsgErrorExitFailure("CreateService failed! %u (wszDriver=%ls)\n", GetLastError(), wszDriver);
    CloseServiceHandle(hSMgrCreate);
    return rcExit;
}


static RTEXITCODE uninstallDriver(void)
{
    SC_HANDLE hSMgr = OpenSCManagerW(NULL, NULL, SERVICE_CHANGE_CONFIG);
    if (!hSMgr)
        return RTMsgErrorExitFailure("OpenSCManager(,,change_config) failed: %u", GetLastError());

    RTEXITCODE rcExit;
    SC_HANDLE hService = OpenServiceW(hSMgr, RT_CONCAT(L, VBOXGUEST_SERVICE_NAME), SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
    if (hService)
    {
        /*
         * Try stop it if it's running.
         */
        SERVICE_STATUS  Status = { 0, 0, 0, 0, 0, 0, 0 };
        QueryServiceStatus(hService, &Status);
        if (Status.dwCurrentState == SERVICE_STOPPED)
            rcExit = RTEXITCODE_SUCCESS;
        else if (ControlService(hService, SERVICE_CONTROL_STOP, &Status))
        {
            int iWait = 100;
            while (Status.dwCurrentState == SERVICE_STOP_PENDING && iWait-- > 0)
            {
                Sleep(100);
                QueryServiceStatus(hService, &Status);
            }
            if (Status.dwCurrentState == SERVICE_STOPPED)
                rcExit = RTEXITCODE_SUCCESS;
            else
                rcExit = RTMsgErrorExitFailure("Failed to stop service! Service status: %u (%#x)\n",
                                                Status.dwCurrentState, Status.dwCurrentState);
        }
        else
            rcExit = RTMsgErrorExitFailure("ControlService failed: %u, Service status: %u (%#x)",
                                            GetLastError(), Status.dwCurrentState, Status.dwCurrentState);

        /*
         * Delete the service.
         */
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            if (DeleteService(hService))
                RTMsgInfo("Successfully deleted the %s service\n", VBOXGUEST_SERVICE_NAME);
            else
                rcExit = RTMsgErrorExitFailure("DeleteService failed: %u", GetLastError());
        }

        CloseServiceHandle(hService);
    }
    else if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST)
    {
        RTMsgInfo("Nothing to do, the service %s does not exist.\n", VBOXGUEST_SERVICE_NAME);
        rcExit = RTEXITCODE_SUCCESS;
    }
    else
        rcExit = RTMsgErrorExitFailure("OpenService failed: %u", GetLastError());

    CloseServiceHandle(hSMgr);
    return rcExit;
}


static RTEXITCODE performTest(void)
{
    HANDLE hDevice = CreateFileW(RT_CONCAT(L,VBOXGUEST_DEVICE_NAME), // Win2k+: VBOXGUEST_DEVICE_NAME_GLOBAL
                                 GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL,
                                 OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL,
                                 NULL);
    if (hDevice != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hDevice);
        RTMsgInfo("Test succeeded\n");
        return RTEXITCODE_SUCCESS;
    }
    return RTMsgErrorExitFailure("Test failed! Unable to open driver (CreateFileW -> %u).", GetLastError());
}


static RTEXITCODE usage(const char *pszProgName)
{
    RTPrintf("\n"
             "Usage: %s [install|uninstall|test]\n", pszProgName);
    return RTEXITCODE_SYNTAX;
}


int main(int argc, char **argv)
{
    if (argc != 2)
    {
        RTMsgError(argc < 2 ? "Too few arguments! Expected one." : "Too many arguments! Expected only one.");
        return usage(argv[0]);
    }

    RTEXITCODE rcExit;
    if (strcmp(argv[1], "install") == 0)
        rcExit = installDriver(true);
    else if (strcmp(argv[1], "uninstall") == 0)
        rcExit = uninstallDriver();
    else if (strcmp(argv[1], "test") == 0)
        rcExit = performTest();
    else
    {
        RTMsgError("Unknown argument: '%s'", argv[1]);
        rcExit = usage(argv[0]);
    }
    return rcExit;
}

