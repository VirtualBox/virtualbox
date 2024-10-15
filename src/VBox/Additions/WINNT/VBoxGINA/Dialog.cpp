/* $Id: Dialog.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGINA - Windows Logon DLL for VirtualBox, Dialog Code.
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

#include <VBox/VBoxGuestLib.h>
#include <iprt/errcore.h>
#include <iprt/utf16.h>

#include "Dialog.h"
#include "WinWlx.h"
#include "Helper.h"
#include "VBoxGINA.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/*
 * Dialog IDs for legacy Windows OSes (e.g. NT 4.0).
 */
#define IDD_WLXDIAPLAYSASNOTICE_DIALOG    1400
#define IDD_WLXLOGGEDOUTSAS_DIALOG        1450
/** Change password dialog: To change the current
 *  account password. */
#define IDD_CHANGE_PASSWORD_DIALOG        1550
#define IDD_WLXLOGGEDONSAS_DIALOG         1650
/** Security dialog: To lock the workstation, log off
 *  change password, ... */
#define IDD_SECURITY_DIALOG               1800
/** Locked dialog: To unlock the currently lockted
 *  workstation. */
#define IDD_WLXWKSTALOCKEDSAS_DIALOG      1850
/** Shutdown dialog: To either restart, logoff current
 *  user or shutdown the workstation. */
#define IDD_SHUTDOWN_DIALOG               2200
/** Logoff dialog: "Do you really want to logoff?". */
#define IDD_LOGOFF_DIALOG                 2250


/*
 * Dialog IDs for Windows 2000 and up.
 */
#define IDD_WLXLOGGEDOUTSAS_DIALOG2       1500
/** Change password dialog: To change the current
 *  account password. */
#define IDD_CHANGE_PASSWORD_DIALOG2       1700
/** Locked dialog: To unlock the currently lockted
 *  workstation. */
#define IDD_WLXWKSTALOCKEDSAS_DIALOG2     1950


/*
 * Control IDs.
 */
#define IDC_WLXLOGGEDOUTSAS_USERNAME      1453
#define IDC_WLXLOGGEDOUTSAS_USERNAME2     1502
#define IDC_WLXLOGGEDOUTSAS_PASSWORD      1454
#define IDC_WLXLOGGEDOUTSAS_PASSWORD2     1503
#define IDC_WLXLOGGEDOUTSAS_DOMAIN        1455
#define IDC_WLXLOGGEDOUTSAS_DOMAIN2       1504

#define IDC_WKSTALOCKED_USERNAME          1953
#define IDC_WKSTALOCKED_PASSWORD          1954
#define IDC_WKSTALOCKEd_DOMAIN            1856
#define IDC_WKSTALOCKED_DOMAIN2           1956


/*
 * Own IDs.
 */
#define IDT_BASE                          WM_USER  + 1100 /* Timer ID base. */
#define IDT_LOGGEDONDLG_POLL              IDT_BASE + 1
#define IDT_LOCKEDDLG_POLL                IDT_BASE + 2


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static DLGPROC g_pfnWlxLoggedOutSASDlgProc = NULL;
static DLGPROC g_pfnWlxLockedSASDlgProc = NULL;

static PWLX_DIALOG_BOX_PARAM g_pfnWlxDialogBoxParam = NULL;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
int WINAPI MyWlxDialogBoxParam (HANDLE, HANDLE, LPWSTR, HWND, DLGPROC, LPARAM);


