/* $Id: SUPLib-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Library - Windows NT specific parts.
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
#define LOG_GROUP LOG_GROUP_SUP
#ifdef IN_SUP_HARDENED_R3
# undef DEBUG /* Warning: disables RT_STRICT */
# undef LOG_DISABLED
# define LOG_DISABLED
  /** @todo RTLOGREL_DISABLED */
# include <iprt/log.h>
# undef LogRelIt
# define LogRelIt(pvInst, fFlags, iGroup, fmtargs) do { } while (0)
#endif

#define USE_NT_DEVICE_IO_CONTROL_FILE
#include <iprt/nt/nt-and-windows.h>

#include <VBox/sup.h>
#include <VBox/types.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#ifndef IN_SUP_HARDENED_R3
# include <iprt/env.h>
# include <iprt/x86.h>
# include <iprt/ldr.h>
#endif
#include <iprt/path.h>
#include <iprt/string.h>
#include "../SUPLibInternal.h"
#include "../SUPDrvIOC.h"
#ifdef VBOX_WITH_HARDENING
# include "win/SUPHardenedVerify-win.h"
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The support service name. */
#define SERVICE_NAME    "VBoxSup"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifndef IN_SUP_HARDENED_R3
static int suplibOsCreateService(void);
//unused: static int suplibOsUpdateService(void);
static int suplibOsDeleteService(void);
static int suplibOsStartService(void);
static int suplibOsStopService(void);
#endif
#ifdef USE_NT_DEVICE_IO_CONTROL_FILE
static int suplibConvertNtStatus(NTSTATUS rcNt);
#else
static int suplibConvertWin32Err(int);
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static bool g_fHardenedVerifyInited = false;


DECLHIDDEN(int) suplibOsHardenedVerifyInit(void)
{
    if (!g_fHardenedVerifyInited)
    {
#if defined(VBOX_WITH_HARDENING) && !defined(IN_SUP_HARDENED_R3) && !defined(IN_SUP_R3_STATIC)
        supR3HardenedWinInitVersion(false /*fEarly*/);
        int rc = supHardenedWinInitImageVerifier(NULL);
        if (RT_FAILURE(rc))
            return rc;
        supR3HardenedWinResolveVerifyTrustApiAndHookThreadCreation(NULL);
#endif
        g_fHardenedVerifyInited = true;
    }
    return VINF_SUCCESS;
}


DECLHIDDEN(int) suplibOsHardenedVerifyTerm(void)
{
    /** @todo free resources...  */
    return VINF_SUCCESS;
}


