/* $Id: VBoxNetAdpUninstall.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NetAdpUninstall - VBoxNetAdp uninstaller command line tool
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
#include <VBox/VBoxNetCfg-win.h>
#include <VBox/VBoxDrvCfg-win.h>

#include <devguid.h>

#include <iprt/initterm.h>
#include <iprt/message.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef NDIS60
# define VBOX_NETADP_HWID L"sun_VBoxNetAdp6"
#else
# define VBOX_NETADP_HWID L"sun_VBoxNetAdp"
#endif


static DECLCALLBACK(void) winNetCfgLogger(const char *pszString)
{
    RTMsgInfo("%s", pszString);
}

static int VBoxNetAdpUninstall(void)
{
    RTMsgInfo("Uninstalling all Host-Only interfaces ...");

    int rcExit = RTEXITCODE_FAILURE;
    VBoxNetCfgWinSetLogging(winNetCfgLogger);

    HRESULT hr = CoInitialize(NULL);
    if (hr == S_OK)
    {
        hr = VBoxNetCfgWinRemoveAllNetDevicesOfId(VBOX_NETADP_HWID);
        if (hr == S_OK)
        {
            hr = VBoxDrvCfgInfUninstallAllSetupDi(&GUID_DEVCLASS_NET, L"Net", VBOX_NETADP_HWID, 0/* could be SUOI_FORCEDELETE */);
            if (hr == S_OK)
                RTMsgInfo("Uninstalled successfully!");
            else
                RTMsgError("uninstalled successfully, but failed to remove infs (%Rhrc)\n", hr);
            rcExit = RTEXITCODE_SUCCESS;
        }
        else
            RTMsgError("uninstall failed: %Rhrc", hr);

        CoUninitialize();
    }
    else
        RTMsgError("Failed initializing COM: %Rhrc", hr);

    VBoxNetCfgWinSetLogging(NULL);

    return rcExit;
}

int __cdecl main(int argc, char **argv)
{
    RTR3InitExeNoArguments(0);
    if (argc != 1)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "This utility takes no arguments\n");
    NOREF(argv);

    return VBoxNetAdpUninstall();
}