void hookDialogBoxes(PVOID pWinlogonFunctions, DWORD dwWlxVersion)
{
    if (!pWinlogonFunctions) /* Needed for testcase. */
        return;

    VBoxGINAVerbose(0, "VBoxGINA::hookDialogBoxes\n");

    /* this is version dependent */
    switch (dwWlxVersion)
    {
        case WLX_VERSION_1_0:
        {
            g_pfnWlxDialogBoxParam = ((PWLX_DISPATCH_VERSION_1_0)pWinlogonFunctions)->WlxDialogBoxParam;
            ((PWLX_DISPATCH_VERSION_1_0)pWinlogonFunctions)->WlxDialogBoxParam = MyWlxDialogBoxParam;
            break;
        }

        case WLX_VERSION_1_1:
        {
            g_pfnWlxDialogBoxParam = ((PWLX_DISPATCH_VERSION_1_1)pWinlogonFunctions)->WlxDialogBoxParam;
            ((PWLX_DISPATCH_VERSION_1_1)pWinlogonFunctions)->WlxDialogBoxParam = MyWlxDialogBoxParam;
            break;
        }

        case WLX_VERSION_1_2:
        {
            g_pfnWlxDialogBoxParam = ((PWLX_DISPATCH_VERSION_1_2)pWinlogonFunctions)->WlxDialogBoxParam;
            ((PWLX_DISPATCH_VERSION_1_2)pWinlogonFunctions)->WlxDialogBoxParam = MyWlxDialogBoxParam;
            break;
        }

        case WLX_VERSION_1_3:
        {
            g_pfnWlxDialogBoxParam = ((PWLX_DISPATCH_VERSION_1_3)pWinlogonFunctions)->WlxDialogBoxParam;
            ((PWLX_DISPATCH_VERSION_1_3)pWinlogonFunctions)->WlxDialogBoxParam = MyWlxDialogBoxParam;
            break;
        }

        case WLX_VERSION_1_4:
        {
            g_pfnWlxDialogBoxParam = ((PWLX_DISPATCH_VERSION_1_4)pWinlogonFunctions)->WlxDialogBoxParam;
            ((PWLX_DISPATCH_VERSION_1_4)pWinlogonFunctions)->WlxDialogBoxParam = MyWlxDialogBoxParam;
            break;
        }

        default:
        {
            VBoxGINAVerbose(0, "VBoxGINA::hookDialogBoxes: unrecognized version '%d', nothing hooked!\n", dwWlxVersion);
            /* not good, don't do anything */
            break;
        }
    }
}

/**
 * Enters credentials into the given text fields.
 *
 * @return  IPRT status code.
 * @param   hwndDlg                 Handle of dialog to enter credentials into.
 * @param   hwndUserId              Handle of username text field. Optional.
 * @param   hwndPassword            Handle of password text field. Optional.
 * @param   hwndDomain              Handle of domain text field. Optional.
 * @param   pwszUser                Username to enter into username text field.
 * @param   pwszPassword            Password to enter into password text field.
 * @param   pwszDomain              Domain to enter into domain text field.
 */
int credentialsToUI(HWND hwndDlg,
                    HWND hwndUserId, HWND hwndPassword, HWND hwndDomain,
                    PCRTUTF16 pwszUser, PCRTUTF16 pwszPassword, PCRTUTF16 pwszDomain)
{
    RT_NOREF(hwndDlg);
    BOOL bIsFQDN = FALSE;
    wchar_t szUserFQDN[512]; /* VMMDEV_CREDENTIALS_STRLEN + 255 bytes max. for FQDN */
    if (hwndDomain)
    {
        /* search the domain combo box for our required domain and select it */
        VBoxGINAVerbose(0, "VBoxGINA::MyWlxLoggedOutSASDlgProc: Trying to find domain entry in combo box ...\n");
        DWORD dwIndex = (DWORD) SendMessage(hwndDomain, CB_FINDSTRING,
                                            0, (LPARAM)pwszDomain);
        if (dwIndex != CB_ERR)
        {
            VBoxGINAVerbose(0, "VBoxGINA::MyWlxLoggedOutSASDlgProc: Found domain at combo box pos %ld\n", dwIndex);
            SendMessage(hwndDomain, CB_SETCURSEL, (WPARAM) dwIndex, 0);
            EnableWindow(hwndDomain, FALSE);
        }
        else
        {
            VBoxGINAVerbose(0, "VBoxGINA::MyWlxLoggedOutSASDlgProc: Domain not found in combo box ...\n");

            /* If the domain value has a dot (.) in it, it is a FQDN (Fully Qualified Domain Name)
             * which will not work with the combo box selection because Windows only keeps the
             * NETBIOS names to the left most part of the domain name there. Of course a FQDN
             * then will not be found by the search in the block above.
             *
             * To solve this problem the FQDN domain value will be appended at the user name value
             * (Kerberos style) using an "@", e.g. "<user-name>@full.qualified.domain".
             *
             */
            size_t l = RTUtf16Len(pwszDomain);
            if (l > 255)
                VBoxGINAVerbose(0, "VBoxGINA::MyWlxLoggedOutSASDlgProc: Warning! FQDN (domain) is too long (max 255 bytes), will be truncated!\n");

            if (*pwszUser) /* We need a user name that we can use in caes of a FQDN */
            {
                if (l > 16) /* Domain name is longer than 16 chars, cannot be a NetBIOS name anymore */
                {
                    VBoxGINAVerbose(0, "VBoxGINA::MyWlxLoggedOutSASDlgProc: Domain seems to be a FQDN (length)!\n");
                    bIsFQDN = TRUE;
                }
                else if (   l > 0
                         && RTUtf16Chr(pwszDomain, L'.') != NULL) /* if we found a dot (.) in the domain name, this has to be a FQDN */
                {
                    VBoxGINAVerbose(0, "VBoxGINA::MyWlxLoggedOutSASDlgProc: Domain seems to be a FQDN (dot)!\n");
                    bIsFQDN = TRUE;
                }

                if (bIsFQDN)
                {
                    RTUtf16Printf(szUserFQDN, sizeof(szUserFQDN) / sizeof(wchar_t), "%ls@%ls", pwszUser, pwszDomain);
                    VBoxGINAVerbose(0, "VBoxGINA::MyWlxLoggedOutSASDlgProc: FQDN user name is now: %s!\n", szUserFQDN);
                }
            }
        }
    }
    if (hwndUserId)
    {
        if (!bIsFQDN)
            SendMessage(hwndUserId, WM_SETTEXT, 0, (LPARAM)pwszUser);
        else
            SendMessage(hwndUserId, WM_SETTEXT, 0, (LPARAM)szUserFQDN);
    }
    if (hwndPassword)
        SendMessage(hwndPassword, WM_SETTEXT, 0, (LPARAM)pwszPassword);

    return VINF_SUCCESS; /** @todo */
}