DECLHIDDEN(int) suplibOsInit(PSUPLIBDATA pThis, bool fPreInited, uint32_t fFlags, SUPINITOP *penmWhat, PRTERRINFO pErrInfo)
{
    /*
     * Make sure the image verifier is fully initialized.
     */
    int rc = suplibOsHardenedVerifyInit();
    if (RT_FAILURE(rc))
        return RTErrInfoSetF(pErrInfo, rc, "suplibOsHardenedVerifyInit failed: %Rrc", rc);

    /*
     * Done if of pre-inited.
     */
    if (fPreInited)
    {
#if defined(VBOX_WITH_HARDENING) && !defined(IN_SUP_HARDENED_R3)
# ifdef IN_SUP_R3_STATIC
        return VERR_NOT_SUPPORTED;
# else
        return VINF_SUCCESS;
# endif
#else
        return VINF_SUCCESS;
#endif
    }

#if !defined(IN_SUP_HARDENED_R3)
    /*
     * Driverless?
     */
    if (fFlags & SUPR3INIT_F_DRIVERLESS)
    {
        pThis->fDriverless = true;
        return VINF_SUCCESS;
    }
#endif

    /*
     * Try open the device.
     */
#ifndef IN_SUP_HARDENED_R3
    uint32_t cTry = 0;
#endif
    HANDLE hDevice;
    for (;;)
    {
        IO_STATUS_BLOCK     Ios   = RTNT_IO_STATUS_BLOCK_INITIALIZER;

        static const WCHAR  s_wszName[] = L"\\Device\\VBoxDrvU";
        UNICODE_STRING      NtName;
        NtName.Buffer        = (PWSTR)s_wszName;
        NtName.Length        = sizeof(s_wszName) - sizeof(WCHAR) * (fFlags & SUPR3INIT_F_UNRESTRICTED ? 2 : 1);
        NtName.MaximumLength = NtName.Length;

        OBJECT_ATTRIBUTES   ObjAttr;
        InitializeObjectAttributes(&ObjAttr, &NtName, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);

        hDevice = RTNT_INVALID_HANDLE_VALUE;

        NTSTATUS rcNt = NtCreateFile(&hDevice,
                                     GENERIC_READ | GENERIC_WRITE, /* No SYNCHRONIZE. */
                                     &ObjAttr,
                                     &Ios,
                                     NULL /* Allocation Size*/,
                                     FILE_ATTRIBUTE_NORMAL,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     FILE_OPEN,
                                     FILE_NON_DIRECTORY_FILE, /* No FILE_SYNCHRONOUS_IO_NONALERT! */
                                     NULL /*EaBuffer*/,
                                     0 /*EaLength*/);
        if (NT_SUCCESS(rcNt))
            rcNt = Ios.Status;
        if (NT_SUCCESS(rcNt))
        {
            /*
             * We're good.
             */
            pThis->hDevice       = hDevice;
            pThis->fUnrestricted = RT_BOOL(fFlags & SUPR3INIT_F_UNRESTRICTED);
            return VINF_SUCCESS;
        }

#ifndef IN_SUP_HARDENED_R3
        /*
         * Failed to open, try starting the service and reopen the device
         * exactly once.
         */
        if (cTry == 0 && !NT_SUCCESS(rcNt))
        {
            cTry++;
            suplibOsStartService();
            continue;
        }
#endif

        /*
         * Translate the error code.
         */
        switch (rcNt)
        {
            /** @todo someone must test what is actually returned. */
            case STATUS_DEVICE_DOES_NOT_EXIST:
            case STATUS_DEVICE_NOT_CONNECTED:
            //case ERROR_BAD_DEVICE:
            case STATUS_DEVICE_REMOVED:
            //case ERROR_DEVICE_NOT_AVAILABLE:
                rc = VERR_VM_DRIVER_LOAD_ERROR;
                break;
            case STATUS_OBJECT_PATH_NOT_FOUND:
            case STATUS_NO_SUCH_DEVICE:
            case STATUS_NO_SUCH_FILE:
            case STATUS_OBJECT_NAME_NOT_FOUND:
                rc = VERR_VM_DRIVER_NOT_INSTALLED;
                break;
            case STATUS_ACCESS_DENIED:
            case STATUS_SHARING_VIOLATION:
                rc = VERR_VM_DRIVER_NOT_ACCESSIBLE;
                break;
            case STATUS_UNSUCCESSFUL:
                rc = VERR_SUPLIB_NT_PROCESS_UNTRUSTED_0;
                break;
            case STATUS_TRUST_FAILURE:
                rc = VERR_SUPLIB_NT_PROCESS_UNTRUSTED_1;
                break;
            case STATUS_TOO_LATE:
                rc = VERR_SUPDRV_HARDENING_EVIL_HANDLE;
                break;
            default:
                if (SUP_NT_STATUS_IS_VBOX(rcNt)) /* See VBoxDrvNtErr2NtStatus. */
                    rc = SUP_NT_STATUS_TO_VBOX(rcNt);
                else
                    rc = VERR_VM_DRIVER_OPEN_ERROR;
                break;
        }

#ifdef IN_SUP_HARDENED_R3
        /*
         * Get more details from VBoxDrvErrorInfo if present.
         */
        if (pErrInfo && pErrInfo->cbMsg > 32)
        {
            /* Prefix. */
            size_t cchPrefix;
            if (RTErrIsKnown(rc))
                cchPrefix = RTStrPrintf(pErrInfo->pszMsg, pErrInfo->cbMsg / 2, "Integrity error (%#x/%Rrc): ", rcNt, rc);
            else
                cchPrefix = RTStrPrintf(pErrInfo->pszMsg, pErrInfo->cbMsg / 2, "Integrity error (%#x/%d): ", rcNt, rc);

            /* Get error info. */
            supR3HardenedWinReadErrorInfoDevice(pErrInfo->pszMsg + cchPrefix, pErrInfo->cbMsg - cchPrefix, "");
            if (pErrInfo->pszMsg[cchPrefix] != '\0')
            {
                pErrInfo->fFlags |= RTERRINFO_FLAGS_SET;
                pErrInfo->rc      = rc;
                *penmWhat = kSupInitOp_Integrity;
            }
            else
                pErrInfo->pszMsg[0] = '\0';
        }

#else
        RT_NOREF1(penmWhat);

        /*
         * Do fallback.
         */
        if (   (fFlags & SUPR3INIT_F_DRIVERLESS_MASK)
            && rcNt != -1 /** @todo */ )
        {
            LogRel(("Failed to open '%.*ls' rc=%Rrc rcNt=%#x - Switching to driverless mode.\n",
                    NtName.Length / sizeof(WCHAR), NtName.Buffer, rc, rcNt));
            pThis->fDriverless = true;
            return VINF_SUCCESS;
        }
#endif
        return rc;
    }
}


