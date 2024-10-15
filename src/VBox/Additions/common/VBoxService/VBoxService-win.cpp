/* $Id: VBoxService-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxService - Guest Additions Service Skeleton, Windows Specific Parts.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/ldr.h>
#include <iprt/system.h> /* For querying OS version. */

#define WIN32_NO_STATUS
#include <iprt/win/ws2tcpip.h>
#include <iprt/win/winsock2.h>
#undef WIN32_NO_STATUS
#include <iprt/nt/nt-and-windows.h>
#include <iprt/win/iphlpapi.h>
#include <aclapi.h>
#include <tlhelp32.h>
#define _NTDEF_
#include <Ntsecapi.h>

#include "VBoxServiceInternal.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void WINAPI vgsvcWinMain(DWORD argc, LPTSTR *argv);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static DWORD          g_dwWinServiceLastStatus = 0;
SERVICE_STATUS_HANDLE g_hWinServiceStatus = NULL;
/** The semaphore for the dummy Windows service. */
static RTSEMEVENT     g_WindowsEvent = NIL_RTSEMEVENT;

static SERVICE_TABLE_ENTRYA const g_aServiceTable[] =
{
    { (PSTR)VBOXSERVICE_NAME, vgsvcWinMain },
    { NULL,                   NULL}
};

/** @name APIs from ADVAPI32.DLL.
 * @{ */
decltype(RegisterServiceCtrlHandlerExA) *g_pfnRegisterServiceCtrlHandlerExA;    /**< W2K+ */
decltype(ChangeServiceConfig2A)         *g_pfnChangeServiceConfig2A;            /**< W2K+ */
decltype(GetNamedSecurityInfoA)         *g_pfnGetNamedSecurityInfoA;            /**< NT4+ */
decltype(SetEntriesInAclA)              *g_pfnSetEntriesInAclA;                 /**< NT4+ */
decltype(SetNamedSecurityInfoA)         *g_pfnSetNamedSecurityInfoA;            /**< NT4+ */
decltype(LsaNtStatusToWinError)         *g_pfnLsaNtStatusToWinError;            /**< NT3.51+ */
/** @} */

/** @name API from KERNEL32.DLL
 * @{ */
decltype(CreateToolhelp32Snapshot)      *g_pfnCreateToolhelp32Snapshot;         /**< W2K+, but Geoff says NT4. Hmm. */
decltype(Process32First)                *g_pfnProcess32First;                   /**< W2K+, but Geoff says NT4. Hmm. */
decltype(Process32Next)                 *g_pfnProcess32Next;                    /**< W2K+, but Geoff says NT4. Hmm. */
decltype(Module32First)                 *g_pfnModule32First;                    /**< W2K+, but Geoff says NT4. Hmm. */
decltype(Module32Next)                  *g_pfnModule32Next;                     /**< W2K+, but Geoff says NT4. Hmm. */
decltype(GetSystemTimeAdjustment)       *g_pfnGetSystemTimeAdjustment;          /**< NT 3.50+ */
decltype(SetSystemTimeAdjustment)       *g_pfnSetSystemTimeAdjustment;          /**< NT 3.50+ */
/** @} */

/** @name API from NTDLL.DLL
 * @{ */
decltype(ZwQuerySystemInformation)      *g_pfnZwQuerySystemInformation;         /**< NT4 (where as NtQuerySystemInformation is W2K). */
/** @} */

/** @name API from IPHLPAPI.DLL
 * @{ */
decltype(GetAdaptersInfo)               *g_pfnGetAdaptersInfo;
/** @} */

/** @name APIs from WS2_32.DLL
 * @note WSAIoctl is not present in wsock32.dll, so no point in trying the
 *       fallback here.
 * @{ */
decltype(WSAStartup)                    *g_pfnWSAStartup;
decltype(WSACleanup)                    *g_pfnWSACleanup;
decltype(WSASocketA)                    *g_pfnWSASocketA;
decltype(WSAIoctl)                      *g_pfnWSAIoctl;
decltype(WSAGetLastError)               *g_pfnWSAGetLastError;
decltype(closesocket)                   *g_pfnclosesocket;
decltype(inet_ntoa)                     *g_pfninet_ntoa;

/** @} */

/**
 * Resolve APIs not present on older windows versions.
 */