/**
 * Tries to retrieve credentials and enters them into the specified windows,
 * optionally followed by a button press to confirm/abort the dialog.
 *
 * @return  IPRT status code.
 * @param   hwndDlg                 Handle of dialog to enter credentials into.
 * @param   hwndUserId              Handle of username text field. Optional.
 * @param   hwndPassword            Handle of password text field. Optional.
 * @param   hwndDomain              Handle of domain text field. Optional.
 * @param   wButtonToPress          Button ID of dialog to press after successful
 *                                  retrieval + storage. If set to 0 no button will
 *                                  be pressed.
 */
int credentialsHandle(HWND hwndDlg,
                      HWND hwndUserId, HWND hwndPassword, HWND hwndDomain,
                      WORD wButtonToPress)
{
    int rc = VINF_SUCCESS;

    if (!VBoxGINAHandleCurrentSession())
        rc = VERR_NOT_FOUND;

    if (RT_SUCCESS(rc))
    {
        rc = VbglR3CredentialsQueryAvailability();
        if (RT_FAILURE(rc))
        {
            if (rc != VERR_NOT_FOUND)
                VBoxGINAVerbose(0, "VBoxGINA::credentialsHandle: error querying for credentials, rc=%Rrc\n", rc);
        }
    }

    if (RT_SUCCESS(rc))
    {
        VBoxGINAVerbose(0, "VBoxGINA::credentialsHandle: credentials available\n");

        /*
         * Set status to "terminating" to let the host know this module now
         * tries to receive and use passed credentials so that credentials from
         * the host won't be sent twice.
         */
        VBoxGINAReportStatus(VBoxGuestFacilityStatus_Terminating);

        PRTUTF16 pwszUser, pwszPassword, pwszDomain;
        rc = VbglR3CredentialsRetrieveUtf16(&pwszUser, &pwszPassword, &pwszDomain);
        if (RT_SUCCESS(rc))
        {
#ifdef DEBUG
            VBoxGINAVerbose(0, "VBoxGINA::credentialsHandle: retrieved credentials: user=%ls, password=%ls, domain=%ls\n",
                            pwszUser, pwszPassword, pwszDomain);
#else
            VBoxGINAVerbose(0, "VBoxGINA::credentialsHandle: retrieved credentials: user=%ls, password=XXX, domain=%ls\n",
                            pwszUser, pwszDomain);
#endif
            /* Fill in credentials to appropriate UI elements. */
            rc = credentialsToUI(hwndDlg,
                                 hwndUserId, hwndPassword, hwndDomain,
                                 pwszUser, pwszPassword, pwszDomain);
            if (RT_SUCCESS(rc))
            {
                /* Confirm/cancel the dialog by pressing the appropriate button. */
                if (wButtonToPress)
                {
                    WPARAM wParam = MAKEWPARAM(wButtonToPress, BN_CLICKED);
                    PostMessage(hwndDlg, WM_COMMAND, wParam, 0);
                }
            }

            VbglR3CredentialsDestroyUtf16(pwszUser, pwszPassword, pwszDomain,
                                          3 /* Passes */);
        }
    }

#ifdef DEBUG
    VBoxGINAVerbose(3, "VBoxGINA::credentialsHandle: returned with rc=%Rrc\n", rc);
#endif
    return rc;
}