#ifndef IN_SUP_HARDENED_R3

DECLHIDDEN(int) suplibOsInstall(void)
{
    int rc = suplibOsCreateService();
    if (RT_SUCCESS(rc))
    {
        int rc2 = suplibOsStartService();
        if (rc2 != VINF_SUCCESS)
            rc = rc2;
    }
    return rc;
}


DECLHIDDEN(int) suplibOsUninstall(void)
{
    int rc = suplibOsStopService();
    if (RT_SUCCESS(rc))
        rc = suplibOsDeleteService();
    return rc;
}


/**
 * Creates the service.
 *
 * @returns VBox status code.
 * @retval  VWRN_ALREADY_EXISTS if it already exists.
 */
static int suplibOsCreateService(void)
{
    /*
     * Assume it didn't exist, so we'll create the service.
     */
    int        rc;
    SC_HANDLE hSMgrCreate = OpenSCManager(NULL, NULL, SERVICE_CHANGE_CONFIG);
    DWORD     dwErr = GetLastError();
    AssertMsg(hSMgrCreate, ("OpenSCManager(,,create) failed dwErr=%d\n", dwErr));
    if (hSMgrCreate != NULL)
    {
        char szDriver[RTPATH_MAX];
        rc = RTPathExecDir(szDriver, sizeof(szDriver) - sizeof("\\VBoxSup.sys"));
        if (RT_SUCCESS(rc))
        {
            strcat(szDriver, "\\VBoxSup.sys");
            SC_HANDLE hService = CreateService(hSMgrCreate,
                                               SERVICE_NAME,
                                               "VBox Support Driver",
                                               SERVICE_QUERY_STATUS,
                                               SERVICE_KERNEL_DRIVER,
                                               SERVICE_DEMAND_START,
                                               SERVICE_ERROR_NORMAL,
                                               szDriver,
                                               NULL, NULL, NULL, NULL, NULL);
            dwErr = GetLastError();
            if (hService)
            {
                CloseServiceHandle(hService);
                rc = VINF_SUCCESS;
            }
            else if (dwErr == ERROR_SERVICE_EXISTS)
                rc = VWRN_ALREADY_EXISTS;
            else
            {
                AssertMsgFailed(("CreateService failed! dwErr=%Rwa szDriver=%s\n", dwErr, szDriver));
                rc = RTErrConvertFromWin32(dwErr);
            }
        }
        CloseServiceHandle(hSMgrCreate);
    }
    else
        rc = RTErrConvertFromWin32(GetLastError());
    return rc;
}


/**
 * Stops a possibly running service.
 *
 * @returns VBox status code.
 */