void VGSvcWinResolveApis(void)
{
    RTLDRMOD hLdrMod;
#define RESOLVE_SYMBOL(a_fn) do { RT_CONCAT(g_pfn, a_fn) = (decltype(a_fn) *)RTLdrGetFunction(hLdrMod, #a_fn); } while (0)

    /* From ADVAPI32.DLL: */
    int rc = RTLdrLoadSystem("advapi32.dll", true /*fNoUnload*/, &hLdrMod);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        RESOLVE_SYMBOL(RegisterServiceCtrlHandlerExA);
        RESOLVE_SYMBOL(ChangeServiceConfig2A);
        RESOLVE_SYMBOL(GetNamedSecurityInfoA);
        RESOLVE_SYMBOL(SetEntriesInAclA);
        RESOLVE_SYMBOL(SetNamedSecurityInfoA);
        RESOLVE_SYMBOL(LsaNtStatusToWinError);
        RTLdrClose(hLdrMod);
    }

    /* From KERNEL32.DLL: */
    rc = RTLdrLoadSystem("kernel32.dll", true /*fNoUnload*/, &hLdrMod);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        RESOLVE_SYMBOL(CreateToolhelp32Snapshot);
        RESOLVE_SYMBOL(Process32First);
        RESOLVE_SYMBOL(Process32Next);
        RESOLVE_SYMBOL(Module32First);
        RESOLVE_SYMBOL(Module32Next);
        RESOLVE_SYMBOL(GetSystemTimeAdjustment);
        RESOLVE_SYMBOL(SetSystemTimeAdjustment);
        RTLdrClose(hLdrMod);
    }

    /* From NTDLL.DLL: */
    rc = RTLdrLoadSystem("ntdll.dll", true /*fNoUnload*/, &hLdrMod);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        RESOLVE_SYMBOL(ZwQuerySystemInformation);
        RTLdrClose(hLdrMod);
    }

    /* From IPHLPAPI.DLL: */
    rc = RTLdrLoadSystem("iphlpapi.dll", true /*fNoUnload*/, &hLdrMod);
    if (RT_SUCCESS(rc))
    {
        RESOLVE_SYMBOL(GetAdaptersInfo);
        RTLdrClose(hLdrMod);
    }

    /* From WS2_32.DLL: */
    rc = RTLdrLoadSystem("ws2_32.dll", true /*fNoUnload*/, &hLdrMod);
    if (RT_SUCCESS(rc))
    {
        RESOLVE_SYMBOL(WSAStartup);
        RESOLVE_SYMBOL(WSACleanup);
        RESOLVE_SYMBOL(WSASocketA);
        RESOLVE_SYMBOL(WSAIoctl);
        RESOLVE_SYMBOL(WSAGetLastError);
        RESOLVE_SYMBOL(closesocket);
        RESOLVE_SYMBOL(inet_ntoa);
        RTLdrClose(hLdrMod);
    }
}


/**
 * @todo Add full unicode support.
 * @todo Add event log capabilities / check return values.
 */
