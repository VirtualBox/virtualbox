/* $Id: VBoxGINA.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGINA -- Windows Logon DLL for VirtualBox
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/win/windows.h>

#include <iprt/buildconfig.h>
#include <iprt/initterm.h>
#include <iprt/ldr.h>
#include <iprt/errcore.h>

#include <VBox/VBoxGuestLib.h>

#include "winwlx.h"
#include "VBoxGINA.h"
#include "Helper.h"
#include "Dialog.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** DLL instance handle. */
HINSTANCE hDllInstance;

/** Version of Winlogon. */
DWORD wlxVersion;

/** Handle to Winlogon service. */
HANDLE hGinaWlx;
/** Winlog function dispatch table. */
PWLX_DISPATCH_VERSION_1_1 pWlxFuncs;

/**
 * Function pointers to MSGINA entry points.
 */
PGWLXNEGOTIATE GWlxNegotiate;
PGWLXINITIALIZE GWlxInitialize;
PGWLXDISPLAYSASNOTICE GWlxDisplaySASNotice;
PGWLXLOGGEDOUTSAS GWlxLoggedOutSAS;
PGWLXACTIVATEUSERSHELL GWlxActivateUserShell;
PGWLXLOGGEDONSAS GWlxLoggedOnSAS;
PGWLXDISPLAYLOCKEDNOTICE GWlxDisplayLockedNotice;
PGWLXWKSTALOCKEDSAS GWlxWkstaLockedSAS;
PGWLXISLOCKOK GWlxIsLockOk;
PGWLXISLOGOFFOK GWlxIsLogoffOk;
PGWLXLOGOFF GWlxLogoff;
PGWLXSHUTDOWN GWlxShutdown;
/* GINA 1.1. */
PGWLXSTARTAPPLICATION GWlxStartApplication;
PGWLXSCREENSAVERNOTIFY GWlxScreenSaverNotify;
/* GINA 1.3. */
PGWLXNETWORKPROVIDERLOAD GWlxNetworkProviderLoad;
PGWLXDISPLAYSTATUSMESSAGE GWlxDisplayStatusMessage;
PGWLXGETSTATUSMESSAGE GWlxGetStatusMessage;
PGWLXREMOVESTATUSMESSAGE GWlxRemoveStatusMessage;
/* GINA 1.4. */
PGWLXGETCONSOLESWITCHCREDENTIALS GWlxGetConsoleSwitchCredentials;
PGWLXRECONNECTNOTIFY GWlxReconnectNotify;
PGWLXDISCONNECTNOTIFY GWlxDisconnectNotify;


/**
 * DLL entry point.
 */
BOOL WINAPI DllMain(HINSTANCE hInstance,
                    DWORD     dwReason,
                    LPVOID    pReserved)
{
    RT_NOREF(pReserved);
    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            RTR3InitDll(RTR3INIT_FLAGS_UNOBTRUSIVE);
            VbglR3Init();

            VBoxGINALoadConfiguration();

            VBoxGINAVerbose(0, "VBoxGINA: v%s r%s (%s %s) loaded\n",
                            RTBldCfgVersion(), RTBldCfgRevisionStr(),
                            __DATE__, __TIME__);

            DisableThreadLibraryCalls(hInstance);
            hDllInstance = hInstance;
            break;
        }

        case DLL_PROCESS_DETACH:
        {
            VBoxGINAVerbose(0, "VBoxGINA: Unloaded\n");
            VbglR3Term();
            /// @todo RTR3Term();
            break;
        }

        default:
            break;
    }
    return TRUE;
}