static int suplibOsStopService(void)
{
    /*
     * Assume it didn't exist, so we'll create the service.
     */
    int         rc;
    SC_HANDLE   hSMgr = OpenSCManager(NULL, NULL, SERVICE_STOP | SERVICE_QUERY_STATUS);
    DWORD       dwErr = GetLastError();
    AssertMsg(hSMgr, ("OpenSCManager(,,delete) failed dwErr=%d\n", dwErr));
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
                rc = VINF_SUCCESS;
            else if (ControlService(hService, SERVICE_CONTROL_STOP, &Status))
            {
                int iWait = 100;
                while (Status.dwCurrentState == SERVICE_STOP_PENDING && iWait-- > 0)
                {
                    Sleep(100);
                    QueryServiceStatus(hService, &Status);
                }
                if (Status.dwCurrentState == SERVICE_STOPPED)
                    rc = VINF_SUCCESS;
                else
                {
                    AssertMsgFailed(("Failed to stop service. status=%d\n", Status.dwCurrentState));
                    rc = VERR_GENERAL_FAILURE;
                }
            }
            else
            {
                dwErr = GetLastError();
                if (   Status.dwCurrentState == SERVICE_STOP_PENDING
                    && dwErr == ERROR_SERVICE_CANNOT_ACCEPT_CTRL)
                    rc = VERR_RESOURCE_BUSY;    /* better than VERR_GENERAL_FAILURE */
                else
                {
                    AssertMsgFailed(("ControlService failed with dwErr=%Rwa. status=%d\n", dwErr, Status.dwCurrentState));
                    rc = RTErrConvertFromWin32(dwErr);
                }
            }
            CloseServiceHandle(hService);
        }
        else
        {
            dwErr = GetLastError();
            if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST)
                rc = VINF_SUCCESS;
            else
            {
                AssertMsgFailed(("OpenService failed dwErr=%Rwa\n", dwErr));
                rc = RTErrConvertFromWin32(dwErr);
            }
        }
        CloseServiceHandle(hSMgr);
    }
    else
        rc = RTErrConvertFromWin32(dwErr);
    return rc;
}


/**
 * Deletes the service.
 *
 * @returns VBox status code.
 */
int suplibOsDeleteService(void)
{
    int         rcRet = VINF_SUCCESS;
    SC_HANDLE   hSMgr = OpenSCManager(NULL, NULL, SERVICE_CHANGE_CONFIG);
    DWORD       dwErr = GetLastError();
    AssertMsg(hSMgr, ("OpenSCManager(,,delete) failed rc=%d\n", dwErr));
    if (hSMgr)
    {
        /*
         * Old service name.
         */
        SC_HANDLE hService = OpenService(hSMgr, "VBoxDrv", DELETE);
        if (hService)
        {
            if (!DeleteService(hService))
            {
                dwErr = GetLastError();
                AssertMsgFailed(("DeleteService failed for VBoxDrv dwErr=%Rwa\n", dwErr));
                rcRet = RTErrConvertFromWin32(dwErr);
            }
            CloseServiceHandle(hService);
        }
        else
        {
            dwErr = GetLastError();
            if (dwErr != ERROR_SERVICE_DOES_NOT_EXIST)
            {
                AssertMsgFailed(("OpenService failed for VBoxDrv dwErr=%Rwa\n", dwErr));
                rcRet = RTErrConvertFromWin32(dwErr);
            }
        }

        /*
         * The new service.
         */
        hService = OpenService(hSMgr, SERVICE_NAME, DELETE);
        if (hService)
        {
            if (!DeleteService(hService))
            {
                dwErr = GetLastError();
                AssertMsgFailed(("DeleteService for " SERVICE_NAME " failed dwErr=%Rwa\n", dwErr));
                rcRet = RTErrConvertFromWin32(dwErr);
            }
            CloseServiceHandle(hService);
        }
        else
        {
            dwErr = GetLastError();
            if (dwErr != ERROR_SERVICE_DOES_NOT_EXIST)
            {
                AssertMsgFailed(("OpenService failed for " SERVICE_NAME " dwErr=%Rwa\n", dwErr));
                rcRet = RTErrConvertFromWin32(dwErr);
            }
        }
        CloseServiceHandle(hSMgr);
    }
    else
        rcRet = RTErrConvertFromWin32(dwErr);
    return rcRet;
}

#if 0
/**
 * Creates the service.
 *
 * @returns 0 on success.
 * @returns -1 on failure.
 */