static int vgsvcWinAddAceToObjectsSecurityDescriptor(LPCSTR pszObjName, SE_OBJECT_TYPE enmObjectType, const char *pszTrustee,
                                                     TRUSTEE_FORM enmTrusteeForm, DWORD dwAccessRights, ACCESS_MODE fAccessMode,
                                                     DWORD dwInheritance)
{
    int rc;
    if (   g_pfnGetNamedSecurityInfoA
        && g_pfnSetEntriesInAclA
        && g_pfnSetNamedSecurityInfoA)
    {
        /* Get a pointer to the existing DACL. */
        PSECURITY_DESCRIPTOR    pSD      = NULL;
        PACL                    pOldDACL = NULL;
        DWORD rcWin = g_pfnGetNamedSecurityInfoA(pszObjName, enmObjectType, DACL_SECURITY_INFORMATION,
                                                 NULL, NULL, &pOldDACL, NULL, &pSD);
        if (rcWin == ERROR_SUCCESS)
        {
            /* Initialize an EXPLICIT_ACCESS structure for the new ACE. */
            EXPLICIT_ACCESSA ExplicitAccess;
            RT_ZERO(ExplicitAccess);
            ExplicitAccess.grfAccessPermissions = dwAccessRights;
            ExplicitAccess.grfAccessMode        = fAccessMode;
            ExplicitAccess.grfInheritance       = dwInheritance;
            ExplicitAccess.Trustee.TrusteeForm  = enmTrusteeForm;
            ExplicitAccess.Trustee.ptstrName    = (char *)pszTrustee;

            /* Create a new ACL that merges the new ACE into the existing DACL. */
            PACL pNewDACL = NULL;
            rcWin = g_pfnSetEntriesInAclA(1, &ExplicitAccess, pOldDACL, &pNewDACL);
            if (rcWin == ERROR_SUCCESS)
            {
                /* Attach the new ACL as the object's DACL. */
                rcWin = g_pfnSetNamedSecurityInfoA((PSTR)pszObjName, enmObjectType, DACL_SECURITY_INFORMATION,
                                                   NULL, NULL, pNewDACL, NULL);
                if (rcWin == ERROR_SUCCESS)
                    rc = VINF_SUCCESS;
                else
                {
                    VGSvcError("AddAceToObjectsSecurityDescriptor: SetNamedSecurityInfo: Error %u\n", rcWin);
                    rc = RTErrConvertFromWin32(rcWin);
                }
                if (pNewDACL)
                    LocalFree(pNewDACL);
            }
            else
            {
                VGSvcError("AddAceToObjectsSecurityDescriptor: SetEntriesInAcl: Error %u\n", rcWin);
                rc = RTErrConvertFromWin32(rcWin);
            }
            if (pSD)
                LocalFree(pSD);
        }
        else
        {
            if (rcWin == ERROR_FILE_NOT_FOUND)
                VGSvcError("AddAceToObjectsSecurityDescriptor: Object not found/installed: %s\n", pszObjName);
            else
                VGSvcError("AddAceToObjectsSecurityDescriptor: GetNamedSecurityInfo: Error %u\n", rcWin);
            rc = RTErrConvertFromWin32(rcWin);
        }
    }
    else
        rc = VINF_SUCCESS; /* fake it */
    return rc;
}


/** Reports our current status to the SCM. */
static BOOL vgsvcWinSetStatus(DWORD dwStatus, DWORD dwCheckPoint)
{
    if (g_hWinServiceStatus == NULL) /* Program could be in testing mode, so no service environment available. */
        return FALSE;

    VGSvcVerbose(2, "Setting service status to: %ld\n", dwStatus);
    g_dwWinServiceLastStatus = dwStatus;

    SERVICE_STATUS ss;
    RT_ZERO(ss);

    ss.dwServiceType              = SERVICE_WIN32_OWN_PROCESS;
    ss.dwCurrentState             = dwStatus;
    /* Don't accept controls when in start pending state. */
    if (ss.dwCurrentState != SERVICE_START_PENDING)
    {
        ss.dwControlsAccepted     = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;

        /* Don't use SERVICE_ACCEPT_SESSIONCHANGE on Windows 2000 or earlier.  This makes SCM angry. */
        char szOSVersion[32];
        int rc = RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, szOSVersion, sizeof(szOSVersion));
        if (RT_SUCCESS(rc))
        {
            if (RTStrVersionCompare(szOSVersion, "5.1") >= 0)
                ss.dwControlsAccepted |= SERVICE_ACCEPT_SESSIONCHANGE;
        }
        else
            VGSvcError("Error determining OS version, rc=%Rrc\n", rc);
    }

    ss.dwWin32ExitCode            = NO_ERROR;
    ss.dwServiceSpecificExitCode  = 0; /* Not used */
    ss.dwCheckPoint               = dwCheckPoint;
    ss.dwWaitHint                 = 3000;

    BOOL fStatusSet = SetServiceStatus(g_hWinServiceStatus, &ss);
    if (!fStatusSet)
        VGSvcError("Error reporting service status=%ld (controls=%x, checkpoint=%ld) to SCM: %ld\n",
                   dwStatus, ss.dwControlsAccepted, dwCheckPoint, GetLastError());
    return fStatusSet;
}


/**
 * Reports SERVICE_STOP_PENDING to SCM.
 *
 * @param   uCheckPoint         Some number.
 */
void VGSvcWinSetStopPendingStatus(uint32_t uCheckPoint)
{
    vgsvcWinSetStatus(SERVICE_STOP_PENDING, uCheckPoint);
}