BOOL WINAPI WlxNegotiate(DWORD dwWinlogonVersion,
                         DWORD *pdwDllVersion)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxNegotiate: dwWinlogonVersion: %ld\n", dwWinlogonVersion);

    /* Load the standard Microsoft GINA DLL. */
    RTLDRMOD hLdrMod;
    int rc = RTLdrLoadSystem("MSGINA.DLL", true /*fNoUnload*/, &hLdrMod);
    if (RT_FAILURE(rc))
    {
        VBoxGINAVerbose(0, "VBoxGINA::WlxNegotiate: failed loading MSGINA! rc=%Rrc\n", rc);
        return FALSE;
    }

    /*
     * Now get the entry points of the MSGINA
     */
    GWlxNegotiate = (PGWLXNEGOTIATE)RTLdrGetFunction(hLdrMod, "WlxNegotiate");
    if (!GWlxNegotiate)
    {
        VBoxGINAVerbose(0, "VBoxGINA::WlxNegotiate: failed resolving WlxNegotiate\n");
        return FALSE;
    }
    GWlxInitialize = (PGWLXINITIALIZE)RTLdrGetFunction(hLdrMod, "WlxInitialize");
    if (!GWlxInitialize)
    {
        VBoxGINAVerbose(0, "VBoxGINA::WlxNegotiate: failed resolving WlxInitialize\n");
        return FALSE;
    }
    GWlxDisplaySASNotice =
        (PGWLXDISPLAYSASNOTICE)RTLdrGetFunction(hLdrMod, "WlxDisplaySASNotice");
    if (!GWlxDisplaySASNotice)
    {
        VBoxGINAVerbose(0, "VBoxGINA::WlxNegotiate: failed resolving WlxDisplaySASNotice\n");
        return FALSE;
    }
    GWlxLoggedOutSAS =
        (PGWLXLOGGEDOUTSAS)RTLdrGetFunction(hLdrMod, "WlxLoggedOutSAS");
    if (!GWlxLoggedOutSAS)
    {
        VBoxGINAVerbose(0, "VBoxGINA::WlxNegotiate: failed resolving WlxLoggedOutSAS\n");
        return FALSE;
    }
    GWlxActivateUserShell =
        (PGWLXACTIVATEUSERSHELL)RTLdrGetFunction(hLdrMod, "WlxActivateUserShell");
    if (!GWlxActivateUserShell)
    {
        VBoxGINAVerbose(0, "VBoxGINA::WlxNegotiate: failed resolving WlxActivateUserShell\n");
        return FALSE;
    }
    GWlxLoggedOnSAS =
        (PGWLXLOGGEDONSAS)RTLdrGetFunction(hLdrMod, "WlxLoggedOnSAS");
    if (!GWlxLoggedOnSAS)
    {
        VBoxGINAVerbose(0, "VBoxGINA::WlxNegotiate: failed resolving WlxLoggedOnSAS\n");
        return FALSE;
    }
    GWlxDisplayLockedNotice =
        (PGWLXDISPLAYLOCKEDNOTICE)RTLdrGetFunction(hLdrMod, "WlxDisplayLockedNotice");
    if (!GWlxDisplayLockedNotice)
    {
        VBoxGINAVerbose(0, "VBoxGINA::WlxNegotiate: failed resolving WlxDisplayLockedNotice\n");
        return FALSE;
    }
    GWlxIsLockOk = (PGWLXISLOCKOK)RTLdrGetFunction(hLdrMod, "WlxIsLockOk");
    if (!GWlxIsLockOk)
    {
        VBoxGINAVerbose(0, "VBoxGINA::WlxNegotiate: failed resolving WlxIsLockOk\n");
        return FALSE;
    }
    GWlxWkstaLockedSAS =
        (PGWLXWKSTALOCKEDSAS)RTLdrGetFunction(hLdrMod, "WlxWkstaLockedSAS");
    if (!GWlxWkstaLockedSAS)
    {
        VBoxGINAVerbose(0, "VBoxGINA::WlxNegotiate: failed resolving WlxWkstaLockedSAS\n");
        return FALSE;
    }
    GWlxIsLogoffOk = (PGWLXISLOGOFFOK)RTLdrGetFunction(hLdrMod, "WlxIsLogoffOk");
    if (!GWlxIsLogoffOk)
    {
        VBoxGINAVerbose(0, "VBoxGINA::WlxNegotiate: failed resolving WlxIsLogoffOk\n");
        return FALSE;
    }
    GWlxLogoff = (PGWLXLOGOFF)RTLdrGetFunction(hLdrMod, "WlxLogoff");
    if (!GWlxLogoff)
    {
        VBoxGINAVerbose(0, "VBoxGINA::WlxNegotiate: failed resolving WlxLogoff\n");
        return FALSE;
    }
    GWlxShutdown = (PGWLXSHUTDOWN)RTLdrGetFunction(hLdrMod, "WlxShutdown");
    if (!GWlxShutdown)
    {
        VBoxGINAVerbose(0, "VBoxGINA::WlxNegotiate: failed resolving WlxShutdown\n");
        return FALSE;
    }
    /* GINA 1.1, optional */
    GWlxStartApplication = (PGWLXSTARTAPPLICATION)RTLdrGetFunction(hLdrMod, "WlxStartApplication");
    GWlxScreenSaverNotify = (PGWLXSCREENSAVERNOTIFY)RTLdrGetFunction(hLdrMod, "WlxScreenSaverNotify");
    /* GINA 1.3, optional */
    GWlxNetworkProviderLoad = (PGWLXNETWORKPROVIDERLOAD)RTLdrGetFunction(hLdrMod, "WlxNetworkProviderLoad");
    GWlxDisplayStatusMessage = (PGWLXDISPLAYSTATUSMESSAGE)RTLdrGetFunction(hLdrMod, "WlxDisplayStatusMessage");
    GWlxGetStatusMessage = (PGWLXGETSTATUSMESSAGE)RTLdrGetFunction(hLdrMod, "WlxGetStatusMessage");
    GWlxRemoveStatusMessage = (PGWLXREMOVESTATUSMESSAGE)RTLdrGetFunction(hLdrMod, "WlxRemoveStatusMessage");
    /* GINA 1.4, optional */
    GWlxGetConsoleSwitchCredentials =
        (PGWLXGETCONSOLESWITCHCREDENTIALS)RTLdrGetFunction(hLdrMod, "WlxGetConsoleSwitchCredentials");
    GWlxReconnectNotify = (PGWLXRECONNECTNOTIFY)RTLdrGetFunction(hLdrMod, "WlxReconnectNotify");
    GWlxDisconnectNotify = (PGWLXDISCONNECTNOTIFY)RTLdrGetFunction(hLdrMod, "WlxDisconnectNotify");
    VBoxGINAVerbose(0, "VBoxGINA::WlxNegotiate: optional function pointers:\n"
                    "  WlxStartApplication: %p\n"
                    "  WlxScreenSaverNotify: %p\n"
                    "  WlxNetworkProviderLoad: %p\n"
                    "  WlxDisplayStatusMessage: %p\n"
                    "  WlxGetStatusMessage: %p\n"
                    "  WlxRemoveStatusMessage: %p\n"
                    "  WlxGetConsoleSwitchCredentials: %p\n"
                    "  WlxReconnectNotify: %p\n"
                    "  WlxDisconnectNotify: %p\n",
                    GWlxStartApplication, GWlxScreenSaverNotify, GWlxNetworkProviderLoad,
                    GWlxDisplayStatusMessage, GWlxGetStatusMessage, GWlxRemoveStatusMessage,
                    GWlxGetConsoleSwitchCredentials, GWlxReconnectNotify, GWlxDisconnectNotify);

    wlxVersion = dwWinlogonVersion;

    /* Acknowledge interface version. */
    if (pdwDllVersion)
        *pdwDllVersion = dwWinlogonVersion;

    return TRUE; /* We're ready to rumble! */
}