static int suplibOsUpdateService(void)
{
    /*
     * Assume it didn't exist, so we'll create the service.
     */
    SC_HANDLE   hSMgr = OpenSCManager(NULL, NULL, SERVICE_CHANGE_CONFIG);
    DWORD LastError = GetLastError(); NOREF(LastError);
    AssertMsg(hSMgr, ("OpenSCManager(,,delete) failed LastError=%Rwa\n", LastError));
    if (hSMgr)
    {
        SC_HANDLE hService = OpenService(hSMgr, SERVICE_NAME, SERVICE_CHANGE_CONFIG);
        if (hService)
        {
            char szDriver[RTPATH_MAX];
            int rc = RTPathExecDir(szDriver, sizeof(szDriver) - sizeof("\\VBoxSup.sys"));
            if (RT_SUCCESS(rc))
            {
                strcat(szDriver, "\\VBoxSup.sys");

                SC_LOCK hLock = LockServiceDatabase(hSMgr);
                if (ChangeServiceConfig(hService,
                                        SERVICE_KERNEL_DRIVER,
                                        SERVICE_DEMAND_START,
                                        SERVICE_ERROR_NORMAL,
                                        szDriver,
                                        NULL, NULL, NULL, NULL, NULL, NULL))
                {

                    UnlockServiceDatabase(hLock);
                    CloseServiceHandle(hService);
                    CloseServiceHandle(hSMgr);
                    return 0;
                }
                else
                {
                    DWORD LastError = GetLastError(); NOREF(LastError);
                    AssertMsgFailed(("ChangeServiceConfig failed LastError=%Rwa\n", LastError));
                }
            }
            UnlockServiceDatabase(hLock);
            CloseServiceHandle(hService);
        }
        else
        {
            DWORD LastError = GetLastError(); NOREF(LastError);
            AssertMsgFailed(("OpenService failed LastError=%Rwa\n", LastError));
        }
        CloseServiceHandle(hSMgr);
    }
    return -1;
}
#endif


/**
 * Attempts to start the service, creating it if necessary.
 *
 * @returns VBox status code.
 */
static int suplibOsStartService(void)
{
    /*
     * Check if the driver service is there.
     */
    SC_HANDLE hSMgr = OpenSCManager(NULL, NULL, SERVICE_QUERY_STATUS | SERVICE_START);
    if (hSMgr == NULL)
    {
        DWORD dwErr = GetLastError();
        AssertMsgFailed(("couldn't open service manager in SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS mode! (dwErr=%d)\n", dwErr));
        return RTErrConvertFromWin32(dwErr);
    }

    /*
     * Try open our service to check it's status.
     */
    SC_HANDLE hService = OpenService(hSMgr, SERVICE_NAME, SERVICE_QUERY_STATUS | SERVICE_START);
    if (!hService)
    {
        /*
         * Create the service.
         */
        int rc = suplibOsCreateService();
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Try open the service.
         */
        hService = OpenService(hSMgr, SERVICE_NAME, SERVICE_QUERY_STATUS | SERVICE_START);
    }

    /*
     * Check if open and on demand create succeeded.
     */
    int rc;
    if (hService)
    {

        /*
         * Query service status to see if we need to start it or not.
         */
        SERVICE_STATUS Status;
        BOOL fRc = QueryServiceStatus(hService, &Status);
        Assert(fRc); NOREF(fRc);
        if (Status.dwCurrentState == SERVICE_RUNNING)
            rc = VINF_ALREADY_INITIALIZED;
        else
        {
            if (Status.dwCurrentState == SERVICE_START_PENDING)
                rc = VINF_SUCCESS;
            else
            {
                /*
                 * Start it.
                 */
                if (StartService(hService, 0, NULL))
                    rc = VINF_SUCCESS;
                else
                {
                    DWORD dwErr = GetLastError();
                    AssertMsg(fRc, ("StartService failed with dwErr=%Rwa\n", dwErr));
                    rc = RTErrConvertFromWin32(dwErr);
                }
            }

            /*
             * Wait for the service to finish starting.
             * We'll wait for 10 seconds then we'll give up.
             */
            QueryServiceStatus(hService, &Status);
            if (Status.dwCurrentState == SERVICE_START_PENDING)
            {
                int iWait;
                for (iWait = 100; iWait > 0 && Status.dwCurrentState == SERVICE_START_PENDING; iWait--)
                {
                    Sleep(100);
                    QueryServiceStatus(hService, &Status);
                }
                DWORD dwErr = GetLastError(); NOREF(dwErr);
                AssertMsg(Status.dwCurrentState != SERVICE_RUNNING,
                          ("Failed to start. dwErr=%Rwa iWait=%d status=%d\n", dwErr, iWait, Status.dwCurrentState));
            }

            if (Status.dwCurrentState == SERVICE_RUNNING)
                rc = VINF_SUCCESS;
            else if (RT_SUCCESS_NP(rc))
                rc = VERR_GENERAL_FAILURE;
        }

        /*
         * Close open handles.
         */
        CloseServiceHandle(hService);
    }
    else
    {
        DWORD dwErr = GetLastError();
        AssertMsgFailed(("OpenService failed! LastError=%Rwa\n", dwErr));
        rc = RTErrConvertFromWin32(dwErr);
    }
    if (!CloseServiceHandle(hSMgr))
        AssertFailed();

    return rc;
}