INT_PTR CALLBACK MyWlxLoggedOutSASDlgProc(HWND   hwndDlg,  // handle to dialog box
                                          UINT   uMsg,     // message
                                          WPARAM wParam,   // first message parameter
                                          LPARAM lParam)   // second message parameter
{
    BOOL bResult;
    static HWND s_hwndUserId, s_hwndPassword, s_hwndDomain = 0;

    /*VBoxGINAVerbose(0, "VBoxGINA::MyWlxLoggedOutSASDlgProc\n");*/

    //
    // Pass on to MSGINA first.
    //
    bResult = g_pfnWlxLoggedOutSASDlgProc(hwndDlg, uMsg, wParam, lParam);

    //
    // We are only interested in the WM_INITDIALOG message.
    //
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            VBoxGINAVerbose(0, "VBoxGINA::MyWlxLoggedOutSASDlgProc: got WM_INITDIALOG\n");

            /* get the entry fields */
            s_hwndUserId = GetDlgItem(hwndDlg, IDC_WLXLOGGEDOUTSAS_USERNAME);
            if (!s_hwndUserId)
                s_hwndUserId = GetDlgItem(hwndDlg, IDC_WLXLOGGEDOUTSAS_USERNAME2);
            s_hwndPassword = GetDlgItem(hwndDlg, IDC_WLXLOGGEDOUTSAS_PASSWORD);
            if (!s_hwndPassword)
                s_hwndPassword = GetDlgItem(hwndDlg, IDC_WLXLOGGEDOUTSAS_PASSWORD2);
            s_hwndDomain = GetDlgItem(hwndDlg, IDC_WLXLOGGEDOUTSAS_DOMAIN);
            if (!s_hwndDomain)
                s_hwndDomain = GetDlgItem(hwndDlg, IDC_WLXLOGGEDOUTSAS_DOMAIN2);

            VBoxGINAVerbose(0, "VBoxGINA::MyWlxLoggedOutSASDlgProc: hwndUserId: %x, hwndPassword: %d, hwndDomain: %d\n",
                            s_hwndUserId, s_hwndPassword, s_hwndDomain);

            /* terminate the credentials poller thread, it's done is job */
            VBoxGINACredentialsPollerTerminate();

            int rc = credentialsHandle(hwndDlg,
                                       s_hwndUserId, s_hwndPassword, s_hwndDomain,
                                       IDOK /* Button */);
            if (RT_FAILURE(rc))
            {
                /*
                 * The dialog is there but we don't have any credentials.
                 * Create a timer and poll for them.
                 */
                UINT_PTR uTimer = SetTimer(hwndDlg, IDT_LOGGEDONDLG_POLL, 200, NULL);
                if (!uTimer)
                    VBoxGINAVerbose(0, "VBoxGINA::MyWlxLoggedOutSASDlgProc: failed creating timer! Last error: %ld\n",
                                    GetLastError());
            }
            break;
        }

        case WM_TIMER:
        {
            /* is it our credentials poller timer? */
            if (wParam == IDT_LOGGEDONDLG_POLL)
            {
                int rc = credentialsHandle(hwndDlg,
                                           s_hwndUserId, s_hwndPassword, s_hwndDomain,
                                           IDOK /* Button */);
                if (RT_SUCCESS(rc))
                {
                    /* we don't need the timer any longer */
                    KillTimer(hwndDlg, IDT_LOGGEDONDLG_POLL);
                }
            }
            break;
        }

        case WM_DESTROY:
            KillTimer(hwndDlg, IDT_LOGGEDONDLG_POLL);
            break;
    }
    return bResult;
}


