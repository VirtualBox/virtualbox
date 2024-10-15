/* $Id: ntDisplay.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Test cases for Display device and DirectX 3D rendering - NT.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

#include <d3d11.h>

#include <iprt/win/setupapi.h>
#include <devguid.h>

#include <iprt/initterm.h>
#include <iprt/getopt.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/errcore.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** How chatty we should be. */
static uint32_t g_cVerbosity = 0;

NTSTATUS SetDisplayDeviceState(bool bEnable)
{
    HDEVINFO hDevs = NULL;
    SP_DEVINFO_DATA DevInfo;
    NTSTATUS rcNt = (NTSTATUS)0;

    hDevs = SetupDiGetClassDevs(&GUID_DEVCLASS_DISPLAY, NULL, NULL, DIGCF_PRESENT | DIGCF_PROFILE);

    if (hDevs == INVALID_HANDLE_VALUE)
    {
        rcNt = GetLastError();
        RTMsgError("SetupDiGetClassDevs failed: %#x\n", rcNt);
        return rcNt;
    }

    RT_ZERO(DevInfo);
    DevInfo.cbSize = sizeof(SP_DEVINFO_DATA);

    if (SetupDiEnumDeviceInfo(hDevs, 0, &DevInfo))
    {
        SP_PROPCHANGE_PARAMS PropChangeParams;

        RT_ZERO(PropChangeParams);
        PropChangeParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        PropChangeParams.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
        PropChangeParams.StateChange = bEnable ? DICS_ENABLE : DICS_DISABLE;
        PropChangeParams.Scope = DICS_FLAG_CONFIGSPECIFIC;
        PropChangeParams.HwProfile = 0;

        if (SetupDiSetClassInstallParams(hDevs, &DevInfo, &PropChangeParams.ClassInstallHeader, sizeof(PropChangeParams)))
        {
            if (SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, hDevs, &DevInfo))
            {
                if (g_cVerbosity >= 1)
                    RTMsgInfo("debug: device %s\n", bEnable ? "enabled" : "disabled");
            }
            else
            {
                rcNt = GetLastError();
                RTMsgError("SetupDiCallClassInstaller failed: %#x\n", rcNt);
            }
        }
        else
        {
            rcNt = GetLastError();
            RTMsgError("SetupDiSetClassInstallParams failed: %#x\n", rcNt);
        }
    }

    SetupDiDestroyDeviceInfoList(hDevs);

    return rcNt;
}

bool CheckDXFeatureLevel()
{
    IDXGIAdapter *pAdapter = NULL; /* Default adapter. */
    static const D3D_FEATURE_LEVEL aFeatureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1
    };
    UINT Flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL FeatureLevel = D3D_FEATURE_LEVEL_9_1;
    ID3D11Device *pDevice = NULL;
    ID3D11DeviceContext *pImmediateContext = NULL;
    bool fResult = false;
    HRESULT hr = S_OK;

    hr = D3D11CreateDevice(pAdapter,
                           D3D_DRIVER_TYPE_HARDWARE,
                           NULL,
                           Flags,
                           aFeatureLevels,
                           RT_ELEMENTS(aFeatureLevels),
                           D3D11_SDK_VERSION,
                           &pDevice,
                           &FeatureLevel,
                           &pImmediateContext);

    if (FAILED(hr))
    {
        RTMsgError("D3D11CreateDevice failed with 0x%X\n", hr);
        return false;
    }

    if (FeatureLevel == D3D_FEATURE_LEVEL_11_1)
    {
        RTMsgInfo("D3D_FEATURE_LEVEL_11_1 is supported\n");
        fResult = true;
    }
    else
    {
        RTMsgError("D3D_FEATURE_LEVEL_11_1 is not supported, only 0x%X\n", FeatureLevel);
    }

    pDevice->Release();
    pImmediateContext->Release();

    return fResult;
}

int main(int argc, char **argv)
{
    /*
     * Init IPRT.
     */
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--enable",       'e',  RTGETOPT_REQ_UINT32  },
        { "--quiet",        'q',  RTGETOPT_REQ_NOTHING },
        { "--verbose",      'v',  RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTSTATE State;
    RTGetOptInit(&State, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    RTGETOPTUNION ValueUnion;
    int chOpt;
    while ((chOpt = RTGetOpt(&State, &ValueUnion)) != 0)
    {
        switch (chOpt)
        {
            case 'e': SetDisplayDeviceState(RT_BOOL(ValueUnion.u32)); break;
            case 'q': g_cVerbosity = 0; break;
            case 'v': g_cVerbosity += 1; break;
            case 'h':
                RTPrintf("usage: ntDisplay.exe [-e|--enable <0 or 1>]\n");
                return 0;

            default:
                return RTGetOptPrintError(chOpt, &ValueUnion);
        }
    }

    return !CheckDXFeatureLevel();
}