#endif /* !IN_SUP_HARDENED_R3 */

DECLHIDDEN(int) suplibOsTerm(PSUPLIBDATA pThis)
{
    /*
     * Check if we're inited at all.
     */
    if (pThis->hDevice != NULL)
    {
        NTSTATUS rcNt = NtClose((HANDLE)pThis->hDevice);
        Assert(NT_SUCCESS(rcNt)); RT_NOREF(rcNt);
        pThis->hDevice = NIL_RTFILE; /* yes, that's right */
    }

    return VINF_SUCCESS;
}

#ifndef IN_SUP_HARDENED_R3

DECLHIDDEN(int) suplibOsIOCtl(PSUPLIBDATA pThis, uintptr_t uFunction, void *pvReq, size_t cbReq)
{
    RT_NOREF1(cbReq);

    /*
     * Issue the device I/O control.
     */
    PSUPREQHDR pHdr = (PSUPREQHDR)pvReq;
    Assert(cbReq == RT_MAX(pHdr->cbIn, pHdr->cbOut));
# ifdef USE_NT_DEVICE_IO_CONTROL_FILE
    IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    NTSTATUS rcNt = NtDeviceIoControlFile((HANDLE)pThis->hDevice, NULL /*hEvent*/, NULL /*pfnApc*/, NULL /*pvApcCtx*/, &Ios,
                                          (ULONG)uFunction,
                                          pvReq /*pvInput */, pHdr->cbIn /* cbInput */,
                                          pvReq /*pvOutput*/, pHdr->cbOut /* cbOutput */);
    if (NT_SUCCESS(rcNt))
    {
        if (NT_SUCCESS(Ios.Status))
            return VINF_SUCCESS;
        rcNt = Ios.Status;
    }
    return suplibConvertNtStatus(rcNt);

# else
    DWORD cbReturned = (ULONG)pHdr->cbOut;
    if (DeviceIoControl((HANDLE)pThis->hDevice, uFunction, pvReq, pHdr->cbIn, pvReq, cbReturned, &cbReturned, NULL))
        return 0;
    return suplibConvertWin32Err(GetLastError());
# endif
}


DECLHIDDEN(int) suplibOsIOCtlFast(PSUPLIBDATA pThis, uintptr_t uFunction, uintptr_t idCpu)
{
    /*
     * Issue device I/O control.
     */
# ifdef USE_NT_DEVICE_IO_CONTROL_FILE
    IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    NTSTATUS rcNt = NtDeviceIoControlFile((HANDLE)pThis->hDevice, NULL /*hEvent*/, NULL /*pfnApc*/, NULL /*pvApcCtx*/, &Ios,
                                          (ULONG)uFunction,
                                          NULL /*pvInput */, 0 /* cbInput */,
                                          (PVOID)idCpu /*pvOutput*/, 0 /* cbOutput */);
    if (NT_SUCCESS(rcNt))
    {
        if (NT_SUCCESS(Ios.Status))
            return VINF_SUCCESS;
        rcNt = Ios.Status;
    }
    return suplibConvertNtStatus(rcNt);
# else
    DWORD cbReturned = 0;
    if (DeviceIoControl((HANDLE)pThis->hDevice, uFunction, NULL, 0, (LPVOID)idCpu, 0, &cbReturned, NULL))
        return VINF_SUCCESS;
    return suplibConvertWin32Err(GetLastError());
# endif
}