BOOL WINAPI WlxInitialize(LPWSTR lpWinsta, HANDLE hWlx, PVOID pvReserved,
                          PVOID pWinlogonFunctions, PVOID *pWlxContext)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxInitialize\n");

    /* Store Winlogon function table */
    pWlxFuncs = (PWLX_DISPATCH_VERSION_1_1)pWinlogonFunctions;

    /* Store handle to Winlogon service*/
    hGinaWlx = hWlx;

    VBoxGINAReportStatus(VBoxGuestFacilityStatus_Init);

    /* Hook the dialogs */
    hookDialogBoxes(pWlxFuncs, wlxVersion);

    /* Forward call */
    if (GWlxInitialize)
        return GWlxInitialize(lpWinsta, hWlx, pvReserved, pWinlogonFunctions, pWlxContext);
    return TRUE;
}


VOID WINAPI WlxDisplaySASNotice(PVOID pWlxContext)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxDisplaySASNotice\n");

    /* Check if there are credentials for us, if so simulate C-A-D */
    int rc = VbglR3CredentialsQueryAvailability();
    if (RT_SUCCESS(rc))
    {
        VBoxGINAVerbose(0, "VBoxGINA::WlxDisplaySASNotice: simulating C-A-D\n");
        /* Wutomatic C-A-D */
        pWlxFuncs->WlxSasNotify(hGinaWlx, WLX_SAS_TYPE_CTRL_ALT_DEL);
    }
    else
    {
        VBoxGINAVerbose(0, "VBoxGINA::WlxDisplaySASNotice: starting credentials poller\n");
        /* start the credentials poller thread */
        VBoxGINACredentialsPollerCreate();
        /* Forward call to MSGINA. */
        if (GWlxDisplaySASNotice)
            GWlxDisplaySASNotice(pWlxContext);
    }
}