static RTEXITCODE vgsvcWinSetDesc(SC_HANDLE hService)
{
    /* On W2K+ there's ChangeServiceConfig2() which lets us set some fields
       like a longer service description. */
    if (g_pfnChangeServiceConfig2A)
    {
        /** @todo On Vista+ SERVICE_DESCRIPTION also supports localized strings! */
        SERVICE_DESCRIPTION desc;
        desc.lpDescription = (LPSTR)VBOXSERVICE_DESCRIPTION;
        if (!g_pfnChangeServiceConfig2A(hService, SERVICE_CONFIG_DESCRIPTION, &desc))
        {
            VGSvcError("Cannot set the service description! Error: %ld\n", GetLastError());
            return RTEXITCODE_FAILURE;
        }
    }
    return RTEXITCODE_SUCCESS;
}


/**
 * Installs the service.
 */
RTEXITCODE VGSvcWinInstall(void)
{
    VGSvcVerbose(1, "Installing service ...\n");

    TCHAR imagePath[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, imagePath, sizeof(imagePath));

    SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (hSCManager == NULL)
    {
        VGSvcError("Could not open SCM! Error: %ld\n", GetLastError());
        return RTEXITCODE_FAILURE;
    }

    RTEXITCODE rc       = RTEXITCODE_SUCCESS;
    SC_HANDLE  hService = CreateService(hSCManager,
                                        VBOXSERVICE_NAME, VBOXSERVICE_FRIENDLY_NAME,
                                        SERVICE_ALL_ACCESS,
                                        SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS,
                                        SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                                        imagePath, NULL, NULL, NULL, NULL, NULL);
    if (hService != NULL)
        VGSvcVerbose(0, "Service successfully installed!\n");
    else
    {
        DWORD dwErr = GetLastError();
        switch (dwErr)
        {
            case ERROR_SERVICE_EXISTS:
                VGSvcVerbose(1, "Service already exists, just updating the service config.\n");
                hService = OpenService(hSCManager, VBOXSERVICE_NAME, SERVICE_ALL_ACCESS);
                if (hService)
                {
                    if (ChangeServiceConfig(hService,
                                            SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS,
                                            SERVICE_DEMAND_START,
                                            SERVICE_ERROR_NORMAL,
                                            imagePath,
                                            NULL,
                                            NULL,
                                            NULL,
                                            NULL,
                                            NULL,
                                            VBOXSERVICE_FRIENDLY_NAME))
                        VGSvcVerbose(1, "The service config has been successfully updated.\n");
                    else
                        rc = VGSvcError("Could not change service config! Error: %ld\n", GetLastError());
                }
                else
                    rc = VGSvcError("Could not open service! Error: %ld\n", GetLastError());
                break;

            default:
                rc = VGSvcError("Could not create service! Error: %ld\n", dwErr);
                break;
        }
    }

    if (rc == RTEXITCODE_SUCCESS)
        rc = vgsvcWinSetDesc(hService);

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    return rc;
}

/**
 * Uninstalls the service.
 */
RTEXITCODE VGSvcWinUninstall(void)
{
    VGSvcVerbose(1, "Uninstalling service ...\n");

    SC_HANDLE hSCManager = OpenSCManager(NULL,NULL,SC_MANAGER_ALL_ACCESS);
    if (hSCManager == NULL)
    {
        VGSvcError("Could not open SCM! Error: %d\n", GetLastError());
        return RTEXITCODE_FAILURE;
    }

    RTEXITCODE rcExit;
    SC_HANDLE  hService = OpenService(hSCManager, VBOXSERVICE_NAME, SERVICE_ALL_ACCESS );
    if (hService != NULL)
    {
        if (DeleteService(hService))
        {
            /*
             * ???
             */
            HKEY hKey = NULL;
            if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                             "SYSTEM\\CurrentControlSet\\Services\\EventLog\\System",
                             0,
                             KEY_ALL_ACCESS,
                             &hKey)
                == ERROR_SUCCESS)
            {
                RegDeleteKey(hKey, VBOXSERVICE_NAME);
                RegCloseKey(hKey);
            }

            VGSvcVerbose(0, "Service successfully uninstalled!\n");
            rcExit = RTEXITCODE_SUCCESS;
        }
        else
            rcExit = VGSvcError("Could not remove service! Error: %d\n", GetLastError());
        CloseServiceHandle(hService);
    }
    else
        rcExit = VGSvcError("Could not open service! Error: %d\n", GetLastError());
    CloseServiceHandle(hSCManager);

    return rcExit;
}