DECLHIDDEN(int) suplibOsPageAlloc(PSUPLIBDATA pThis, size_t cPages, uint32_t fFlags, void **ppvPages)
{
    RT_NOREF(pThis, fFlags);

    /*
     * Do some one-time init here wrt large pages.
     *
     * Large pages requires the SeLockMemoryPrivilege, which by default (Win10,
     * Win11) isn't even enabled and must be gpedit'ed to be adjustable here.
     */
    if (!(cPages & 511) && (fFlags & SUP_PAGE_ALLOC_F_LARGE_PAGES))
    {
        static int volatile s_fCanDoLargePages = -1;
        int fCanDoLargePages = s_fCanDoLargePages;
        if (s_fCanDoLargePages != -1)
        { /* likely */ }
        else if (RTEnvExistsUtf8("VBOX_DO_NOT_USE_LARGE_PAGES")) /** @todo add flag for this instead? */
            s_fCanDoLargePages = fCanDoLargePages = 0;
        else
        {
            HANDLE hToken = NULL;
            if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hToken))
            {
                union
                {
                    TOKEN_PRIVILEGES s;
                    uint8_t abPadding[RT_UOFFSETOF(TOKEN_PRIVILEGES, Privileges) + sizeof(LUID_AND_ATTRIBUTES)];
                } Privileges;
                RT_ZERO(Privileges);
                Privileges.s.PrivilegeCount = 1;
                Privileges.s.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                if (LookupPrivilegeValueW(NULL, L"SeLockMemoryPrivilege", &Privileges.s.Privileges[0].Luid))
                    AdjustTokenPrivileges(hToken, FALSE, &Privileges.s, 0, NULL, NULL);
                else
                    AssertFailed();
                CloseHandle(hToken);
            }
            else
                AssertFailed();
            s_fCanDoLargePages = fCanDoLargePages = -2;
        }

        /*
         * Try allocate with large pages.
         */
        if (fCanDoLargePages != 0)
        {
            *ppvPages = VirtualAlloc(NULL, (size_t)cPages << PAGE_SHIFT, MEM_COMMIT | MEM_LARGE_PAGES, PAGE_EXECUTE_READWRITE);
            if (*ppvPages)
            {
                if (fCanDoLargePages == -2)
                {
                    s_fCanDoLargePages = 1;
                    LogRel(("SUPLib: MEM_LARGE_PAGES works!\n"));
                }
                LogRel2(("SUPLib: MEM_LARGE_PAGES for %p LB %p\n", *ppvPages, (size_t)cPages << PAGE_SHIFT));
                return VINF_SUCCESS;
            }

            /* This can happen if the above AdjustTokenPrivileges failed (non-admin
               user), or if the privilege isn't present in the token (need gpedit). */
            if (GetLastError() == ERROR_PRIVILEGE_NOT_HELD)
            {
                LogRel(("SUPLib: MEM_LARGE_PAGES privilege not held.\n"));
                s_fCanDoLargePages = 0;
            }
            else
                LogRel2(("SUPLib: MEM_LARGE_PAGES allocation failed with odd status: %u\n", GetLastError()));
        }
    }

    /*
     * Do a regular allocation w/o large pages.
     */
    *ppvPages = VirtualAlloc(NULL, (size_t)cPages << PAGE_SHIFT, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (*ppvPages)
        return VINF_SUCCESS;
    return RTErrConvertFromWin32(GetLastError());
}


DECLHIDDEN(int) suplibOsPageFree(PSUPLIBDATA pThis, void *pvPages, size_t /* cPages */)
{
    NOREF(pThis);
    if (VirtualFree(pvPages, 0, MEM_RELEASE))
        return VINF_SUCCESS;
    return RTErrConvertFromWin32(GetLastError());
}


DECLHIDDEN(bool) suplibOsIsNemSupportedWhenNoVtxOrAmdV(void)
{
# if ARCH_BITS == 64
#  if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    /*
     * Check that we're in a VM.
     */
    if (!ASMHasCpuId())
        return false;
    if (!RTX86IsValidStdRange(ASMCpuId_EAX(0)))
        return false;
    if (!(ASMCpuId_ECX(1) & X86_CPUID_FEATURE_ECX_HVP))
        return false;
#  endif

    /*
     * Try load WinHvPlatform and resolve API for checking.
     * Note! The two size_t arguments and the ssize_t one are all too big, but who cares.
     */
    RTLDRMOD hLdrMod = NIL_RTLDRMOD;
    int rc = RTLdrLoadSystem("WinHvPlatform.dll", false, &hLdrMod);
    if (RT_FAILURE(rc))
        return false;

    bool fRet = false;
    typedef HRESULT (WINAPI *PFNWHVGETCAPABILITY)(ssize_t, void *, size_t, size_t *);
    PFNWHVGETCAPABILITY pfnWHvGetCapability = (PFNWHVGETCAPABILITY)RTLdrGetFunction(hLdrMod, "WHvGetCapability");
    if (pfnWHvGetCapability)
    {
        /*
         * Query the API.
         */
        union
        {
            BOOL fHypervisorPresent;
            uint64_t u64Padding;
        } Caps;
        RT_ZERO(Caps);
        size_t cbRetIgn = 0;
        HRESULT hrc = pfnWHvGetCapability(0 /*WHvCapabilityCodeHypervisorPresent*/, &Caps, sizeof(Caps), &cbRetIgn);
        if (SUCCEEDED(hrc) && Caps.fHypervisorPresent)
            fRet = true;
    }

    RTLdrClose(hLdrMod);
    return fRet;
# else
    return false;
#endif
}