int WINAPI WlxLoggedOutSAS(PVOID pWlxContext, DWORD dwSasType, PLUID pAuthenticationId,
                           PSID pLogonSid, PDWORD pdwOptions, PHANDLE phToken,
                           PWLX_MPR_NOTIFY_INFO pMprNotifyInfo, PVOID *pProfile)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxLoggedOutSAS\n");

    /* When performing a direct logon without C-A-D, our poller might not be running */
    int rc = VbglR3CredentialsQueryAvailability();
    if (RT_FAILURE(rc))
        VBoxGINACredentialsPollerCreate();

    if (GWlxLoggedOutSAS)
    {
        int iRet;
        iRet = GWlxLoggedOutSAS(pWlxContext, dwSasType, pAuthenticationId, pLogonSid,
                                pdwOptions, phToken, pMprNotifyInfo, pProfile);

        if (iRet == WLX_SAS_ACTION_LOGON)
        {
            //
            // Copy pMprNotifyInfo and pLogonSid for later use
            //

            // pMprNotifyInfo->pszUserName
            // pMprNotifyInfo->pszDomain
            // pMprNotifyInfo->pszPassword
            // pMprNotifyInfo->pszOldPassword
        }

        return iRet;
    }

    return WLX_SAS_ACTION_NONE;
}


/**
 * WinLogon calls this function following a successful logon to request that the GINA activate the user's shell program.
 */
BOOL WINAPI WlxActivateUserShell(PVOID pWlxContext, PWSTR pszDesktopName,
                                 PWSTR pszMprLogonScript, PVOID pEnvironment)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxActivateUserShell\n");

    /*
     * Report status "terminated" to the host -- this means that a user
     * got logged in (either manually or automatically using the provided credentials).
     */
    VBoxGINAReportStatus(VBoxGuestFacilityStatus_Terminated);

    /* Forward call to MSGINA. */
    if (GWlxActivateUserShell)
        return GWlxActivateUserShell(pWlxContext, pszDesktopName, pszMprLogonScript, pEnvironment);
    return TRUE; /* Activate the user shell. */
}


int WINAPI WlxLoggedOnSAS(PVOID pWlxContext, DWORD dwSasType, PVOID pReserved)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxLoggedOnSAS: dwSasType=%ld\n", dwSasType);

    /*
     * We don't want to do anything special here since the OS should behave
     * as VBoxGINA wouldn't have been installed. So pass all calls down
     * to the original MSGINA.
     */

    /* Forward call to MSGINA. */
    VBoxGINAVerbose(0, "VBoxGINA::WlxLoggedOnSAS: Forwarding call to MSGINA ...\n");
    if (GWlxLoggedOnSAS)
        return GWlxLoggedOnSAS(pWlxContext, dwSasType, pReserved);
    return WLX_SAS_ACTION_NONE;
}

VOID WINAPI WlxDisplayLockedNotice(PVOID pWlxContext)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxDisplayLockedNotice\n");

    /* Check if there are credentials for us, if so simulate C-A-D */
    int rc = VbglR3CredentialsQueryAvailability();
    if (RT_SUCCESS(rc))
    {
        VBoxGINAVerbose(0, "VBoxGINA::WlxDisplayLockedNotice: simulating C-A-D\n");
        /* Automatic C-A-D */
        pWlxFuncs->WlxSasNotify(hGinaWlx, WLX_SAS_TYPE_CTRL_ALT_DEL);
    }
    else
    {
        VBoxGINAVerbose(0, "VBoxGINA::WlxDisplayLockedNotice: starting credentials poller\n");
        /* start the credentials poller thread */
        VBoxGINACredentialsPollerCreate();
        /* Forward call to MSGINA. */
        if (GWlxDisplayLockedNotice)
            GWlxDisplayLockedNotice(pWlxContext);
    }
}


/*
 * Winlogon calls this function before it attempts to lock the workstation.
 */
BOOL WINAPI WlxIsLockOk(PVOID pWlxContext)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxIsLockOk\n");

    /* Forward call to MSGINA. */
    if (GWlxIsLockOk)
        return GWlxIsLockOk(pWlxContext);
    return TRUE; /* Locking is OK. */
}