static int vgsvcWinStart(void)
{
    int rc = VINF_SUCCESS;

    /*
     * Create a well-known SID for the "Builtin Users" group and modify the ACE
     * for the shared folders miniport redirector DN (whatever DN means).
     */
    PSID                     pBuiltinUsersSID = NULL;
    SID_IDENTIFIER_AUTHORITY SIDAuthWorld     = SECURITY_LOCAL_SID_AUTHORITY;
    if (AllocateAndInitializeSid(&SIDAuthWorld, 1, SECURITY_LOCAL_RID, 0, 0, 0, 0, 0, 0, 0, &pBuiltinUsersSID))
    {
        rc = vgsvcWinAddAceToObjectsSecurityDescriptor("\\\\.\\VBoxMiniRdrDN", SE_FILE_OBJECT,
                                                       (LPTSTR)pBuiltinUsersSID, TRUSTEE_IS_SID,
                                                       FILE_GENERIC_READ | FILE_GENERIC_WRITE, SET_ACCESS, NO_INHERITANCE);
        /* If we don't find our "VBoxMiniRdrDN" (for Shared Folders) object above,
           don't report an error; it just might be not installed. Otherwise this
           would cause the SCM to hang on starting up the service. */
        if (rc == VERR_FILE_NOT_FOUND || rc == VERR_PATH_NOT_FOUND)
            rc = VINF_SUCCESS;

        FreeSid(pBuiltinUsersSID);
    }
    else
        rc = RTErrConvertFromWin32(GetLastError());
    if (RT_SUCCESS(rc))
    {
        /*
         * Start the service.
         */
        vgsvcWinSetStatus(SERVICE_START_PENDING, 0);

        rc = VGSvcStartServices();
        if (RT_SUCCESS(rc))
        {
            vgsvcWinSetStatus(SERVICE_RUNNING, 0);
            VGSvcMainWait();
        }
        else
        {
            vgsvcWinSetStatus(SERVICE_STOPPED, 0);
#if 0 /** @todo r=bird: Enable this if SERVICE_CONTROL_STOP isn't triggered automatically */
            VGSvcStopServices();
#endif
        }
    }
    else
        vgsvcWinSetStatus(SERVICE_STOPPED, 0);

    if (RT_FAILURE(rc))
        VGSvcError("Service failed to start with rc=%Rrc!\n", rc);

    return rc;
}


/**
 * Call StartServiceCtrlDispatcher.
 *
 * The main() thread invokes this when not started in foreground mode.  It
 * won't return till the service is being shutdown (unless start up fails).
 *
 * @returns RTEXITCODE_SUCCESS on normal return after service shutdown.
 *          Something else on failure, error will have been reported.
 */
RTEXITCODE VGSvcWinEnterCtrlDispatcher(void)
{
    if (!StartServiceCtrlDispatcher(&g_aServiceTable[0]))
        return VGSvcError("StartServiceCtrlDispatcher: %u. Please start %s with option -f (foreground)!\n",
                          GetLastError(), g_pszProgName);
    return RTEXITCODE_SUCCESS;
}


/**
 * Event code to description.
 *
 * @returns String.
 * @param   dwEvent             The event code.
 */
static const char *vgsvcWTSStateToString(DWORD dwEvent)
{
    switch (dwEvent)
    {
        case WTS_CONSOLE_CONNECT:           return "A session was connected to the console terminal";
        case WTS_CONSOLE_DISCONNECT:        return "A session was disconnected from the console terminal";
        case WTS_REMOTE_CONNECT:            return "A session connected to the remote terminal";
        case WTS_REMOTE_DISCONNECT:         return "A session was disconnected from the remote terminal";
        case WTS_SESSION_LOGON:             return "A user has logged on to a session";
        case WTS_SESSION_LOGOFF:            return "A user has logged off the session";
        case WTS_SESSION_LOCK:              return "A session has been locked";
        case WTS_SESSION_UNLOCK:            return "A session has been unlocked";
        case WTS_SESSION_REMOTE_CONTROL:    return "A session has changed its remote controlled status";
#ifdef WTS_SESSION_CREATE
        case WTS_SESSION_CREATE:            return "A session has been created";
#endif
#ifdef WTS_SESSION_TERMINATE
        case WTS_SESSION_TERMINATE:         return "The session has been terminated";
#endif
        default:                            return "Uknonwn state";
    }
}