# ifndef USE_NT_DEVICE_IO_CONTROL_FILE
/**
 * Converts a supdrv win32 error code to an IPRT status code.
 *
 * @returns corresponding IPRT error code.
 * @param   rc  Win32 error code.
 */
static int suplibConvertWin32Err(int rc)
{
    /* Conversion program (link with ntdll.lib from ddk):
        #define _WIN32_WINNT 0x0501
        #include <iprt/win/windows.h>
        #include <ntstatus.h>
        #include <winternl.h>
        #include <stdio.h>

        int main()
        {
            #define CONVERT(a)  printf(#a " %#x -> %d\n", a, RtlNtStatusToDosError((a)))
            CONVERT(STATUS_SUCCESS);
            CONVERT(STATUS_NOT_SUPPORTED);
            CONVERT(STATUS_INVALID_PARAMETER);
            CONVERT(STATUS_UNKNOWN_REVISION);
            CONVERT(STATUS_INVALID_HANDLE);
            CONVERT(STATUS_INVALID_ADDRESS);
            CONVERT(STATUS_NOT_LOCKED);
            CONVERT(STATUS_IMAGE_ALREADY_LOADED);
            CONVERT(STATUS_ACCESS_DENIED);
            CONVERT(STATUS_REVISION_MISMATCH);

            return 0;
        }
     */

    switch (rc)
    {
        //case 0:                             return STATUS_SUCCESS;
        case 0:                             return VINF_SUCCESS;
        case ERROR_NOT_SUPPORTED:           return VERR_GENERAL_FAILURE;
        case ERROR_INVALID_PARAMETER:       return VERR_INVALID_PARAMETER;
        case ERROR_UNKNOWN_REVISION:        return VERR_INVALID_MAGIC;
        case ERROR_INVALID_HANDLE:          return VERR_INVALID_HANDLE;
        case ERROR_UNEXP_NET_ERR:           return VERR_INVALID_POINTER;
        case ERROR_NOT_LOCKED:              return VERR_LOCK_FAILED;
        case ERROR_SERVICE_ALREADY_RUNNING: return VERR_ALREADY_LOADED;
        case ERROR_ACCESS_DENIED:           return VERR_PERMISSION_DENIED;
        case ERROR_REVISION_MISMATCH:       return VERR_VERSION_MISMATCH;
    }

    /* fall back on the default conversion. */
    return RTErrConvertFromWin32(rc);
}
# else
/**
 * Reverse of VBoxDrvNtErr2NtStatus
 * returns VBox status code.
 * @param   rcNt    NT status code.
 */
static int suplibConvertNtStatus(NTSTATUS rcNt)
{
    switch (rcNt)
    {
        case STATUS_SUCCESS:                    return VINF_SUCCESS;
        case STATUS_NOT_SUPPORTED:              return VERR_GENERAL_FAILURE;
        case STATUS_INVALID_PARAMETER:          return VERR_INVALID_PARAMETER;
        case STATUS_UNKNOWN_REVISION:           return VERR_INVALID_MAGIC;
        case STATUS_INVALID_HANDLE:             return VERR_INVALID_HANDLE;
        case STATUS_INVALID_ADDRESS:            return VERR_INVALID_POINTER;
        case STATUS_NOT_LOCKED:                 return VERR_LOCK_FAILED;
        case STATUS_IMAGE_ALREADY_LOADED:       return VERR_ALREADY_LOADED;
        case STATUS_ACCESS_DENIED:              return VERR_PERMISSION_DENIED;
        case STATUS_REVISION_MISMATCH:          return VERR_VERSION_MISMATCH;
    }

    /* See VBoxDrvNtErr2NtStatus. */
    if (SUP_NT_STATUS_IS_VBOX(rcNt))
        return SUP_NT_STATUS_TO_VBOX(rcNt);

    /* Fall back on IPRT for the rest. */
    return RTErrConvertFromNtStatus(rcNt);
}
# endif

#endif /* !IN_SUP_HARDENED_R3 */