int WINAPI WlxWkstaLockedSAS(PVOID pWlxContext, DWORD dwSasType)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxWkstaLockedSAS, dwSasType=%ld\n", dwSasType);

    /* When performing a direct logon without C-A-D, our poller might not be running */
    int rc = VbglR3CredentialsQueryAvailability();
    if (RT_FAILURE(rc))
        VBoxGINACredentialsPollerCreate();

    /* Forward call to MSGINA. */
    if (GWlxWkstaLockedSAS)
        return GWlxWkstaLockedSAS(pWlxContext, dwSasType);
    return WLX_SAS_ACTION_NONE;
}


BOOL WINAPI WlxIsLogoffOk(PVOID pWlxContext)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxIsLogoffOk\n");

    if (GWlxIsLogoffOk)
        return GWlxIsLogoffOk(pWlxContext);
    return TRUE; /* Log off is OK. */
}


/*
 * Winlogon calls this function to notify the GINA of a logoff operation on this
 * workstation. This allows the GINA to perform any logoff operations that may be required.
 */
VOID WINAPI WlxLogoff(PVOID pWlxContext)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxLogoff\n");

    /* No need to report the "active" status to the host here -- this will be done
     * when VBoxGINA gets the chance to hook the dialogs (again). */

    /* Forward call to MSGINA. */
    if (GWlxLogoff)
        GWlxLogoff(pWlxContext);
}


/*
 * Winlogon calls this function just before shutting down.
 * This allows the GINA to perform any necessary shutdown tasks.
 * Will be called *after* WlxLogoff!
 */
VOID WINAPI WlxShutdown(PVOID pWlxContext, DWORD ShutdownType)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxShutdown\n");

    /*
     * Report status "inactive" to the host -- this means the
     * auto-logon feature won't be active anymore at this point
     * (until it maybe gets loaded again after a reboot).
     */
    VBoxGINAReportStatus(VBoxGuestFacilityStatus_Inactive);

    /* Forward call to MSGINA. */
    if (GWlxShutdown)
        GWlxShutdown(pWlxContext, ShutdownType);
}


/*
 * GINA 1.1 entry points
 */
BOOL WINAPI WlxScreenSaverNotify(PVOID pWlxContext, BOOL *pSecure)
{
    RT_NOREF(pWlxContext);
    VBoxGINAVerbose(0, "VBoxGINA::WlxScreenSaverNotify, pSecure=%d\n",
                    pSecure ? *pSecure : 0);

    /* Report the status to "init" since the screensaver
     * (Winlogon) does not give VBoxGINA yet the chance to hook into dialogs
     * which only then in turn would set the status to "active" -- so
     * at least set some status here. */
    VBoxGINAReportStatus(VBoxGuestFacilityStatus_Init);

    /* Note: Disabling the screensaver's grace period is necessary to get
     *       VBoxGINA loaded and set the status to "terminated" again properly
     *       after the logging-in handling was done. To do this:
     *       - on a non-domain machine, set:
     *         HKLM\Software\Microsoft\Windows NT\CurrentVersion\Winlogon\ScreenSaverGracePeriod (REG_SZ)
     *         to "0"
     *       - on a machine joined a domain:
     *         use the group policy preferences and/or the registry key above,
     *         depending on the domain's policies.
     */

    /* Indicate that the workstation should be locked. */
    *pSecure = TRUE;

    return TRUE; /* Screensaver should be activated. */
}


BOOL WINAPI WlxStartApplication(PVOID pWlxContext, PWSTR pszDesktopName,
                                PVOID pEnvironment, PWSTR pszCmdLine)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxStartApplication: pWlxCtx=%p, pszDesktopName=%ls, pEnvironment=%p, pszCmdLine=%ls\n",
                    pWlxContext, pszDesktopName, pEnvironment, pszCmdLine);

    /* Forward to MSGINA if present. */
    if (GWlxStartApplication)
        return GWlxStartApplication(pWlxContext, pszDesktopName, pEnvironment, pszCmdLine);
    return FALSE;
}


/*
 * GINA 1.3 entry points
 */