INT_PTR CALLBACK MyWlxLockedSASDlgProc(HWND   hwndDlg,  // handle to dialog box
                                       UINT   uMsg,     // message
                                       WPARAM wParam,   // first message parameter
                                       LPARAM lParam)   // second message parameter
{
    BOOL bResult;
    static HWND s_hwndPassword = 0;

    /*VBoxGINAVerbose(0, "VBoxGINA::MyWlxLockedSASDlgProc\n");*/

    //
    // Pass on to MSGINA first.
    //
    bResult = g_pfnWlxLockedSASDlgProc(hwndDlg, uMsg, wParam, lParam);

    //
    // We are only interested in the WM_INITDIALOG message.
    //
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            VBoxGINAVerbose(0, "VBoxGINA::MyWlxLockedSASDlgProc: WM_INITDIALOG\n");

            /* get the entry fields */
            s_hwndPassword = GetDlgItem(hwndDlg, IDC_WKSTALOCKED_PASSWORD);
            VBoxGINAVerbose(0, "VBoxGINA::MyWlxLockedSASDlgProc: hwndPassword: %d\n", s_hwndPassword);

            /* terminate the credentials poller thread, it's done is job */
            VBoxGINACredentialsPollerTerminate();

            int rc = credentialsHandle(hwndDlg,
                                       NULL /* Username */, s_hwndPassword, NULL /* Domain */,
                                       IDOK /* Button */);
            if (RT_FAILURE(rc))
            {
                /*
                 * The dialog is there but we don't have any credentials.
                 * Create a timer and poll for them.
                 */
                UINT_PTR uTimer = SetTimer(hwndDlg, IDT_LOCKEDDLG_POLL, 200, NULL);
                if (!uTimer)
                    VBoxGINAVerbose(0, "VBoxGINA::MyWlxLockedSASDlgProc: failed creating timer! Last error: %ld\n",
                         GetLastError());
            }
            break;
        }

        case WM_TIMER:
        {
            /* is it our credentials poller timer? */
            if (wParam == IDT_LOCKEDDLG_POLL)
            {
                int rc = credentialsHandle(hwndDlg,
                                           NULL /* Username */, s_hwndPassword, NULL /* Domain */,
                                           IDOK /* Button */);
                if (RT_SUCCESS(rc))
                {
                    /* we don't need the timer any longer */
                    KillTimer(hwndDlg, IDT_LOCKEDDLG_POLL);
                }
            }
            break;
        }

        case WM_DESTROY:
        {
            VBoxGINAVerbose(0, "VBoxGINA::MyWlxLockedSASDlgProc: WM_DESTROY\n");

            /* Because this is the only point where we know within our module that the locked
             * dialog has been closed by a valid unlock password we have to set the appropriate
             * facility status here. */
            VBoxGINAReportStatus(VBoxGuestFacilityStatus_Terminated);

            KillTimer(hwndDlg, IDT_LOCKEDDLG_POLL);
            break;
        }
    }
    return bResult;
}


int WINAPI MyWlxDialogBoxParam(HANDLE  hWlx,
                               HANDLE  hInst,
                               LPWSTR  pszTemplate,
                               HWND    hwndOwner,
                               DLGPROC dlgprc,
                               LPARAM  dwInitParam)
{
    VBoxGINAVerbose(0, "VBoxGINA::MyWlxDialogBoxParam: pszTemplate=%ls\n", pszTemplate);

    VBoxGINAReportStatus(VBoxGuestFacilityStatus_Active);

    //
    // We only know MSGINA dialogs by identifiers.
    //
    if (((uintptr_t)pszTemplate >> 16) == 0)
    {
        //
        // Hook appropriate dialog boxes as necessary.
        //
        switch ((DWORD)(uintptr_t)pszTemplate)
        {
            case IDD_WLXDIAPLAYSASNOTICE_DIALOG:
                VBoxGINAVerbose(0, "VBoxGINA::MyWlxDialogBoxParam: SAS notice dialog displayed; not handled\n");
                break;

            case IDD_WLXLOGGEDOUTSAS_DIALOG:     /* Windows NT 4.0. */
            case IDD_WLXLOGGEDOUTSAS_DIALOG2:    /* Windows 2000 and up. */
            {
                VBoxGINAVerbose(0, "VBoxGINA::MyWlxDialogBoxParam: returning hooked SAS logged out dialog\n");
                g_pfnWlxLoggedOutSASDlgProc = dlgprc;
                return g_pfnWlxDialogBoxParam(hWlx, hInst, pszTemplate, hwndOwner,
                                              MyWlxLoggedOutSASDlgProc, dwInitParam);
            }

            case IDD_SECURITY_DIALOG:
                VBoxGINAVerbose(0, "VBoxGINA::MyWlxDialogBoxParam: Security dialog displayed; not handled\n");
                break;

            case IDD_WLXWKSTALOCKEDSAS_DIALOG:   /* Windows NT 4.0. */
            case IDD_WLXWKSTALOCKEDSAS_DIALOG2:  /* Windows 2000 and up. */
            {
                VBoxGINAVerbose(0, "VBoxGINA::MyWlxDialogBoxParam: returning hooked SAS locked dialog\n");
                g_pfnWlxLockedSASDlgProc = dlgprc;
                return g_pfnWlxDialogBoxParam(hWlx, hInst, pszTemplate, hwndOwner,
                                              MyWlxLockedSASDlgProc, dwInitParam);
            }

            /** @todo Add other hooking stuff here. */

            default:
                VBoxGINAVerbose(0, "VBoxGINA::MyWlxDialogBoxParam: dialog %p (%u) not handled\n",
                                pszTemplate, (DWORD)(uintptr_t)pszTemplate);
                break;
        }
    }

    /* The rest will be redirected. */
    return g_pfnWlxDialogBoxParam(hWlx, hInst, pszTemplate, hwndOwner, dlgprc, dwInitParam);
}