/**
 * Common control handler.
 *
 * @returns Return code for NT5+.
 * @param   dwControl           The control code.
 */
static DWORD vgsvcWinCtrlHandlerCommon(DWORD dwControl)
{
    DWORD rcRet = NO_ERROR;
    switch (dwControl)
    {
        case SERVICE_CONTROL_INTERROGATE:
            vgsvcWinSetStatus(g_dwWinServiceLastStatus, 0);
            break;

        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
        {
            vgsvcWinSetStatus(SERVICE_STOP_PENDING, 0);

            int rc2 = VGSvcStopServices();
            if (RT_FAILURE(rc2))
                rcRet = ERROR_GEN_FAILURE;
            else
            {
                rc2 = VGSvcReportStatus(VBoxGuestFacilityStatus_Terminated);
                AssertRC(rc2);
            }

            vgsvcWinSetStatus(SERVICE_STOPPED, 0);
            break;
        }

        default:
            VGSvcVerbose(1, "Control handler: Function not implemented: %#x\n", dwControl);
            rcRet = ERROR_CALL_NOT_IMPLEMENTED;
            break;
    }

    return rcRet;
}


/**
 * Callback registered by RegisterServiceCtrlHandler on NT4 and earlier.
 */
static VOID WINAPI vgsvcWinCtrlHandlerNt4(DWORD dwControl) RT_NOTHROW_DEF
{
    VGSvcVerbose(2, "Control handler (NT4): dwControl=%#x\n", dwControl);
    vgsvcWinCtrlHandlerCommon(dwControl);
}


/**
 * Callback registered by RegisterServiceCtrlHandler on NT5 and later.
 */
static DWORD WINAPI
vgsvcWinCtrlHandlerNt5Plus(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext) RT_NOTHROW_DEF
{
    VGSvcVerbose(2, "Control handler: dwControl=%#x, dwEventType=%#x\n", dwControl, dwEventType);
    RT_NOREF1(lpContext);

    switch (dwControl)
    {
        default:
            return vgsvcWinCtrlHandlerCommon(dwControl);

        case SERVICE_CONTROL_SESSIONCHANGE:  /* Only Windows 2000 and up. */
        {
            AssertPtr(lpEventData);
            PWTSSESSION_NOTIFICATION pNotify = (PWTSSESSION_NOTIFICATION)lpEventData;
            Assert(pNotify->cbSize == sizeof(WTSSESSION_NOTIFICATION));

            VGSvcVerbose(1, "Control handler: %s (Session=%ld, Event=%#x)\n",
                         vgsvcWTSStateToString(dwEventType), pNotify->dwSessionId, dwEventType);

            /* Handle all events, regardless of dwEventType. */
            int rc2 = VGSvcVMInfoSignal();
            AssertRC(rc2);

            return NO_ERROR;
        }
    }
}


static void WINAPI vgsvcWinMain(DWORD argc, LPTSTR *argv)
{
    RT_NOREF2(argc, argv);
    VGSvcVerbose(2, "Registering service control handler ...\n");
    if (g_pfnRegisterServiceCtrlHandlerExA)
        g_hWinServiceStatus = g_pfnRegisterServiceCtrlHandlerExA(VBOXSERVICE_NAME, vgsvcWinCtrlHandlerNt5Plus, NULL);
    else
        g_hWinServiceStatus = RegisterServiceCtrlHandlerA(VBOXSERVICE_NAME, vgsvcWinCtrlHandlerNt4);
    if (g_hWinServiceStatus != NULL)
    {
        VGSvcVerbose(2, "Service control handler registered.\n");
        vgsvcWinStart();
    }
    else
    {
        DWORD dwErr = GetLastError();
        switch (dwErr)
        {
            case ERROR_INVALID_NAME:
                VGSvcError("Invalid service name!\n");
                break;
            case ERROR_SERVICE_DOES_NOT_EXIST:
                VGSvcError("Service does not exist!\n");
                break;
            default:
                VGSvcError("Could not register service control handle! Error: %ld\n", dwErr);
                break;
        }
    }
}