BOOL WINAPI WlxNetworkProviderLoad(PVOID pWlxContext, PWLX_MPR_NOTIFY_INFO pNprNotifyInfo)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxNetworkProviderLoad\n");

    /* Forward to MSGINA if present. */
    if (GWlxNetworkProviderLoad)
        return GWlxNetworkProviderLoad(pWlxContext, pNprNotifyInfo);
    return FALSE;
}


BOOL WINAPI WlxDisplayStatusMessage(PVOID pWlxContext, HDESK hDesktop, DWORD dwOptions,
                                    PWSTR pTitle, PWSTR pMessage)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxDisplayStatusMessage\n");

    /* Forward to MSGINA if present. */
    if (GWlxDisplayStatusMessage)
        return GWlxDisplayStatusMessage(pWlxContext, hDesktop, dwOptions, pTitle, pMessage);
    return FALSE;
}


BOOL WINAPI WlxGetStatusMessage(PVOID pWlxContext, DWORD *pdwOptions,
                                PWSTR pMessage, DWORD dwBufferSize)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxGetStatusMessage\n");

    /* Forward to MSGINA if present. */
    if (GWlxGetStatusMessage)
        return GWlxGetStatusMessage(pWlxContext, pdwOptions, pMessage, dwBufferSize);
    return FALSE;
}


BOOL WINAPI WlxRemoveStatusMessage(PVOID pWlxContext)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxRemoveStatusMessage\n");

    /* Forward to MSGINA if present. */
    if (GWlxRemoveStatusMessage)
        return GWlxRemoveStatusMessage(pWlxContext);
    return FALSE;
}


/*
 * GINA 1.4 entry points
 */
BOOL WINAPI WlxGetConsoleSwitchCredentials(PVOID pWlxContext,PVOID pCredInfo)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxGetConsoleSwitchCredentials\n");

    /* Forward call to MSGINA if present */
    if (GWlxGetConsoleSwitchCredentials)
        return GWlxGetConsoleSwitchCredentials(pWlxContext,pCredInfo);
    return FALSE;
}


VOID WINAPI WlxReconnectNotify(PVOID pWlxContext)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxReconnectNotify\n");

    /* Forward to MSGINA if present. */
    if (GWlxReconnectNotify)
        GWlxReconnectNotify(pWlxContext);
}


VOID WINAPI WlxDisconnectNotify(PVOID pWlxContext)
{
    VBoxGINAVerbose(0, "VBoxGINA::WlxDisconnectNotify\n");

    /* Forward to MSGINA if present. */
    if (GWlxDisconnectNotify)
        GWlxDisconnectNotify(pWlxContext);
}


/*
 * Windows Notification Package callbacks
 */
void WnpScreenSaverStop(PWLX_NOTIFICATION_INFO pInfo)
{
    RT_NOREF(pInfo);
    VBoxGINAVerbose(0, "VBoxGINA::WnpScreenSaverStop\n");

    /*
     * Because we set the status to "init" in WlxScreenSaverNotify when
     * the screensaver becomes active we also have to take into account
     * that in case the screensaver terminates (either within the grace
     * period or because the lock screen appears) we have to set the
     * status accordingly.
     */
    VBoxGINAReportStatus(VBoxGuestFacilityStatus_Terminated);
}


DWORD WINAPI VBoxGINADebug(void)
{
#ifdef DEBUG
    DWORD dwVersion;
    BOOL fRes = WlxNegotiate(WLX_VERSION_1_4, &dwVersion);
    if (!fRes)
        return 1;

    void* pWlxContext = NULL;
    WLX_DISPATCH_VERSION_1_4 wlxDispatch;
    ZeroMemory(&wlxDispatch, sizeof(WLX_DISPATCH_VERSION_1_4));

    fRes = WlxInitialize(0, 0,
                         NULL /* Reserved */,
                         NULL /* Winlogon functions */,
                         &pWlxContext);
    if (!fRes)
        return 2;

    WlxDisplaySASNotice(pWlxContext);

    char szSID[MAX_PATH];
    LUID luidAuth;
    DWORD dwOpts;
    WLX_MPR_NOTIFY_INFO wlxNotifyInfo;
    void* pvProfile;
    HANDLE hToken;
    int iRes = WlxLoggedOutSAS(pWlxContext, WLX_SAS_TYPE_CTRL_ALT_DEL,
                               &luidAuth, szSID,
                               &dwOpts, &hToken, &wlxNotifyInfo, &pvProfile);
    return iRes;
#else
    return 0;
#endif
}

