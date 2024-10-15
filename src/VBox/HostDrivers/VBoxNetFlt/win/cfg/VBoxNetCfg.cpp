/* $Id: VBoxNetCfg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetCfg.cpp - Network Configuration API.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#define _WIN32_DCOM

#include "VBox/VBoxNetCfg-win.h"
#include "VBox/VBoxDrvCfg-win.h"

#include <devguid.h>
#include <regstr.h>
#include <iprt/win/shlobj.h>
#include <cfgmgr32.h>
#include <iprt/win/objbase.h>

#include <Wbemidl.h>

#include <iprt/win/winsock2.h>
#include <iprt/win/ws2tcpip.h>
#include <ws2ipdef.h>
#include <iprt/win/netioapi.h>
#include <iprt/win/iphlpapi.h>

#include <iprt/asm.h>
#include <iprt/assertcompile.h>
#include <iprt/mem.h>
#include <iprt/list.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include <VBox/com/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifndef Assert   /** @todo r=bird: where would this be defined? */
//# ifdef DEBUG
//#  define Assert(_expr) assert(_expr)
//# else
//#  define Assert(_expr) do{ }while (0)
//# endif
# define Assert _ASSERT
# define AssertMsg(expr, msg) do{}while (0)
#endif

#define NonStandardLog              DoLogging
#define NonStandardLogFlow(x)       DoLogging x

#define SetErrBreak(strAndArgs) \
    if (1) { \
        hrc = E_FAIL; \
        NonStandardLog strAndArgs; \
        bstrError.printfNoThrow strAndArgs; \
        break; \
    } else do {} while (0)


#define VBOXNETCFGWIN_NETADP_ID_SZ  "sun_VBoxNetAdp"
#define VBOXNETCFGWIN_NETADP_ID_WSZ RT_CONCAT(L,VBOXNETCFGWIN_NETADP_ID_SZ)
#define DRIVERHWID                  VBOXNETCFGWIN_NETADP_ID_WSZ

/* We assume the following name matches the device description in vboxnetadp6.inf */
#define HOSTONLY_ADAPTER_NAME_SZ    "VirtualBox Host-Only Ethernet Adapter"
#define HOSTONLY_ADAPTER_NAME_WSZ   RT_CONCAT(L,HOSTONLY_ADAPTER_NAME_SZ)

#define VBOX_CONNECTION_NAME_SZ     "VirtualBox Host-Only Network"
#define VBOX_CONNECTION_NAME_WSZ    RT_CONCAT(L,VBOX_CONNECTION_NAME_SZ)

#define VBOXNETCFGWIN_NETLWF_ID     L"oracle_VBoxNetLwf"



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static PFNVBOXNETCFGLOGGER volatile g_pfnLogger = NULL;

/*
 * Wrappers for HelpAPI functions
 */
typedef void FNINITIALIZEIPINTERFACEENTRY( _Inout_ PMIB_IPINTERFACE_ROW row);
typedef FNINITIALIZEIPINTERFACEENTRY *PFNINITIALIZEIPINTERFACEENTRY;

typedef NETIOAPI_API FNGETIPINTERFACEENTRY( _Inout_ PMIB_IPINTERFACE_ROW row);
typedef FNGETIPINTERFACEENTRY *PFNGETIPINTERFACEENTRY;

typedef NETIOAPI_API FNSETIPINTERFACEENTRY( _Inout_ PMIB_IPINTERFACE_ROW row);
typedef FNSETIPINTERFACEENTRY *PFNSETIPINTERFACEENTRY;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static PFNINITIALIZEIPINTERFACEENTRY    g_pfnInitializeIpInterfaceEntry = NULL;
static PFNGETIPINTERFACEENTRY           g_pfnGetIpInterfaceEntry        = NULL;
static PFNSETIPINTERFACEENTRY           g_pfnSetIpInterfaceEntry        = NULL;

static void DoLogging(const char *pszString, ...);

/*
 * Forward declaration for using vboxNetCfgWinSetupMetric()
 */
static HRESULT vboxNetCfgWinSetupMetric(IN NET_LUID *pLuid);
static HRESULT vboxNetCfgWinGetInterfaceLUID(IN HKEY hKey, OUT NET_LUID *pLUID);



static HRESULT vboxNetCfgWinINetCfgLock(IN INetCfg *pNetCfg,
                                        IN LPCWSTR pszwClientDescription,
                                        IN DWORD cmsTimeout,
                                        OUT LPWSTR *ppszwClientDescription)
{
    INetCfgLock *pLock;
    HRESULT hr = pNetCfg->QueryInterface(IID_INetCfgLock, (PVOID *)&pLock);
    if (FAILED(hr))
    {
        NonStandardLogFlow(("QueryInterface failed: %Rhrc\n", hr));
        return hr;
    }

    hr = pLock->AcquireWriteLock(cmsTimeout, pszwClientDescription, ppszwClientDescription);
    if (hr == S_FALSE)
        NonStandardLogFlow(("Write lock busy\n"));
    else if (FAILED(hr))
        NonStandardLogFlow(("AcquireWriteLock failed: %Rhrc\n", hr));

    pLock->Release();
    return hr;
}

static HRESULT vboxNetCfgWinINetCfgUnlock(IN INetCfg *pNetCfg)
{
    INetCfgLock *pLock;
    HRESULT hr = pNetCfg->QueryInterface(IID_INetCfgLock, (PVOID *)&pLock);
    if (FAILED(hr))
    {
        NonStandardLogFlow(("QueryInterface failed: %Rhrc\n", hr));
        return hr;
    }

    hr = pLock->ReleaseWriteLock();
    if (FAILED(hr))
        NonStandardLogFlow(("ReleaseWriteLock failed: %Rhrc\n", hr));

    pLock->Release();
    return hr;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinQueryINetCfg(OUT INetCfg **ppNetCfg,
                                                      IN BOOL fGetWriteLock,
                                                      IN LPCWSTR pszwClientDescription,
                                                      IN DWORD cmsTimeout,
                                                      OUT LPWSTR *ppszwClientDescription)
{
    INetCfg *pNetCfg = NULL;
    HRESULT hr = CoCreateInstance(CLSID_CNetCfg, NULL, CLSCTX_INPROC_SERVER, IID_INetCfg, (PVOID *)&pNetCfg);
    if (FAILED(hr))
    {
        NonStandardLogFlow(("CoCreateInstance failed: %Rhrc\n", hr));
        return hr;
    }

    if (fGetWriteLock)
    {
        hr = vboxNetCfgWinINetCfgLock(pNetCfg, pszwClientDescription, cmsTimeout, ppszwClientDescription);
        if (hr == S_FALSE)
        {
            NonStandardLogFlow(("Write lock is busy\n", hr));
            hr = NETCFG_E_NO_WRITE_LOCK;
        }
    }

    if (SUCCEEDED(hr))
    {
        hr = pNetCfg->Initialize(NULL);
        if (SUCCEEDED(hr))
        {
            *ppNetCfg = pNetCfg;
            return S_OK;
        }
        NonStandardLogFlow(("Initialize failed: %Rhrc\n", hr));
    }

    pNetCfg->Release();
    return hr;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinReleaseINetCfg(IN INetCfg *pNetCfg, IN BOOL fHasWriteLock)
{
    if (!pNetCfg) /* If network config has been released already, just bail out. */
    {
        NonStandardLogFlow(("Warning: No network config given but write lock is set to TRUE\n"));
        return S_OK;
    }

    HRESULT hr = pNetCfg->Uninitialize();
    if (FAILED(hr))
    {
        NonStandardLogFlow(("Uninitialize failed: %Rhrc\n", hr));
        /* Try to release the write lock below. */
    }

    if (fHasWriteLock)
    {
        HRESULT hr2 = vboxNetCfgWinINetCfgUnlock(pNetCfg);
        if (FAILED(hr2))
            NonStandardLogFlow(("vboxNetCfgWinINetCfgUnlock failed: %Rhrc\n", hr2));
        if (SUCCEEDED(hr))
            hr = hr2;
    }

    pNetCfg->Release();
    return hr;
}

static HRESULT vboxNetCfgWinGetComponentByGuidEnum(IEnumNetCfgComponent *pEnumNcc,
                                                   IN const GUID *pGuid,
                                                   OUT INetCfgComponent **ppNcc)
{
    HRESULT hr = pEnumNcc->Reset();
    if (FAILED(hr))
    {
        NonStandardLogFlow(("Reset failed: %Rhrc\n", hr));
        return hr;
    }

    INetCfgComponent *pNcc = NULL;
    while ((hr = pEnumNcc->Next(1, &pNcc, NULL)) == S_OK)
    {
        ULONG uComponentStatus = 0;
        hr = pNcc->GetDeviceStatus(&uComponentStatus);
        if (SUCCEEDED(hr))
        {
            if (uComponentStatus == 0)
            {
                GUID NccGuid;
                hr = pNcc->GetInstanceGuid(&NccGuid);

                if (SUCCEEDED(hr))
                {
                    if (NccGuid == *pGuid)
                    {
                        /* found the needed device */
                        *ppNcc = pNcc;
                        break;
                    }
                }
                else
                    NonStandardLogFlow(("GetInstanceGuid failed: %Rhrc\n", hr));
            }
        }

        pNcc->Release();
    }
    return hr;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinGetComponentByGuid(IN INetCfg *pNc,
                                                            IN const GUID *pguidClass,
                                                            IN const GUID * pComponentGuid,
                                                            OUT INetCfgComponent **ppncc)
{
    IEnumNetCfgComponent *pEnumNcc = NULL;
    HRESULT hr = pNc->EnumComponents(pguidClass, &pEnumNcc);
    if (SUCCEEDED(hr))
    {
        hr = vboxNetCfgWinGetComponentByGuidEnum(pEnumNcc, pComponentGuid, ppncc);
        if (hr == S_FALSE)
            NonStandardLogFlow(("Component not found\n"));
        else if (FAILED(hr))
            NonStandardLogFlow(("vboxNetCfgWinGetComponentByGuidEnum failed: %Rhrc\n", hr));
        pEnumNcc->Release();
    }
    else
        NonStandardLogFlow(("EnumComponents failed: %Rhrc\n", hr));
    return hr;
}

static HRESULT vboxNetCfgWinQueryInstaller(IN INetCfg *pNetCfg, IN const GUID *pguidClass, INetCfgClassSetup **ppSetup)
{
    HRESULT hr = pNetCfg->QueryNetCfgClass(pguidClass, IID_INetCfgClassSetup, (void **)ppSetup);
    if (FAILED(hr))
        NonStandardLogFlow(("QueryNetCfgClass failed: %Rhrc\n", hr));
    return hr;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinInstallComponent(IN INetCfg *pNetCfg, IN LPCWSTR pszwComponentId,
                                                          IN const GUID *pguidClass, OUT INetCfgComponent **ppComponent)
{
    INetCfgClassSetup *pSetup;
    HRESULT hr = vboxNetCfgWinQueryInstaller(pNetCfg, pguidClass, &pSetup);
    if (FAILED(hr))
    {
        NonStandardLogFlow(("vboxNetCfgWinQueryInstaller failed: %Rhrc\n", hr));
        return hr;
    }

    OBO_TOKEN Token;
    RT_ZERO(Token);
    Token.Type = OBO_USER;

    INetCfgComponent *pTempComponent = NULL;
    hr = pSetup->Install(pszwComponentId, &Token,
                         0,    /* IN DWORD dwSetupFlags */
                         0,    /* IN DWORD dwUpgradeFromBuildNo */
                         NULL, /* IN LPCWSTR pszwAnswerFile */
                         NULL, /* IN LPCWSTR pszwAnswerSections */
                         &pTempComponent);
    if (SUCCEEDED(hr))
    {
        if (pTempComponent != NULL)
        {
            /*
             *   Set default metric value of interface to fix multicast issue
             *   See @bugref{6379} for details.
             */
            HKEY    hKey = (HKEY)INVALID_HANDLE_VALUE;
            HRESULT hrc2 = pTempComponent->OpenParamKey(&hKey);

            /* Set default metric value for host-only interface only */
            if (   SUCCEEDED(hrc2)
                && hKey != (HKEY)INVALID_HANDLE_VALUE
                /* Original was weird: && wcsnicmp(pszwComponentId, VBOXNETCFGWIN_NETADP_ID_WSZ, 256) == 0) */
                && RTUtf16ICmpAscii(pszwComponentId, VBOXNETCFGWIN_NETADP_ID_SZ) == 0)
            {
                NET_LUID luid;
                hrc2 = vboxNetCfgWinGetInterfaceLUID(hKey, &luid);

                /* Close the key as soon as possible. See @bugref{7973}. */
                RegCloseKey(hKey);
                hKey = (HKEY)INVALID_HANDLE_VALUE;

                if (FAILED(hrc2))
                {
                    /*
                     *   The setting of Metric is not very important functionality,
                     *   So we will not break installation process due to this error.
                     */
                    NonStandardLogFlow(("VBoxNetCfgWinInstallComponent Warning! vboxNetCfgWinGetInterfaceLUID failed, default metric for new interface will not be set: %Rhrc\n", hrc2));
                }
                else
                {
                    hrc2 = vboxNetCfgWinSetupMetric(&luid);
                    if (FAILED(hrc2))
                    {
                        /*
                         *   The setting of Metric is not very important functionality,
                         *   So we will not break installation process due to this error.
                         */
                        NonStandardLogFlow(("VBoxNetCfgWinInstallComponent Warning! vboxNetCfgWinSetupMetric failed, default metric for new interface will not be set: %Rhrc\n", hrc2));
                    }
                }
            }
            if (hKey != (HKEY)INVALID_HANDLE_VALUE)
                RegCloseKey(hKey);
            if (ppComponent != NULL)
                *ppComponent = pTempComponent;
            else
                pTempComponent->Release();
        }

        /* ignore the apply failure */
        HRESULT hrc3 = pNetCfg->Apply();
        Assert(hrc3 == S_OK);
        if (hrc3 != S_OK)
            NonStandardLogFlow(("Apply failed: %Rhrc\n", hrc3));
    }
    else
        NonStandardLogFlow(("Install failed: %Rhrc\n", hr));

    pSetup->Release();
    return hr;
}

static HRESULT vboxNetCfgWinInstallInfAndComponent(IN INetCfg *pNetCfg, IN LPCWSTR pszwComponentId, IN const GUID *pguidClass,
                                                   IN LPCWSTR const *apwszInfPaths, IN UINT cInfPaths,
                                                   OUT INetCfgComponent **ppComponent)
{
    NonStandardLogFlow(("Installing %u INF files ...\n", cInfPaths));

    HRESULT hr              = S_OK;
    UINT    cFilesProcessed = 0;
    for (; cFilesProcessed < cInfPaths; cFilesProcessed++)
    {
        NonStandardLogFlow(("Installing INF file \"%ls\" ...\n", apwszInfPaths[cFilesProcessed]));
        hr = VBoxDrvCfgInfInstall(apwszInfPaths[cFilesProcessed]);
        if (FAILED(hr))
        {
            NonStandardLogFlow(("VBoxNetCfgWinInfInstall failed: %Rhrc\n", hr));
            break;
        }
    }

    if (SUCCEEDED(hr))
    {
        hr = VBoxNetCfgWinInstallComponent(pNetCfg, pszwComponentId, pguidClass, ppComponent);
        if (FAILED(hr))
            NonStandardLogFlow(("VBoxNetCfgWinInstallComponent failed: %Rhrc\n", hr));
    }

    if (FAILED(hr))
    {
        NonStandardLogFlow(("Installation failed, rolling back installation set ...\n"));

        do
        {
            HRESULT hr2 = VBoxDrvCfgInfUninstall(apwszInfPaths[cFilesProcessed], 0);
            if (FAILED(hr2))
                NonStandardLogFlow(("VBoxDrvCfgInfUninstall failed: %Rhrc\n", hr2));
                /* Keep going. */
            if (!cFilesProcessed)
                break;
        } while (cFilesProcessed--);

        NonStandardLogFlow(("Rollback complete\n"));
    }

    return hr;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinUninstallComponent(IN INetCfg *pNetCfg, IN INetCfgComponent *pComponent)
{
    GUID GuidClass;
    HRESULT hr = pComponent->GetClassGuid(&GuidClass);
    if (FAILED(hr))
    {
        NonStandardLogFlow(("GetClassGuid failed: %Rhrc\n", hr));
        return hr;
    }

    INetCfgClassSetup *pSetup = NULL;
    hr = vboxNetCfgWinQueryInstaller(pNetCfg, &GuidClass, &pSetup);
    if (FAILED(hr))
    {
        NonStandardLogFlow(("vboxNetCfgWinQueryInstaller failed: %Rhrc\n", hr));
        return hr;
    }

    OBO_TOKEN Token;
    RT_ZERO(Token);
    Token.Type = OBO_USER;

    hr = pSetup->DeInstall(pComponent, &Token, NULL /* OUT LPWSTR *pmszwRefs */);
    if (SUCCEEDED(hr))
    {
        hr = pNetCfg->Apply();
        if (FAILED(hr))
            NonStandardLogFlow(("Apply failed: %Rhrc\n", hr));
    }
    else
        NonStandardLogFlow(("DeInstall failed: %Rhrc\n", hr));

    if (pSetup)
        pSetup->Release();
    return hr;
}

typedef BOOL (*PFN_VBOXNETCFGWIN_NETCFGENUM_CALLBACK_T)(IN INetCfg *pNetCfg, IN INetCfgComponent *pNetCfgComponent,
                                                        PVOID pvContext);

static HRESULT vboxNetCfgWinEnumNetCfgComponents(IN INetCfg *pNetCfg,
                                                 IN const GUID *pguidClass,
                                                 PFN_VBOXNETCFGWIN_NETCFGENUM_CALLBACK_T pfnCallback,
                                                 PVOID pContext)
{
    IEnumNetCfgComponent *pEnumComponent = NULL;
    HRESULT hr = pNetCfg->EnumComponents(pguidClass, &pEnumComponent);
    if (SUCCEEDED(hr))
    {
        INetCfgComponent *pNetCfgComponent;
        hr = pEnumComponent->Reset();
        for (;;)
        {
            hr = pEnumComponent->Next(1, &pNetCfgComponent, NULL);
            if (hr == S_OK)
            {
//                ULONG uComponentStatus;
//                hr = pNcc->GetDeviceStatus(&uComponentStatus);
//                if (SUCCEEDED(hr))
                BOOL fResult = FALSE;
                if (pNetCfgComponent)
                {
                    fResult = pfnCallback(pNetCfg, pNetCfgComponent, pContext);
                    pNetCfgComponent->Release();
                }

                if (!fResult)
                    break;
            }
            else
            {
                if (hr == S_FALSE)
                    hr = S_OK; /* no more components */
                else
                    NonStandardLogFlow(("Next failed: %Rhrc\n", hr));
                break;
            }
        }
        pEnumComponent->Release();
    }
    return hr;
}

/** PFNVBOXNETCFGWINNETENUMCALLBACK */
static BOOL vboxNetCfgWinRemoveAllNetDevicesOfIdCallback(HDEVINFO hDevInfo, PSP_DEVINFO_DATA pDev, PVOID pvContext)
{
    RT_NOREF1(pvContext);

    SP_REMOVEDEVICE_PARAMS rmdParams;
    RT_ZERO(rmdParams);
    rmdParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    rmdParams.ClassInstallHeader.InstallFunction = DIF_REMOVE;
    rmdParams.Scope = DI_REMOVEDEVICE_GLOBAL;

    if (SetupDiSetClassInstallParams(hDevInfo,pDev,
                                     &rmdParams.ClassInstallHeader, sizeof(rmdParams)))
    {
        if (SetupDiSetSelectedDevice(hDevInfo, pDev))
        {
#ifndef VBOXNETCFG_DELAYEDRENAME
            /* Figure out NetCfgInstanceId. */
            HKEY hKey = SetupDiOpenDevRegKey(hDevInfo,
                                             pDev,
                                             DICS_FLAG_GLOBAL,
                                             0,
                                             DIREG_DRV,
                                             KEY_READ);
            if (hKey == INVALID_HANDLE_VALUE)
                NonStandardLogFlow(("vboxNetCfgWinRemoveAllNetDevicesOfIdCallback: SetupDiOpenDevRegKey failed with error %u\n",
                                    GetLastError()));
            else
            {
                WCHAR wszCfgGuidString[50] = { L'' };
                DWORD cbSize = sizeof(wszCfgGuidString) - sizeof(WCHAR); /* make sure we get a terminated string back */
                DWORD dwValueType = 0;
                LSTATUS lrc = RegQueryValueExW(hKey, L"NetCfgInstanceId", NULL, &dwValueType, (LPBYTE)wszCfgGuidString, &cbSize);
                if (lrc == ERROR_SUCCESS)
                {
                    /** @todo r=bird: original didn't check the type here, just assumed it was a
                     * valid zero terminated string. (zero term handled by -sizeof(WHCAR) above now). */
                    if (dwValueType == REG_SZ || dwValueType == REG_EXPAND_SZ || dwValueType == REG_EXPAND_SZ)
                    {
                        NonStandardLogFlow(("vboxNetCfgWinRemoveAllNetDevicesOfIdCallback: Processing device ID \"%ls\"\n",
                                            wszCfgGuidString));

                        /* Figure out device name. */
                        WCHAR wszDevName[256 + 1] = {0};
                        if (SetupDiGetDeviceRegistryPropertyW(hDevInfo, pDev, SPDRP_FRIENDLYNAME, NULL, (PBYTE)wszDevName,
                                                              sizeof(wszDevName) - sizeof(WCHAR) /* yes, in bytes */, NULL))
                        {
                            /*
                             * Rename the connection before removing the device. This will
                             * hopefully prevent an error when we will be attempting
                             * to rename a newly created connection (see @bugref{6740}).
                             */
                            WCHAR wszNewName[RT_ELEMENTS(wszDevName) + 128 /* ensure sufficient buffer */];
                            HRESULT hr = VBoxNetCfgWinGenHostonlyConnectionName(wszDevName, wszNewName,
                                                                                RT_ELEMENTS(wszNewName) - 10 /*removed++*/,
                                                                                NULL);
                            RTUtf16CatAscii(wszNewName, sizeof(wszNewName), " removed");
                            if (SUCCEEDED(hr))
                                hr = VBoxNetCfgWinRenameConnection(wszCfgGuidString, wszNewName);
                            //NonStandardLogFlow(("VBoxNetCfgWinRenameConnection(%S,%S) => 0x%x\n", wszCfgGuidString, TempName, hr_tmp));
                        }
                        else
                            NonStandardLogFlow(("vboxNetCfgWinRemoveAllNetDevicesOfIdCallback: Failed to get friendly name for device \"%ls\"\n",
                                                wszCfgGuidString));
                    }
                    else
                        NonStandardLogFlow(("vboxNetCfgWinRemoveAllNetDevicesOfIdCallback: Friendly name for \"%S\" isn't a string: %d\n",
                                            wszCfgGuidString, dwValueType
                }
                else
                    NonStandardLogFlow(("vboxNetCfgWinRemoveAllNetDevicesOfIdCallback: Querying instance ID failed with %u (%#x)\n",
                                        lrc, lrc));

                RegCloseKey(hKey);
            }
#endif /* VBOXNETCFG_DELAYEDRENAME */

            if (SetupDiCallClassInstaller(DIF_REMOVE, hDevInfo, pDev))
            {
                SP_DEVINSTALL_PARAMS_W DevParams = { sizeof(DevParams) };
                if (SetupDiGetDeviceInstallParams(hDevInfo, pDev, &DevParams))
                {
                    if (   (DevParams.Flags & DI_NEEDRESTART)
                        || (DevParams.Flags & DI_NEEDREBOOT))
                        NonStandardLog(("vboxNetCfgWinRemoveAllNetDevicesOfIdCallback: A reboot is required\n"));
                }
                else
                    NonStandardLogFlow(("vboxNetCfgWinRemoveAllNetDevicesOfIdCallback: SetupDiGetDeviceInstallParams failed with %u\n",
                                        GetLastError()));
            }
            else
                NonStandardLogFlow(("vboxNetCfgWinRemoveAllNetDevicesOfIdCallback: SetupDiCallClassInstaller failed with %u\n",
                                    GetLastError()));
        }
        else
            NonStandardLogFlow(("vboxNetCfgWinRemoveAllNetDevicesOfIdCallback: SetupDiSetSelectedDevice failed with %u\n",
                                GetLastError()));
    }
    else
        NonStandardLogFlow(("vboxNetCfgWinRemoveAllNetDevicesOfIdCallback: SetupDiSetClassInstallParams failed with %u\n",
                            GetLastError()));

    /* Continue enumeration. */
    return TRUE;
}

typedef struct VBOXNECTFGWINPROPCHANGE
{
    VBOXNECTFGWINPROPCHANGE_TYPE_T enmPcType;
    HRESULT hr;
} VBOXNECTFGWINPROPCHANGE, *PVBOXNECTFGWINPROPCHANGE;

static BOOL vboxNetCfgWinPropChangeAllNetDevicesOfIdCallback(HDEVINFO hDevInfo, PSP_DEVINFO_DATA pDev, PVOID pContext)
{
    PVBOXNECTFGWINPROPCHANGE pPc = (PVBOXNECTFGWINPROPCHANGE)pContext;

    SP_PROPCHANGE_PARAMS PcParams;
    RT_ZERO(PcParams);
    PcParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    PcParams.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    PcParams.Scope = DICS_FLAG_GLOBAL;

    switch (pPc->enmPcType)
    {
        case VBOXNECTFGWINPROPCHANGE_TYPE_DISABLE:
            PcParams.StateChange = DICS_DISABLE;
            NonStandardLogFlow(("vboxNetCfgWinPropChangeAllNetDevicesOfIdCallback: Change type (DICS_DISABLE): %d\n", pPc->enmPcType));
            break;
        case VBOXNECTFGWINPROPCHANGE_TYPE_ENABLE:
            PcParams.StateChange = DICS_ENABLE;
            NonStandardLogFlow(("vboxNetCfgWinPropChangeAllNetDevicesOfIdCallback: Change type (DICS_ENABLE): %d\n", pPc->enmPcType));
            break;
        default:
            NonStandardLogFlow(("vboxNetCfgWinPropChangeAllNetDevicesOfIdCallback: Unexpected prop change type: %d\n", pPc->enmPcType));
            pPc->hr = E_INVALIDARG;
            return FALSE;
    }

    if (SetupDiSetClassInstallParamsW(hDevInfo, pDev, &PcParams.ClassInstallHeader, sizeof(PcParams)))
    {
        if (SetupDiSetSelectedDevice(hDevInfo, pDev))
        {
            if (SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, hDevInfo, pDev))
            {
                SP_DEVINSTALL_PARAMS_W DevParams = { sizeof(DevParams) };
                if (SetupDiGetDeviceInstallParamsW(hDevInfo, pDev, &DevParams))
                {
                    if (   (DevParams.Flags & DI_NEEDRESTART)
                        || (DevParams.Flags & DI_NEEDREBOOT))
                        NonStandardLog(("vboxNetCfgWinPropChangeAllNetDevicesOfIdCallback: A reboot is required\n"));
                }
                else
                    NonStandardLogFlow(("vboxNetCfgWinPropChangeAllNetDevicesOfIdCallback: SetupDiGetDeviceInstallParams failed with %u\n",
                                        GetLastError()));
            }
            else
                NonStandardLogFlow(("vboxNetCfgWinPropChangeAllNetDevicesOfIdCallback: SetupDiCallClassInstaller failed with %u\n",
                                    GetLastError()));
        }
        else
            NonStandardLogFlow(("SetupDiSetSelectedDevice failed with %u\n", GetLastError()));
    }
    else
        NonStandardLogFlow(("SetupDiSetClassInstallParams failed with %u\n", GetLastError()));

    /* Continue enumeration. */
    return TRUE;
}

typedef BOOL (*PFNVBOXNETCFGWINNETENUMCALLBACK)(HDEVINFO hDevInfo, PSP_DEVINFO_DATA pDev, PVOID pContext);

static HRESULT vboxNetCfgWinEnumNetDevices(LPCWSTR pwszPnPId, PFNVBOXNETCFGWINNETENUMCALLBACK pfnCallback, PVOID pvContext)
{
    NonStandardLogFlow(("VBoxNetCfgWinEnumNetDevices: Searching for: %ls\n", pwszPnPId));

    HRESULT hr;
    HDEVINFO hDevInfo = SetupDiGetClassDevsExW(&GUID_DEVCLASS_NET,
                                               NULL,            /* IN PCTSTR Enumerator, OPTIONAL */
                                               NULL,            /* IN HWND hwndParent, OPTIONAL */
                                               DIGCF_PRESENT,   /* IN DWORD Flags,*/
                                               NULL,            /* IN HDEVINFO DeviceInfoSet, OPTIONAL */
                                               NULL,            /* IN PCTSTR MachineName, OPTIONAL */
                                               NULL             /* IN PVOID Reserved */);
    if (hDevInfo != INVALID_HANDLE_VALUE)
    {
        size_t const cwcPnPId = RTUtf16Len(pwszPnPId);
        DWORD        winEr    = NO_ERROR;
        DWORD        dwDevId  = 0;
        DWORD        cbBuffer = 0;
        PBYTE        pbBuffer = NULL;
        for (;;)
        {
            SP_DEVINFO_DATA Dev;
            memset(&Dev, 0, sizeof(SP_DEVINFO_DATA));
            Dev.cbSize = sizeof(SP_DEVINFO_DATA);

            if (!SetupDiEnumDeviceInfo(hDevInfo, dwDevId, &Dev))
            {
                winEr = GetLastError();
                if (winEr == ERROR_NO_MORE_ITEMS)
                    winEr = NO_ERROR;
                break;
            }

            NonStandardLogFlow(("VBoxNetCfgWinEnumNetDevices: Enumerating device %u ... \n", dwDevId));
            dwDevId++;

            DWORD cbRequired = 0;
            SetLastError(0);
            if (!SetupDiGetDeviceRegistryPropertyW(hDevInfo, &Dev,
                                                   SPDRP_HARDWAREID, /* IN DWORD Property */
                                                   NULL,             /* OUT PDWORD PropertyRegDataType OPTIONAL */
                                                   pbBuffer,         /* OUT PBYTE PropertyBuffer */
                                                   cbBuffer,         /* IN DWORD PropertyBufferSize */
                                                   &cbRequired       /* OUT PDWORD RequiredSize OPTIONAL */))
            {
                winEr = GetLastError();
                if (winEr != ERROR_INSUFFICIENT_BUFFER)
                {
                    if (winEr == ERROR_INVALID_DATA)
                    {
                        NonStandardLogFlow(("VBoxNetCfgWinEnumNetDevices: SetupDiGetDeviceRegistryPropertyW (1) failed with ERROR_INVALID_DATA - ignoring, skipping to next device\n"));
                        continue;
                    }
                    NonStandardLogFlow(("VBoxNetCfgWinEnumNetDevices: SetupDiGetDeviceRegistryPropertyW (1) failed with %u\n", winEr));
                    break;
                }
                winEr = NO_ERROR;

                cbBuffer = RT_ALIGN_32(cbRequired, 64);
                void *pvNew = RTMemRealloc(pbBuffer, cbBuffer);
                if (pvNew)
                    pbBuffer = (PBYTE)pvNew;
                else
                {
                    NonStandardLogFlow(("VBoxNetCfgWinEnumNetDevices: Out of memory allocating %u bytes\n", cbBuffer));
                    winEr = ERROR_OUTOFMEMORY;
                    break;
                }

                if (!SetupDiGetDeviceRegistryPropertyW(hDevInfo, &Dev,
                                                       SPDRP_HARDWAREID, /* IN DWORD Property */
                                                       NULL,             /* OUT PDWORD PropertyRegDataType, OPTIONAL */
                                                       pbBuffer,         /* OUT PBYTE PropertyBuffer */
                                                       cbBuffer,         /* IN DWORD PropertyBufferSize */
                                                       &cbRequired       /* OUT PDWORD RequiredSize OPTIONAL */))
                {
                    winEr = GetLastError();
                    NonStandardLogFlow(("VBoxNetCfgWinEnumNetDevices: SetupDiGetDeviceRegistryPropertyW (2) failed with %u\n",
                                        winEr));
                    break;
                }
            }

            PWSTR  pwszCurId = (PWSTR)pbBuffer;
            size_t cwcCurId  = RTUtf16Len(pwszCurId);

            NonStandardLogFlow(("VBoxNetCfgWinEnumNetDevices: Device %u: %ls\n", dwDevId, pwszCurId));

            if (cwcCurId >= cwcPnPId)
            {
                NonStandardLogFlow(("!RTUtf16NICmp(pwszCurId = (%ls), pwszPnPId = (%ls), cwcPnPId = (%d))\n", pwszCurId, pwszPnPId, cwcPnPId));

                pwszCurId += cwcCurId - cwcPnPId;
                if (!RTUtf16NICmp(pwszCurId, pwszPnPId, cwcPnPId))
                {
                    if (!pfnCallback(hDevInfo, &Dev, pvContext))
                        break;
                }
            }
        }

        NonStandardLogFlow(("VBoxNetCfgWinEnumNetDevices: Found %u devices total\n", dwDevId));

        if (pbBuffer)
            RTMemFree(pbBuffer);

        hr = HRESULT_FROM_WIN32(winEr);

        SetupDiDestroyDeviceInfoList(hDevInfo);
    }
    else
    {
        DWORD winEr = GetLastError();
        NonStandardLogFlow(("VBoxNetCfgWinEnumNetDevices: SetupDiGetClassDevsExW failed with %u\n", winEr));
        hr = HRESULT_FROM_WIN32(winEr);
    }

    NonStandardLogFlow(("VBoxNetCfgWinEnumNetDevices: Ended with hr (0x%x)\n", hr));
    return hr;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinRemoveAllNetDevicesOfId(IN LPCWSTR pwszPnPId)
{
    return vboxNetCfgWinEnumNetDevices(pwszPnPId, vboxNetCfgWinRemoveAllNetDevicesOfIdCallback, NULL);
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinPropChangeAllNetDevicesOfId(IN LPCWSTR pwszPnPId, VBOXNECTFGWINPROPCHANGE_TYPE_T enmPcType)
{
    VBOXNECTFGWINPROPCHANGE Pc;
    Pc.enmPcType = enmPcType;
    Pc.hr = S_OK;
    NonStandardLogFlow(("Calling VBoxNetCfgWinEnumNetDevices with pwszPnPId (= %ls) and vboxNetCfgWinPropChangeAllNetDevicesOfIdCallback\n", pwszPnPId));

    HRESULT hr = vboxNetCfgWinEnumNetDevices(pwszPnPId, vboxNetCfgWinPropChangeAllNetDevicesOfIdCallback, &Pc);
    if (!SUCCEEDED(hr))
    {
        NonStandardLogFlow(("VBoxNetCfgWinEnumNetDevices failed 0x%x\n", hr));
        return hr;
    }

    if (!SUCCEEDED(Pc.hr))
    {
        NonStandardLogFlow(("vboxNetCfgWinPropChangeAllNetDevicesOfIdCallback failed 0x%x\n", Pc.hr));
        return Pc.hr;
    }

    return S_OK;
}



/*********************************************************************************************************************************
*   Logging                                                                                                                      *
*********************************************************************************************************************************/

static void DoLogging(const char *pszString, ...)
{
    PFNVBOXNETCFGLOGGER pfnLogger = g_pfnLogger;
    if (pfnLogger)
    {
        char szBuffer[4096];
        va_list va;
        va_start(va, pszString);
        RTStrPrintfV(szBuffer, RT_ELEMENTS(szBuffer), pszString, va);
        va_end(va);

        pfnLogger(szBuffer);
    }
}

VBOXNETCFGWIN_DECL(void) VBoxNetCfgWinSetLogging(IN PFNVBOXNETCFGLOGGER pfnLogger)
{
    g_pfnLogger = pfnLogger;
}



/*********************************************************************************************************************************
*   IP configuration API                                                                                                         *
*********************************************************************************************************************************/
/* network settings config */
#if 1 /** @todo r=bird: Can't we replace this with VBox/com/ptr.h? */
/**
 *  Strong referencing operators. Used as a second argument to ComPtr<>/ComObjPtr<>.
 */
template<class C>
class ComStrongRef
{
protected:

    static void addref(C *p)  { p->AddRef(); }
    static void release(C *p) { p->Release(); }
};


/**
 *  Base template for smart COM pointers. Not intended to be used directly.
 */
template<class C, template<class> class RefOps = ComStrongRef>
class ComPtrBase : protected RefOps<C>
{
public:

    /* special template to disable AddRef()/Release() */
    template<class I>
    class NoAddRefRelease : public I
    {
    public:
        virtual ~NoAddRefRelease() { /* Make VC++ 19.2 happy. */ }
    private:
#ifndef VBOX_WITH_XPCOM
        STDMETHOD_(ULONG, AddRef)() = 0;
        STDMETHOD_(ULONG, Release)() = 0;
#else
        NS_IMETHOD_(nsrefcnt) AddRef(void) = 0;
        NS_IMETHOD_(nsrefcnt) Release(void) = 0;
#endif
    };

protected:

    ComPtrBase() : p(NULL) {}
    ComPtrBase(const ComPtrBase &that) : p(that.p) { addref(); }
    ComPtrBase(C *that_p) : p(that_p)              { addref(); }

    ~ComPtrBase() { release(); }

    ComPtrBase &operator=(const ComPtrBase &that)
    {
        safe_assign(that.p);
        return *this;
    }

    ComPtrBase &operator=(C *that_p)
    {
        safe_assign(that_p);
        return *this;
    }

public:

    void setNull()
    {
        release();
        p = NULL;
    }

    bool isNull() const
    {
        return (p == NULL);
    }

    bool operator!() const              { return isNull(); }

    bool operator<(C* that_p) const     { return p < that_p; }
    bool operator==(C* that_p) const    { return p == that_p; }

    template<class I>
    bool equalsTo(I *aThat) const
    {
        return ComPtrEquals(p, aThat);
    }

    template<class OC>
    bool equalsTo(const ComPtrBase<OC> &oc) const
    {
        return equalsTo((OC *) oc);
    }

    /** Intended to pass instances as in parameters to interface methods */
    operator C *() const { return p; }

    /**
     *  Dereferences the instance (redirects the -> operator to the managed
     *  pointer).
     */
    NoAddRefRelease<C> *operator->() const
    {
        AssertMsg (p, ("Managed pointer must not be null\n"));
        return (NoAddRefRelease <C> *) p;
    }

    template<class I>
    HRESULT queryInterfaceTo(I **pp) const
    {
        if (pp)
        {
            if (p)
                return p->QueryInterface(COM_IIDOF(I), (void **)pp);
            *pp = NULL;
            return S_OK;
        }
        return E_INVALIDARG;
    }

    /** Intended to pass instances as out parameters to interface methods */
    C **asOutParam()
    {
        setNull();
        return &p;
    }

private:

    void addref()
    {
        if (p)
            RefOps<C>::addref(p);
    }

    void release()
    {
        if (p)
            RefOps<C>::release(p);
    }

    void safe_assign(C *that_p)
    {
        /* be aware of self-assignment */
        if (that_p)
            RefOps<C>::addref(that_p);
        release();
        p = that_p;
    }

    C *p;
};

/**
 *  Smart COM pointer wrapper that automatically manages refcounting of
 *  interface pointers.
 *
 *  @param I    COM interface class
 */
template<class I, template<class> class RefOps = ComStrongRef>
class ComPtr : public ComPtrBase<I, RefOps>
{
    typedef ComPtrBase<I, RefOps> Base;

public:

    ComPtr() : Base() {}
    ComPtr(const ComPtr &that) : Base(that) {}
    ComPtr&operator= (const ComPtr &that)
    {
        Base::operator=(that);
        return *this;
    }

    template<class OI>
    ComPtr(OI *that_p) : Base () { operator=(that_p); }

    /* specialization for I */
    ComPtr(I *that_p) : Base (that_p) {}

    template <class OC>
    ComPtr(const ComPtr<OC, RefOps> &oc) : Base() { operator=((OC *) oc); }

    template<class OI>
    ComPtr &operator=(OI *that_p)
    {
        if (that_p)
            that_p->QueryInterface(COM_IIDOF(I), (void **)Base::asOutParam());
        else
            Base::setNull();
        return *this;
    }

    /* specialization for I */
    ComPtr &operator=(I *that_p)
    {
        Base::operator=(that_p);
        return *this;
    }

    template<class OC>
    ComPtr &operator=(const ComPtr<OC, RefOps> &oc)
    {
        return operator=((OC *) oc);
    }
};
#endif

static HRESULT netIfWinFindAdapterClassById(IWbemServices *pSvc, const GUID *pGuid, IWbemClassObject **pAdapterConfig)
{
    HRESULT hr;

    WCHAR wszGuid[50];
    int cwcGuid = StringFromGUID2(*pGuid, wszGuid, RT_ELEMENTS(wszGuid));
    if (cwcGuid)
    {
        com::BstrFmt bstrQuery("SELECT * FROM Win32_NetworkAdapterConfiguration WHERE SettingID = \"%ls\"", wszGuid);
        IEnumWbemClassObject* pEnumerator = NULL;
        hr = pSvc->ExecQuery(com::Bstr("WQL").raw(), bstrQuery.raw(), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                             NULL, &pEnumerator);
        if (SUCCEEDED(hr))
        {
            if (pEnumerator)
            {
                IWbemClassObject *pclsObj = NULL;
                ULONG uReturn = 0;
                hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
                NonStandardLogFlow(("netIfWinFindAdapterClassById: IEnumWbemClassObject::Next -> hr=0x%x pclsObj=%p uReturn=%u 42=%u\n",
                                    hr, (void *)pclsObj, uReturn, 42));
                if (SUCCEEDED(hr))
                {
                    if (uReturn && pclsObj != NULL)
                    {
                        *pAdapterConfig = pclsObj;
                        pEnumerator->Release();
                        NonStandardLogFlow(("netIfWinFindAdapterClassById: S_OK and %p\n", *pAdapterConfig));
                        return S_OK;
                    }
                    hr = E_FAIL;
                }
                pEnumerator->Release();
            }
            else
            {
                NonStandardLogFlow(("ExecQuery returned no enumerator\n"));
                hr = E_FAIL;
            }
        }
        else
            NonStandardLogFlow(("ExecQuery failed (0x%x)\n", hr));
    }
    else
    {
        DWORD winEr = GetLastError();
        hr = HRESULT_FROM_WIN32(winEr);
        if (SUCCEEDED(hr))
            hr = E_FAIL;
        NonStandardLogFlow(("StringFromGUID2 failed winEr=%u, hr=0x%x\n", winEr, hr));
    }

    NonStandardLogFlow(("netIfWinFindAdapterClassById: 0x%x and %p\n", hr, *pAdapterConfig));
    return hr;
}

static HRESULT netIfWinIsHostOnly(IWbemClassObject *pAdapterConfig, BOOL *pfIsHostOnly)
{
    VARIANT vtServiceName;
    VariantInit(&vtServiceName);

    HRESULT hr = pAdapterConfig->Get(L"ServiceName", 0 /*lFlags*/, &vtServiceName, NULL /*pvtType*/, NULL /*plFlavor*/);
    if (SUCCEEDED(hr))
    {
        *pfIsHostOnly = RTUtf16CmpAscii(vtServiceName.bstrVal, "VBoxNetAdp") == 0;

        VariantClear(&vtServiceName);
    }

    return hr;
}

static HRESULT netIfWinGetIpSettings(IWbemClassObject * pAdapterConfig, ULONG *pIpv4, ULONG *pMaskv4)
{
    *pIpv4   = 0;
    *pMaskv4 = 0;

    VARIANT vtIp;
    VariantInit(&vtIp);
    HRESULT hr = pAdapterConfig->Get(L"IPAddress", 0, &vtIp, 0, 0);
    if (SUCCEEDED(hr))
    {
        if (vtIp.vt == (VT_ARRAY | VT_BSTR))
        {
            VARIANT vtMask;
            VariantInit(&vtMask);
            hr = pAdapterConfig->Get(L"IPSubnet", 0, &vtMask, 0, 0);
            if (SUCCEEDED(hr))
            {
                if (vtMask.vt == (VT_ARRAY | VT_BSTR))
                {
                    SAFEARRAY *pIpArray   = vtIp.parray;
                    SAFEARRAY *pMaskArray = vtMask.parray;
                    if (pIpArray && pMaskArray)
                    {
                        BSTR pBstrCurIp;
                        BSTR pBstrCurMask;
                        for (LONG i = 0;
                                SafeArrayGetElement(pIpArray,  &i,  (PVOID)&pBstrCurIp)   == S_OK
                             && SafeArrayGetElement(pMaskArray, &i, (PVOID)&pBstrCurMask) == S_OK;
                             i++)
                        {
                            com::Utf8Str strIp(pBstrCurIp);
                            ULONG Ipv4 = inet_addr(strIp.c_str());
                            if (Ipv4 != INADDR_NONE)
                            {
                                *pIpv4 = Ipv4;

                                com::Utf8Str strMask(pBstrCurMask);
                                *pMaskv4 = inet_addr(strMask.c_str());
                                break;
                            }
                        }
                    }
                }
                VariantClear(&vtMask);
            }
        }
        VariantClear(&vtIp);
    }

    return hr;
}

#if 0 /* unused */

static HRESULT netIfWinHasIpSettings(IWbemClassObject * pAdapterConfig, SAFEARRAY * pCheckIp, SAFEARRAY * pCheckMask, bool *pFound)
{
    VARIANT vtIp;
    HRESULT hr;
    VariantInit(&vtIp);

    *pFound = false;

    hr = pAdapterConfig->Get(L"IPAddress", 0, &vtIp, 0, 0);
    if (SUCCEEDED(hr))
    {
        VARIANT vtMask;
        VariantInit(&vtMask);
        hr = pAdapterConfig->Get(L"IPSubnet", 0, &vtMask, 0, 0);
        if (SUCCEEDED(hr))
        {
            SAFEARRAY * pIpArray = vtIp.parray;
            SAFEARRAY * pMaskArray = vtMask.parray;
            if (pIpArray && pMaskArray)
            {
                BSTR pIp, pMask;
                for (LONG k = 0;
                    SafeArrayGetElement(pCheckIp, &k, (PVOID)&pIp) == S_OK
                    && SafeArrayGetElement(pCheckMask, &k, (PVOID)&pMask) == S_OK;
                    k++)
                {
                    BSTR pCurIp;
                    BSTR pCurMask;
                    for (LONG i = 0;
                        SafeArrayGetElement(pIpArray, &i, (PVOID)&pCurIp) == S_OK
                        && SafeArrayGetElement(pMaskArray, &i, (PVOID)&pCurMask) == S_OK;
                        i++)
                    {
                        if (!wcsicmp(pCurIp, pIp))
                        {
                            if (!wcsicmp(pCurMask, pMask))
                                *pFound = true;
                            break;
                        }
                    }
                }
            }


            VariantClear(&vtMask);
        }

        VariantClear(&vtIp);
    }

    return hr;
}

static HRESULT netIfWinWaitIpSettings(IWbemServices *pSvc, const GUID * pGuid, SAFEARRAY * pCheckIp, SAFEARRAY * pCheckMask, ULONG sec2Wait, bool *pFound)
{
    /* on Vista we need to wait for the address to get applied */
    /* wait for the address to appear in the list */
    HRESULT hr = S_OK;
    ULONG i;
    *pFound = false;
    ComPtr<IWbemClassObject> pAdapterConfig;
    for (i = 0;
            (hr = netIfWinFindAdapterClassById(pSvc, pGuid, pAdapterConfig.asOutParam())) == S_OK
         && (hr = netIfWinHasIpSettings(pAdapterConfig, pCheckIp, pCheckMask, pFound)) == S_OK
         && !(*pFound)
         && i < sec2Wait/6;
         i++)
    {
        Sleep(6000);
    }

    return hr;
}

#endif /* unused */

static HRESULT netIfWinCreateIWbemServices(IWbemServices **ppSvc)
{
    IWbemLocator *pLoc = NULL;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID *)&pLoc);
    if (SUCCEEDED(hr))
    {
        IWbemServices *pSvc = NULL;
        hr = pLoc->ConnectServer(com::Bstr(L"ROOT\\CIMV2").raw(), /* [in] const BSTR strNetworkResource */
                                 NULL, /* [in] const BSTR strUser */
                                 NULL, /* [in] const BSTR strPassword */
                                 0,    /* [in] const BSTR strLocale */
                                 NULL, /* [in] LONG lSecurityFlags */
                                 0,    /* [in] const BSTR strAuthority */
                                 0,    /* [in] IWbemContext* pCtx */
                                 &pSvc /* [out] IWbemServices** ppNamespace */);
        if (SUCCEEDED(hr))
        {
            hr = CoSetProxyBlanket(pSvc, /* IUnknown * pProxy */
                                   RPC_C_AUTHN_WINNT, /* DWORD dwAuthnSvc */
                                   RPC_C_AUTHZ_NONE, /* DWORD dwAuthzSvc */
                                   NULL, /* WCHAR * pServerPrincName */
                                   RPC_C_AUTHN_LEVEL_CALL, /* DWORD dwAuthnLevel */
                                   RPC_C_IMP_LEVEL_IMPERSONATE, /* DWORD dwImpLevel */
                                   NULL, /* RPC_AUTH_IDENTITY_HANDLE pAuthInfo */
                                   EOAC_NONE /* DWORD dwCapabilities */
                                   );
            if (SUCCEEDED(hr))
            {
                *ppSvc = pSvc;
                /* do not need it any more */
                pLoc->Release();
                return hr;
            }

            NonStandardLogFlow(("CoSetProxyBlanket failed: %Rhrc\n", hr));
            pSvc->Release();
        }
        else
            NonStandardLogFlow(("ConnectServer failed: %Rhrc\n", hr));
        pLoc->Release();
    }
    else
        NonStandardLogFlow(("CoCreateInstance failed: %Rhrc\n", hr));
    return hr;
}

static HRESULT netIfWinAdapterConfigPath(IWbemClassObject *pObj, com::Bstr *pRet)
{
    VARIANT index;
    VariantInit(&index);
    HRESULT hr = pObj->Get(L"Index", 0, &index, 0, 0);
    if (SUCCEEDED(hr))
        hr = pRet->printfNoThrow("Win32_NetworkAdapterConfiguration.Index='%u'", index.uintVal);
    else
    {
        pRet->setNull();
        NonStandardLogFlow(("Get failed: %Rhrc\n", hr));
    }
    return hr;
}

static HRESULT netIfExecMethod(IWbemServices * pSvc, IWbemClassObject *pClass, com::Bstr const &rObjPath,
                               const char *pszMethodName, LPCWSTR *papwszArgNames, LPVARIANT *pArgs, UINT cArgs,
                               IWbemClassObject **ppOutParams)
{
    *ppOutParams = NULL;
    com::Bstr bstrMethodName;
    HRESULT hr = bstrMethodName.assignEx(pszMethodName);
    if (SUCCEEDED(hr))
    {
        ComPtr<IWbemClassObject> pInParamsDefinition;
        ComPtr<IWbemClassObject> pClassInstance;
        if (cArgs)
        {
            hr = pClass->GetMethod(bstrMethodName.raw(), 0, pInParamsDefinition.asOutParam(), NULL);
            if (SUCCEEDED(hr))
            {
                hr = pInParamsDefinition->SpawnInstance(0, pClassInstance.asOutParam());
                if (SUCCEEDED(hr))
                {
                    for (UINT i = 0; i < cArgs; i++)
                    {
                        hr = pClassInstance->Put(papwszArgNames[i], 0, pArgs[i], 0);
                        if (FAILED(hr))
                            break;
                    }
                }
            }
        }

        if (SUCCEEDED(hr))
        {
            IWbemClassObject *pOutParams = NULL;
            hr = pSvc->ExecMethod(rObjPath.raw(), bstrMethodName.raw(), 0, NULL, pClassInstance, &pOutParams, NULL);
            if (SUCCEEDED(hr))
                *ppOutParams = pOutParams;
        }
    }

    return hr;
}

static HRESULT netIfWinCreateIpArray(SAFEARRAY **ppArray, in_addr const *paIps, UINT cIps)
{
    HRESULT    hr = S_OK;
    SAFEARRAY *pIpArray = SafeArrayCreateVector(VT_BSTR, 0, cIps);
    if (pIpArray)
    {
        for (UINT i = 0; i < cIps; i++)
        {
            com::Bstr bstrVal;
            hr = bstrVal.printfNoThrow("%RTnaipv4", paIps[i].s_addr);
            if (SUCCEEDED(hr))
            {
                Assert(bstrVal.equals(inet_ntoa(paIps[i])));

                BSTR pRawVal;
                hr = bstrVal.detachToEx(&pRawVal);
                if (SUCCEEDED(hr))
                {
                    LONG aIndex[1] = { (LONG)i };
                    hr = SafeArrayPutElement(pIpArray, aIndex, pRawVal);
                    if (SUCCEEDED(hr))
                        continue;
                    SysFreeString(pRawVal);
                }
            }
            break;
        }

        if (SUCCEEDED(hr))
            *ppArray = pIpArray;
        else
            SafeArrayDestroy(pIpArray);
    }
    else
        hr = HRESULT_FROM_WIN32(GetLastError());
    return hr;
}

#if 0 /* unused */
static HRESULT netIfWinCreateIpArrayV4V6(SAFEARRAY **ppArray, BSTR Ip)
{
    HRESULT hr;
    SAFEARRAY *pIpArray = SafeArrayCreateVector(VT_BSTR, 0, 1);
    if (pIpArray)
    {
        BSTR val = com::Bstr(Ip, false).copy();
        long aIndex[1];
        aIndex[0] = 0;
        hr = SafeArrayPutElement(pIpArray, aIndex, val);
        if (FAILED(hr))
        {
            SysFreeString(val);
            SafeArrayDestroy(pIpArray);
        }

        if (SUCCEEDED(hr))
        {
            *ppArray = pIpArray;
        }
    }
    else
        hr = HRESULT_FROM_WIN32(GetLastError());

    return hr;
}
#endif


static HRESULT netIfWinCreateIpArrayVariantV4(VARIANT *pIpAddresses, in_addr const *paIps, UINT cIps)
{
    VariantInit(pIpAddresses);
    pIpAddresses->vt = VT_ARRAY | VT_BSTR;

    SAFEARRAY *pIpArray;
    HRESULT hr = netIfWinCreateIpArray(&pIpArray, paIps, cIps);
    if (SUCCEEDED(hr))
        pIpAddresses->parray = pIpArray;
    return hr;
}

#if 0 /* unused */
static HRESULT netIfWinCreateIpArrayVariantV4V6(VARIANT * pIpAddresses, BSTR Ip)
{
    HRESULT hr;
    VariantInit(pIpAddresses);
    pIpAddresses->vt = VT_ARRAY | VT_BSTR;
    SAFEARRAY *pIpArray;
    hr = netIfWinCreateIpArrayV4V6(&pIpArray, Ip);
    if (SUCCEEDED(hr))
    {
        pIpAddresses->parray = pIpArray;
    }
    return hr;
}
#endif

static HRESULT netIfWinEnableStatic(IWbemServices *pSvc, const GUID *pGuid, com::Bstr &rObjPath, VARIANT *pIp, VARIANT *pMask)
{
    com::Bstr bstrClassName;
    HRESULT hr = bstrClassName.assignEx("Win32_NetworkAdapterConfiguration");
    if (SUCCEEDED(hr))
    {
        ComPtr<IWbemClassObject> pClass;
        hr = pSvc->GetObject(bstrClassName.raw(), 0, NULL, pClass.asOutParam(), NULL);
        if (SUCCEEDED(hr))
        {
            LPCWSTR argNames[] = {L"IPAddress", L"SubnetMask"};
            LPVARIANT   args[] = {         pIp,        pMask };

            ComPtr<IWbemClassObject> pOutParams;
            hr = netIfExecMethod(pSvc, pClass, rObjPath.raw(), "EnableStatic", argNames, args,
                                 2, pOutParams.asOutParam());
            if (SUCCEEDED(hr))
            {
                com::Bstr bstrReturnValue;
                hr = bstrReturnValue.assignEx("ReturnValue");
                if (SUCCEEDED(hr))
                {
                    VARIANT varReturnValue;
                    VariantInit(&varReturnValue);
                    hr = pOutParams->Get(bstrReturnValue.raw(), 0, &varReturnValue, NULL, 0);
                    Assert(SUCCEEDED(hr));
                    if (SUCCEEDED(hr))
                    {
                        //Assert(varReturnValue.vt == VT_UINT);
                        int winEr = varReturnValue.uintVal;
                        switch (winEr)
                        {
                            case 0:
                            {
                                hr = S_OK;
                                //bool bFound;
                                //HRESULT tmpHr = netIfWinWaitIpSettings(pSvc, pGuid, pIp->parray, pMask->parray, 180, &bFound);
                                NOREF(pGuid);
                                break;
                            }
                            default:
                                hr = HRESULT_FROM_WIN32( winEr );
                                break;
                        }
                    }
                }
            }
        }
    }
    return hr;
}


static HRESULT netIfWinEnableStaticV4(IWbemServices *pSvc, const GUID *pGuid, com::Bstr &rObjPath,
                                      in_addr const *paIps, in_addr const *paMasks, UINT cIpAndMasks)
{
    VARIANT ipAddresses;
    HRESULT hr = netIfWinCreateIpArrayVariantV4(&ipAddresses, paIps, cIpAndMasks);
    if (SUCCEEDED(hr))
    {
        VARIANT ipMasks;
        hr = netIfWinCreateIpArrayVariantV4(&ipMasks, paMasks, cIpAndMasks);
        if (SUCCEEDED(hr))
        {
            hr = netIfWinEnableStatic(pSvc, pGuid, rObjPath, &ipAddresses, &ipMasks);
            VariantClear(&ipMasks);
        }
        VariantClear(&ipAddresses);
    }
    return hr;
}

#if 0 /* unused */

static HRESULT netIfWinEnableStaticV4V6(IWbemServices * pSvc, const GUID * pGuid, BSTR ObjPath, BSTR Ip, BSTR Mask)
{
    VARIANT ipAddresses;
    HRESULT hr = netIfWinCreateIpArrayVariantV4V6(&ipAddresses, Ip);
    if (SUCCEEDED(hr))
    {
        VARIANT ipMasks;
        hr = netIfWinCreateIpArrayVariantV4V6(&ipMasks, Mask);
        if (SUCCEEDED(hr))
        {
            hr = netIfWinEnableStatic(pSvc, pGuid, ObjPath, &ipAddresses, &ipMasks);
            VariantClear(&ipMasks);
        }
        VariantClear(&ipAddresses);
    }
    return hr;
}

/* win API allows to set gw metrics as well, we are not setting them */
static HRESULT netIfWinSetGateways(IWbemServices * pSvc, BSTR ObjPath, VARIANT * pGw)
{
    ComPtr<IWbemClassObject> pClass;
    BSTR ClassName = SysAllocString(L"Win32_NetworkAdapterConfiguration");
    HRESULT hr;
    if (ClassName)
    {
        hr = pSvc->GetObject(ClassName, 0, NULL, pClass.asOutParam(), NULL);
        if (SUCCEEDED(hr))
        {
            LPCWSTR argNames[] = {L"DefaultIPGateway"};
            LPVARIANT args[]   = {pGw};
            ComPtr<IWbemClassObject> pOutParams;

            hr = netIfExecMethod(pSvc, pClass, ObjPath, com::Bstr(L"SetGateways"), argNames, args, 1, pOutParams.asOutParam());
            if (SUCCEEDED(hr))
            {
                VARIANT varReturnValue;
                hr = pOutParams->Get(com::Bstr(L"ReturnValue"), 0, &varReturnValue, NULL, 0);
                Assert(SUCCEEDED(hr));
                if (SUCCEEDED(hr))
                {
//                    Assert(varReturnValue.vt == VT_UINT);
                    int winEr = varReturnValue.uintVal;
                    switch (winEr)
                    {
                    case 0:
                        hr = S_OK;
                        break;
                    default:
                        hr = HRESULT_FROM_WIN32( winEr );
                        break;
                    }
                }
            }
        }
        SysFreeString(ClassName);
    }
    else
        hr = HRESULT_FROM_WIN32(GetLastError());

    return hr;
}

/* win API allows to set gw metrics as well, we are not setting them */
static HRESULT netIfWinSetGatewaysV4(IWbemServices * pSvc, BSTR ObjPath, in_addr* aGw, UINT cGw)
{
    VARIANT gwais;
    HRESULT hr = netIfWinCreateIpArrayVariantV4(&gwais, aGw, cGw);
    if (SUCCEEDED(hr))
    {
        netIfWinSetGateways(pSvc, ObjPath, &gwais);
        VariantClear(&gwais);
    }
    return hr;
}

/* win API allows to set gw metrics as well, we are not setting them */
static HRESULT netIfWinSetGatewaysV4V6(IWbemServices * pSvc, BSTR ObjPath, BSTR Gw)
{
    VARIANT vGw;
    HRESULT hr = netIfWinCreateIpArrayVariantV4V6(&vGw, Gw);
    if (SUCCEEDED(hr))
    {
        netIfWinSetGateways(pSvc, ObjPath, &vGw);
        VariantClear(&vGw);
    }
    return hr;
}

#endif /* unused */

static HRESULT netIfWinEnableDHCP(IWbemServices * pSvc, const com::Bstr &rObjPath)
{
    com::Bstr bstrClassName;
    HRESULT hr = bstrClassName.assignEx("Win32_NetworkAdapterConfiguration");
    if (SUCCEEDED(hr))
    {
        ComPtr<IWbemClassObject> pClass;
        hr = pSvc->GetObject(bstrClassName.raw(), 0, NULL, pClass.asOutParam(), NULL);
        if (SUCCEEDED(hr))
        {
            ComPtr<IWbemClassObject> pOutParams;
            hr = netIfExecMethod(pSvc, pClass, rObjPath, "EnableDHCP", NULL, NULL, 0, pOutParams.asOutParam());
            if (SUCCEEDED(hr))
            {
                com::Bstr bstrReturnValue;
                hr = bstrReturnValue.assignEx("ReturnValue");
                if (SUCCEEDED(hr))
                {
                    VARIANT varReturnValue;
                    VariantInit(&varReturnValue);
                    hr = pOutParams->Get(bstrReturnValue.raw(), 0, &varReturnValue, NULL, 0);
                    Assert(SUCCEEDED(hr));
                    if (SUCCEEDED(hr))
                    {
                        //Assert(varReturnValue.vt == VT_UINT);
                        int winEr = varReturnValue.uintVal;
                        switch (winEr)
                        {
                            case 0:
                                hr = S_OK;
                                break;
                            default:
                                hr = HRESULT_FROM_WIN32( winEr );
                                break;
                        }
                    }
                }
            }
        }
    }
    return hr;
}

static HRESULT netIfWinDhcpRediscover(IWbemServices * pSvc, const com::Bstr &rObjPath)
{
    com::Bstr bstrClassName;
    HRESULT hr = bstrClassName.assignEx("Win32_NetworkAdapterConfiguration");
    if (SUCCEEDED(hr))
    {
        ComPtr<IWbemClassObject> pClass;
        hr = pSvc->GetObject(bstrClassName.raw(), 0, NULL, pClass.asOutParam(), NULL);
        if (SUCCEEDED(hr))
        {
            ComPtr<IWbemClassObject> pOutParams;
            hr = netIfExecMethod(pSvc, pClass, rObjPath, "ReleaseDHCPLease", NULL, NULL, 0, pOutParams.asOutParam());
            if (SUCCEEDED(hr))
            {
                com::Bstr bstrReturnValue;
                hr = bstrReturnValue.assignEx("ReturnValue");
                if (SUCCEEDED(hr))
                {
                    VARIANT varReturnValue;
                    VariantInit(&varReturnValue);
                    hr = pOutParams->Get(bstrReturnValue.raw(), 0, &varReturnValue, NULL, 0);
                    Assert(SUCCEEDED(hr));
                    if (SUCCEEDED(hr))
                    {
                        //Assert(varReturnValue.vt == VT_UINT);
                        int winEr = varReturnValue.uintVal;
                        if (winEr == 0)
                        {
                            hr = netIfExecMethod(pSvc, pClass, rObjPath, "RenewDHCPLease", NULL, NULL, 0, pOutParams.asOutParam());
                            if (SUCCEEDED(hr))
                            {
                                hr = pOutParams->Get(bstrReturnValue.raw(), 0, &varReturnValue, NULL, 0);
                                Assert(SUCCEEDED(hr));
                                if (SUCCEEDED(hr))
                                {
                                    //Assert(varReturnValue.vt == VT_UINT);
                                    winEr = varReturnValue.uintVal;
                                    if (winEr == 0)
                                        hr = S_OK;
                                    else
                                        hr = HRESULT_FROM_WIN32( winEr );
                                }
                            }
                        }
                        else
                            hr = HRESULT_FROM_WIN32( winEr );
                    }
                }
            }
        }
    }

    return hr;
}

static HRESULT vboxNetCfgWinIsDhcpEnabled(IWbemClassObject *pAdapterConfig, BOOL *pfEnabled)
{
    VARIANT vtEnabled;
    VariantInit(&vtEnabled);
    HRESULT hr = pAdapterConfig->Get(L"DHCPEnabled", 0, &vtEnabled, 0, 0);
    if (SUCCEEDED(hr))
        *pfEnabled = vtEnabled.boolVal;
    else
        *pfEnabled = FALSE;
    return hr;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinGetAdapterSettings(IN const GUID *pGuid, OUT PADAPTER_SETTINGS pSettings)
{
    ComPtr<IWbemServices> pSvc;
    HRESULT hr = netIfWinCreateIWbemServices(pSvc.asOutParam());
    if (SUCCEEDED(hr))
    {
        ComPtr<IWbemClassObject> pAdapterConfig;
        hr = netIfWinFindAdapterClassById(pSvc, pGuid, pAdapterConfig.asOutParam());
        if (SUCCEEDED(hr))
        {
            hr = vboxNetCfgWinIsDhcpEnabled(pAdapterConfig, &pSettings->bDhcp);
            if (SUCCEEDED(hr))
                hr = netIfWinGetIpSettings(pAdapterConfig, &pSettings->ip, &pSettings->mask);
        }
    }

    return hr;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinIsDhcpEnabled(const GUID * pGuid, BOOL *pEnabled)
{
    ComPtr<IWbemServices> pSvc;
    HRESULT hr = netIfWinCreateIWbemServices(pSvc.asOutParam());
    if (SUCCEEDED(hr))
    {
        ComPtr<IWbemClassObject> pAdapterConfig;
        hr = netIfWinFindAdapterClassById(pSvc, pGuid, pAdapterConfig.asOutParam());
        if (SUCCEEDED(hr))
        {
            VARIANT vtEnabled;
            hr = pAdapterConfig->Get(L"DHCPEnabled", 0, &vtEnabled, 0, 0);
            if (SUCCEEDED(hr))
                *pEnabled = vtEnabled.boolVal;
        }
    }

    return hr;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinEnableStaticIpConfig(IN const GUID *pGuid, IN ULONG ip, IN ULONG mask)
{
    NonStandardLogFlow(("VBoxNetCfgWinEnableStaticIpConfig: ip=0x%x mask=0x%x\n", ip, mask));
    ComPtr<IWbemServices> pSvc;
    HRESULT hr = netIfWinCreateIWbemServices(pSvc.asOutParam());
    if (SUCCEEDED(hr))
    {
        ComPtr<IWbemClassObject> pAdapterConfig;
        hr = netIfWinFindAdapterClassById(pSvc, pGuid, pAdapterConfig.asOutParam());
        if (SUCCEEDED(hr))
        {
            BOOL fIsHostOnly;
            hr = netIfWinIsHostOnly(pAdapterConfig, &fIsHostOnly);
            if (SUCCEEDED(hr))
            {
                if (fIsHostOnly)
                {
                    in_addr aIp[1];
                    in_addr aMask[1];
                    aIp[0].S_un.S_addr   = ip;
                    aMask[0].S_un.S_addr = mask;

                    com::Bstr bstrObjPath;
                    hr = netIfWinAdapterConfigPath(pAdapterConfig, &bstrObjPath);
                    if (SUCCEEDED(hr))
                    {
                        hr = netIfWinEnableStaticV4(pSvc, pGuid, bstrObjPath, aIp, aMask, ip != 0 ? 1 : 0);
                        if (SUCCEEDED(hr))
                        {
#if 0
                            in_addr aGw[1];
                            aGw[0].S_un.S_addr = gw;
                            hr = netIfWinSetGatewaysV4(pSvc, bstrObjPath, aGw, 1);
                            if (SUCCEEDED(hr))
#endif
                            {
                            }
                        }
                    }
                }
                else
                {
                    hr = E_FAIL;
                }
            }
        }
    }

    NonStandardLogFlow(("VBoxNetCfgWinEnableStaticIpConfig: returns %Rhrc\n", hr));
    return hr;
}

#if 0
static HRESULT netIfEnableStaticIpConfigV6(const GUID *pGuid, IN_BSTR aIPV6Address, IN_BSTR aIPV6Mask, IN_BSTR aIPV6DefaultGateway)
{
    HRESULT hr;
        ComPtr<IWbemServices> pSvc;
        hr = netIfWinCreateIWbemServices(pSvc.asOutParam());
        if (SUCCEEDED(hr))
        {
            ComPtr<IWbemClassObject> pAdapterConfig;
            hr = netIfWinFindAdapterClassById(pSvc, pGuid, pAdapterConfig.asOutParam());
            if (SUCCEEDED(hr))
            {
                BSTR ObjPath;
                hr = netIfWinAdapterConfigPath(pAdapterConfig, &ObjPath);
                if (SUCCEEDED(hr))
                {
                    hr = netIfWinEnableStaticV4V6(pSvc, pAdapterConfig, ObjPath, aIPV6Address, aIPV6Mask);
                    if (SUCCEEDED(hr))
                    {
                        if (aIPV6DefaultGateway)
                        {
                            hr = netIfWinSetGatewaysV4V6(pSvc, ObjPath, aIPV6DefaultGateway);
                        }
                        if (SUCCEEDED(hr))
                        {
//                            hr = netIfWinUpdateConfig(pIf);
                        }
                    }
                    SysFreeString(ObjPath);
                }
            }
        }

    return SUCCEEDED(hr) ? VINF_SUCCESS : VERR_GENERAL_FAILURE;
}

static HRESULT netIfEnableStaticIpConfigV6(const GUID *pGuid, IN_BSTR aIPV6Address, ULONG aIPV6MaskPrefixLength)
{
    RTNETADDRIPV6 Mask;
    int rc = RTNetPrefixToMaskIPv6(aIPV6MaskPrefixLength, &Mask);
    if (RT_SUCCESS(rc))
    {
        Bstr maskStr = composeIPv6Address(&Mask);
        rc = netIfEnableStaticIpConfigV6(pGuid, aIPV6Address, maskStr, NULL);
    }
    return rc;
}
#endif

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinEnableDynamicIpConfig(IN const GUID *pGuid)
{
    ComPtr<IWbemServices> pSvc;
    HRESULT hr = netIfWinCreateIWbemServices(pSvc.asOutParam());
    if (SUCCEEDED(hr))
    {
        ComPtr<IWbemClassObject> pAdapterConfig;
        hr = netIfWinFindAdapterClassById(pSvc, pGuid, pAdapterConfig.asOutParam());
        if (SUCCEEDED(hr))
        {
            BOOL fIsHostOnly;
            hr = netIfWinIsHostOnly(pAdapterConfig, &fIsHostOnly);
            if (SUCCEEDED(hr))
            {
                if (fIsHostOnly)
                {
                    com::Bstr bstrObjPath;
                    hr = netIfWinAdapterConfigPath(pAdapterConfig, &bstrObjPath);
                    if (SUCCEEDED(hr))
                    {
                        hr = netIfWinEnableDHCP(pSvc, bstrObjPath);
                        if (SUCCEEDED(hr))
                        {
                            //hr = netIfWinUpdateConfig(pIf);
                        }
                    }
                }
                else
                    hr = E_FAIL;
            }
        }
    }
    return hr;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinDhcpRediscover(IN const GUID *pGuid)
{
    ComPtr<IWbemServices> pSvc;
    HRESULT hr = netIfWinCreateIWbemServices(pSvc.asOutParam());
    if (SUCCEEDED(hr))
    {
        ComPtr<IWbemClassObject> pAdapterConfig;
        hr = netIfWinFindAdapterClassById(pSvc, pGuid, pAdapterConfig.asOutParam());
        if (SUCCEEDED(hr))
        {
            BOOL fIsHostOnly;
            hr = netIfWinIsHostOnly(pAdapterConfig, &fIsHostOnly);
            if (SUCCEEDED(hr))
            {
                if (fIsHostOnly)
                {
                    com::Bstr bstrObjPath;
                    hr = netIfWinAdapterConfigPath(pAdapterConfig, &bstrObjPath);
                    if (SUCCEEDED(hr))
                    {
                        hr = netIfWinDhcpRediscover(pSvc, bstrObjPath);
                        if (SUCCEEDED(hr))
                        {
                            //hr = netIfWinUpdateConfig(pIf);
                        }
                    }
                }
                else
                    hr = E_FAIL;
            }
        }
    }


    return hr;
}

static const char *vboxNetCfgWinAddrToStr(char *pszBuf, size_t cbBuf, LPSOCKADDR pAddr)
{
    switch (pAddr->sa_family)
    {
        case AF_INET:
            RTStrPrintf(pszBuf, cbBuf, "%d.%d.%d.%d",
                        ((PSOCKADDR_IN)pAddr)->sin_addr.S_un.S_un_b.s_b1,
                        ((PSOCKADDR_IN)pAddr)->sin_addr.S_un.S_un_b.s_b2,
                        ((PSOCKADDR_IN)pAddr)->sin_addr.S_un.S_un_b.s_b3,
                        ((PSOCKADDR_IN)pAddr)->sin_addr.S_un.S_un_b.s_b4);
            break;
        case AF_INET6:
            RTStrPrintf(pszBuf, cbBuf, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
                        ((PSOCKADDR_IN6)pAddr)->sin6_addr.s6_addr[0], ((PSOCKADDR_IN6)pAddr)->sin6_addr.s6_addr[1],
                        ((PSOCKADDR_IN6)pAddr)->sin6_addr.s6_addr[2], ((PSOCKADDR_IN6)pAddr)->sin6_addr.s6_addr[3],
                        ((PSOCKADDR_IN6)pAddr)->sin6_addr.s6_addr[4], ((PSOCKADDR_IN6)pAddr)->sin6_addr.s6_addr[5],
                        ((PSOCKADDR_IN6)pAddr)->sin6_addr.s6_addr[6], ((PSOCKADDR_IN6)pAddr)->sin6_addr.s6_addr[7],
                        ((PSOCKADDR_IN6)pAddr)->sin6_addr.s6_addr[8], ((PSOCKADDR_IN6)pAddr)->sin6_addr.s6_addr[9],
                        ((PSOCKADDR_IN6)pAddr)->sin6_addr.s6_addr[10], ((PSOCKADDR_IN6)pAddr)->sin6_addr.s6_addr[11],
                        ((PSOCKADDR_IN6)pAddr)->sin6_addr.s6_addr[12], ((PSOCKADDR_IN6)pAddr)->sin6_addr.s6_addr[13],
                        ((PSOCKADDR_IN6)pAddr)->sin6_addr.s6_addr[14], ((PSOCKADDR_IN6)pAddr)->sin6_addr.s6_addr[15]);
            break;
        default:
            RTStrCopy(pszBuf, cbBuf, "unknown");
            break;
    }
    return pszBuf;
}

typedef bool (*PFNVBOXNETCFG_IPSETTINGS_CALLBACK) (ULONG ip, ULONG mask, PVOID pContext);

static void vboxNetCfgWinEnumIpConfig(PIP_ADAPTER_ADDRESSES pAddresses, PFNVBOXNETCFG_IPSETTINGS_CALLBACK pfnCallback, PVOID pContext)
{
    PIP_ADAPTER_ADDRESSES pAdapter;
    for (pAdapter = pAddresses; pAdapter; pAdapter = pAdapter->Next)
    {
        NonStandardLogFlow(("+- Enumerating adapter '%ls' %s\n", pAdapter->FriendlyName, pAdapter->AdapterName));
        for (PIP_ADAPTER_PREFIX pPrefix = pAdapter->FirstPrefix; pPrefix; pPrefix = pPrefix->Next)
        {
            char szBuf[80];
            const char *pcszAddress = vboxNetCfgWinAddrToStr(szBuf, sizeof(szBuf), pPrefix->Address.lpSockaddr);

            /* We are concerned with IPv4 only, ignore the rest. */
            if (pPrefix->Address.lpSockaddr->sa_family != AF_INET)
            {
                NonStandardLogFlow(("| +- %s %d: not IPv4, ignoring\n", pcszAddress, pPrefix->PrefixLength));
                continue;
            }

            /* Ignore invalid prefixes as well as host addresses. */
            if (pPrefix->PrefixLength < 1 || pPrefix->PrefixLength > 31)
            {
                NonStandardLogFlow(("| +- %s %d: host or broadcast, ignoring\n", pcszAddress, pPrefix->PrefixLength));
                continue;
            }

            /* Ignore multicast and beyond. */
            ULONG ip = ((struct sockaddr_in *)pPrefix->Address.lpSockaddr)->sin_addr.s_addr;
            if ((ip & 0xF0) > 224)
            {
                NonStandardLogFlow(("| +- %s %d: multicast, ignoring\n", pcszAddress, pPrefix->PrefixLength));
                continue;
            }

            ULONG mask = htonl((~(((ULONG)~0) >> pPrefix->PrefixLength)));
            bool fContinue = pfnCallback(ip, mask, pContext);
            if (!fContinue)
            {
                NonStandardLogFlow(("| +- %s %d: CONFLICT!\n", pcszAddress, pPrefix->PrefixLength));
                return;
            }

            NonStandardLogFlow(("| +- %s %d: no conflict, moving on\n", pcszAddress, pPrefix->PrefixLength));
        }
    }
}

typedef struct _IPPROBE_CONTEXT
{
    ULONG Prefix;
    bool fConflict;
}IPPROBE_CONTEXT, *PIPPROBE_CONTEXT;

#define IPPROBE_INIT(a_pContext, a_addr) \
    do { (a_pContext)->fConflict = false; (a_pContext)->Prefix = (a_addr); } while (0)

#define IPPROBE_INIT_STR(a_pContext, a_straddr) \
    IPROBE_INIT(a_pContext, inet_addr(_straddr))

static bool vboxNetCfgWinIpProbeCallback (ULONG ip, ULONG mask, PVOID pContext)
{
    PIPPROBE_CONTEXT pProbe = (PIPPROBE_CONTEXT)pContext;

    if ((ip & mask) == (pProbe->Prefix & mask))
    {
        pProbe->fConflict = true;
        return false;
    }

    return true;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinGenHostOnlyNetworkNetworkIp(OUT PULONG pNetIp, OUT PULONG pNetMask)
{
    HRESULT hr = S_OK;

    *pNetIp   = 0;
    *pNetMask = 0;

    /*
     * MSDN recommends to pre-allocate a 15KB buffer.
     */
    ULONG                 cbBuf       = 15 * _1K;
    PIP_ADAPTER_ADDRESSES paAddresses = (PIP_ADAPTER_ADDRESSES)RTMemAllocZ(cbBuf);
    if (!paAddresses)
        return E_OUTOFMEMORY;
    DWORD dwRc = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, paAddresses, &cbBuf);
    if (dwRc == ERROR_BUFFER_OVERFLOW)
    {
        /* Impressive! More than 10 adapters! Get more memory and try again. */
        RTMemFree(paAddresses);
        paAddresses = (PIP_ADAPTER_ADDRESSES)RTMemAllocZ(cbBuf);
        if (!paAddresses)
            return E_OUTOFMEMORY;
        dwRc = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, paAddresses, &cbBuf);
    }
    if (dwRc == NO_ERROR)
    {
        const ULONG ip192168 = inet_addr("192.168.0.0");
        for (int i = 0; i < 384; i++)
        {
#if 0
            ULONG ipProbe = rand()*255 / RAND_MAX;
#else
            uint32_t ipProbe = RTRandU32Ex(0, 255);
#endif
            ipProbe = ip192168 | (ipProbe << 16);
            NonStandardLogFlow(("probing %RTnaipv4\n", ipProbe));

            IPPROBE_CONTEXT Context;
            IPPROBE_INIT(&Context, ipProbe);
            vboxNetCfgWinEnumIpConfig(paAddresses, vboxNetCfgWinIpProbeCallback, &Context);
            if (!Context.fConflict)
            {
                NonStandardLogFlow(("found unused net %RTnaipv4\n", ipProbe));
                *pNetIp = ipProbe;
                *pNetMask = inet_addr("255.255.255.0");
                break;
            }
        }
        if (*pNetIp == 0)
            dwRc = ERROR_DHCP_ADDRESS_CONFLICT;
    }
    else
        NonStandardLogFlow(("GetAdaptersAddresses err (%u)\n", dwRc));

    RTMemFree(paAddresses);

    if (dwRc != NO_ERROR)
        hr = HRESULT_FROM_WIN32(dwRc);
    return hr;
}

/*
 * convenience functions to perform netflt/adp manipulations
 */
#define VBOXNETCFGWIN_NETFLT_ID    L"sun_VBoxNetFlt"
#define VBOXNETCFGWIN_NETFLT_MP_ID L"sun_VBoxNetFltmp"

static HRESULT vboxNetCfgWinNetFltUninstall(IN INetCfg *pNc, DWORD InfRmFlags)
{
    INetCfgComponent *pNcc = NULL;
    HRESULT hr = pNc->FindComponent(VBOXNETCFGWIN_NETFLT_ID, &pNcc);
    if (hr == S_OK)
    {
        NonStandardLog("NetFlt is installed currently, uninstalling ...\n");

        hr = VBoxNetCfgWinUninstallComponent(pNc, pNcc);
        NonStandardLogFlow(("NetFlt component uninstallation ended with hr (%Rhrc)\n", hr));

        pNcc->Release();
    }
    else if (hr == S_FALSE)
        NonStandardLog("NetFlt is not installed currently\n");
    else
        NonStandardLogFlow(("FindComponent failed: %Rhrc\n", hr));

    VBoxDrvCfgInfUninstallAllF(L"NetService", VBOXNETCFGWIN_NETFLT_ID, InfRmFlags);
    VBoxDrvCfgInfUninstallAllF(L"Net", VBOXNETCFGWIN_NETFLT_MP_ID, InfRmFlags);

    return hr;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinNetFltUninstall(IN INetCfg *pNc)
{
    return vboxNetCfgWinNetFltUninstall(pNc, 0);
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinNetFltInstall(IN INetCfg *pNc, IN LPCWSTR const *pwszInfFullPath, IN UINT cInfFullPaths)
{
    HRESULT hr = vboxNetCfgWinNetFltUninstall(pNc, SUOI_FORCEDELETE);
    if (SUCCEEDED(hr))
    {
        NonStandardLog("NetFlt will be installed ...\n");
        hr = vboxNetCfgWinInstallInfAndComponent(pNc, VBOXNETCFGWIN_NETFLT_ID,
                                                 &GUID_DEVCLASS_NETSERVICE,
                                                 pwszInfFullPath,
                                                 cInfFullPaths,
                                                 NULL);
    }
    return hr;
}

static HRESULT vboxNetCfgWinNetAdpUninstall(IN INetCfg *pNc, LPCWSTR pwszId, DWORD InfRmFlags)
{
    NOREF(pNc);
    NonStandardLog("Finding NetAdp driver package and trying to uninstall it ...\n");

    VBoxDrvCfgInfUninstallAllF(L"Net", pwszId, InfRmFlags);
    NonStandardLog("NetAdp is not installed currently\n");
    return S_OK;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinNetAdpUninstall(IN INetCfg *pNc, IN LPCWSTR pwszId)
{
    return vboxNetCfgWinNetAdpUninstall(pNc, pwszId, SUOI_FORCEDELETE);
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinNetAdpInstall(IN INetCfg *pNc,
                                                       IN LPCWSTR const pwszInfFullPath)
{
    NonStandardLog("NetAdp will be installed ...\n");
    HRESULT hr = vboxNetCfgWinInstallInfAndComponent(pNc, VBOXNETCFGWIN_NETADP_ID_WSZ,
                                                     &GUID_DEVCLASS_NET,
                                                     &pwszInfFullPath,
                                                     1,
                                                     NULL);
    return hr;
}


static HRESULT vboxNetCfgWinNetLwfUninstall(IN INetCfg *pNc, DWORD InfRmFlags)
{
    INetCfgComponent * pNcc = NULL;
    HRESULT hr = pNc->FindComponent(VBOXNETCFGWIN_NETLWF_ID, &pNcc);
    if (hr == S_OK)
    {
        NonStandardLog("NetLwf is installed currently, uninstalling ...\n");

        hr = VBoxNetCfgWinUninstallComponent(pNc, pNcc);

        pNcc->Release();
    }
    else if (hr == S_FALSE)
    {
        NonStandardLog("NetLwf is not installed currently\n");
        hr = S_OK;
    }
    else
    {
        NonStandardLogFlow(("FindComponent failed: %Rhrc\n", hr));
        hr = S_OK;
    }

    VBoxDrvCfgInfUninstallAllF(L"NetService", VBOXNETCFGWIN_NETLWF_ID, InfRmFlags);

    return hr;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinNetLwfUninstall(IN INetCfg *pNc)
{
    return vboxNetCfgWinNetLwfUninstall(pNc, 0);
}

static void VBoxNetCfgWinFilterLimitWorkaround(void)
{
    /*
     * Need to check if the system has a limit of installed filter drivers. If it
     * has, bump the limit to 14, which the maximum value supported by Windows 7.
     * Note that we only touch the limit if it is set to the default value (8).
     * See @bugref{7899}.
     */
    /** @todo r=bird: This code was mixing HRESULT and LSTATUS, checking the return
     * codes using SUCCEEDED(lrc) instead of lrc == ERROR_SUCCESS.  So, it might
     * have misbehaved on bogus registry content, but worked fine on sane values. */
    HKEY hKeyNet = NULL;
    LSTATUS lrc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Network", 0,
                                KEY_QUERY_VALUE | KEY_SET_VALUE, &hKeyNet);
    if (lrc == ERROR_SUCCESS)
    {
        DWORD dwMaxNumFilters = 0;
        DWORD cbMaxNumFilters = sizeof(dwMaxNumFilters);
        lrc = RegQueryValueExW(hKeyNet, L"MaxNumFilters", NULL, NULL, (LPBYTE)&dwMaxNumFilters, &cbMaxNumFilters);
        if (lrc == ERROR_SUCCESS && cbMaxNumFilters == sizeof(dwMaxNumFilters) && dwMaxNumFilters == 8)
        {
            dwMaxNumFilters = 14;
            lrc = RegSetValueExW(hKeyNet, L"MaxNumFilters", 0, REG_DWORD, (LPBYTE)&dwMaxNumFilters, sizeof(dwMaxNumFilters));
            if (lrc == ERROR_SUCCESS)
                NonStandardLog("Adjusted the installed filter limit to 14...\n");
            else
                NonStandardLog("Failed to set MaxNumFilters, error code %d\n", lrc);
        }
        RegCloseKey(hKeyNet);
    }
    else
        NonStandardLog("Failed to open network key, error code %d\n", lrc);

}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinNetLwfInstall(IN INetCfg *pNc, IN LPCWSTR const pwszInfFullPath)
{
    HRESULT hr = vboxNetCfgWinNetLwfUninstall(pNc, SUOI_FORCEDELETE);
    if (SUCCEEDED(hr))
    {
        VBoxNetCfgWinFilterLimitWorkaround();
        NonStandardLog("NetLwf will be installed ...\n");
        hr = vboxNetCfgWinInstallInfAndComponent(pNc, VBOXNETCFGWIN_NETLWF_ID,
                                                 &GUID_DEVCLASS_NETSERVICE,
                                                 &pwszInfFullPath,
                                                 1,
                                                 NULL);
    }
    return hr;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinGenHostonlyConnectionName(IN PCWSTR pwszDevName, OUT WCHAR *pwszBuf,
                                                                   IN ULONG cwcBuf, OUT PULONG pcwcNeeded)
{
    /* Look for a suffix that we need to preserve. */
    size_t const cwcDevName = RTUtf16Len(pwszDevName);
    size_t       offSuffix  = cwcDevName;
    while (offSuffix > 0 && pwszDevName[offSuffix - 1] != '#')
        offSuffix--;
    size_t const cwcSuffix  = pwszDevName[offSuffix] != '#' ? 0 : cwcDevName - offSuffix;

    /* Calculate required buffer size: */
    size_t cwcNeeded = sizeof(VBOX_CONNECTION_NAME_WSZ) / sizeof(wchar_t) /* includes terminator */
                     + !!cwcSuffix /*space*/ + cwcSuffix;
    if (pcwcNeeded)
        *pcwcNeeded = (ULONG)cwcNeeded;

    if (cwcNeeded <= cwcBuf)
    {
        memcpy(pwszBuf, VBOX_CONNECTION_NAME_WSZ, sizeof(VBOX_CONNECTION_NAME_WSZ));
        if (cwcSuffix > 0)
        {
            size_t offDst = sizeof(VBOX_CONNECTION_NAME_WSZ) / sizeof(wchar_t) - 1;
            pwszBuf[offDst++] = ' ';
            memcpy(&pwszBuf[offDst], &pwszDevName[offSuffix], cwcSuffix * sizeof(wchar_t));
            pwszBuf[offDst + cwcSuffix] = '\0';
        }
        return S_OK;
    }
    return E_FAIL;
}

static BOOL vboxNetCfgWinAdjustHostOnlyNetworkInterfacePriority(IN INetCfg *pNc, IN INetCfgComponent *pNcc, PVOID pContext)
{
    GUID * const pGuid = (GUID*)pContext;
    RT_NOREF1(pNc);

    /* Get component's binding. */
    INetCfgComponentBindings *pNetCfgBindings = NULL;
    HRESULT hr = pNcc->QueryInterface(IID_INetCfgComponentBindings, (PVOID *)&pNetCfgBindings);
    if (SUCCEEDED(hr))
    {
        /* Get binding path enumerator reference. */
        IEnumNetCfgBindingPath *pEnumNetCfgBindPath = NULL;
        hr = pNetCfgBindings->EnumBindingPaths(EBP_BELOW, &pEnumNetCfgBindPath);
        if (SUCCEEDED(hr))
        {
            bool fFoundIface = false;
            hr = pEnumNetCfgBindPath->Reset();
            do
            {
                INetCfgBindingPath *pNetCfgBindPath = NULL;
                hr = pEnumNetCfgBindPath->Next(1, &pNetCfgBindPath, NULL);
                if (hr == S_OK)
                {
                    IEnumNetCfgBindingInterface *pEnumNetCfgBindIface;
                    hr = pNetCfgBindPath->EnumBindingInterfaces(&pEnumNetCfgBindIface);
                    if (hr == S_OK)
                    {
                        pEnumNetCfgBindIface->Reset();
                        do
                        {
                            INetCfgBindingInterface *pNetCfgBindIfce;
                            hr = pEnumNetCfgBindIface->Next(1, &pNetCfgBindIfce, NULL);
                            if (hr == S_OK)
                            {
                                INetCfgComponent *pNetCfgCompo;
                                hr = pNetCfgBindIfce->GetLowerComponent(&pNetCfgCompo);
                                if (hr == S_OK)
                                {
                                    ULONG uComponentStatus;
                                    hr = pNetCfgCompo->GetDeviceStatus(&uComponentStatus);
                                    if (hr == S_OK)
                                    {
                                        GUID guid;
                                        hr = pNetCfgCompo->GetInstanceGuid(&guid);
                                        if (   hr == S_OK
                                            && guid == *pGuid)
                                        {
                                            hr = pNetCfgBindings->MoveAfter(pNetCfgBindPath, NULL);
                                            if (FAILED(hr))
                                                NonStandardLogFlow(("Unable to move interface: %Rhrc\n", hr));
                                            fFoundIface = true;

                                            /*
                                             * Enable binding paths for host-only adapters bound to bridged filter
                                             * (see @bugref{8140}).
                                             */
                                            HRESULT hr2;
                                            LPWSTR  pwszHwId = NULL;
                                            if ((hr2 = pNcc->GetId(&pwszHwId)) != S_OK)
                                                NonStandardLogFlow(("Failed to get HW ID: %Rhrc\n", hr2));
                                            else
                                            {
                                                /** @todo r=bird: Original was:
                                                 *    _wcsnicmp(pwszHwId, VBOXNETCFGWIN_NETLWF_ID, sizeof(VBOXNETCFGWIN_NETLWF_ID)/2)
                                                 * which is the same as _wcsicmp. Not sure if this was accidental, but it's not the
                                                 * only one in this code area (VBoxNetFltNobj.cpp had some too IIRC). */
                                                if (RTUtf16ICmp(pwszHwId, VBOXNETCFGWIN_NETLWF_ID) != 0)
                                                    NonStandardLogFlow(("Ignoring component %ls\n", pwszHwId));
                                                else if ((hr2 = pNetCfgBindPath->IsEnabled()) != S_FALSE)
                                                    NonStandardLogFlow(("Already enabled binding path: %Rhrc\n", hr2));
                                                else if ((hr2 = pNetCfgBindPath->Enable(TRUE)) != S_OK)
                                                    NonStandardLogFlow(("Failed to enable binding path: %Rhrc\n", hr2));
                                                else
                                                    NonStandardLogFlow(("Enabled binding path\n"));
                                                CoTaskMemFree(pwszHwId);
                                            }
                                        }
                                    }
                                    pNetCfgCompo->Release();
                                }
                                else
                                    NonStandardLogFlow(("GetLowerComponent failed: %Rhrc\n", hr));
                                pNetCfgBindIfce->Release();
                            }
                            else
                            {
                                if (hr == S_FALSE) /* No more binding interfaces? */
                                    hr = S_OK;
                                else
                                    NonStandardLogFlow(("Next binding interface failed: %Rhrc\n", hr));
                                break;
                            }
                        } while (!fFoundIface);
                        pEnumNetCfgBindIface->Release();
                    }
                    else
                        NonStandardLogFlow(("EnumBindingInterfaces failed: %Rhrc\n", hr));
                    pNetCfgBindPath->Release();
                }
                else
                {
                    if (hr == S_FALSE) /* No more binding paths? */
                        hr = S_OK;
                    else
                        NonStandardLogFlow(("Next bind path failed: %Rhrc\n", hr));
                    break;
                }
            } while (!fFoundIface);
            pEnumNetCfgBindPath->Release();
        }
        else
            NonStandardLogFlow(("EnumBindingPaths failed: %Rhrc\n", hr));
        pNetCfgBindings->Release();
    }
    else
        NonStandardLogFlow(("QueryInterface for IID_INetCfgComponentBindings failed: %Rhrc\n", hr));
    return TRUE;
}

/** Callback for SetupDiSetDeviceInstallParams used  by
 *  vboxNetCfgWinCreateHostOnlyNetworkInterface */
static UINT WINAPI vboxNetCfgWinPspFileCallback(PVOID Context, UINT Notification, UINT_PTR Param1, UINT_PTR Param2)
{
    switch (Notification)
    {
        case SPFILENOTIFY_TARGETNEWER:
        case SPFILENOTIFY_TARGETEXISTS:
            return TRUE;
    }
    return SetupDefaultQueueCallbackW(Context, Notification, Param1, Param2);
}




/* The original source of the VBoxNetAdp adapter creation/destruction code has the following copyright: */
/*
   Copyright 2004 by the Massachusetts Institute of Technology

   All rights reserved.

   Permission to use, copy, modify, and distribute this software and its
   documentation for any purpose and without fee is hereby granted,
   provided that the above copyright notice appear in all copies and that
   both that copyright notice and this permission notice appear in
   supporting documentation, and that the name of the Massachusetts
   Institute of Technology (M.I.T.) not be used in advertising or publicity
   pertaining to distribution of the software without specific, written
   prior permission.

   M.I.T. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
   ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
   M.I.T. BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
   ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
   WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
   ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
   SOFTWARE.
*/


/**
 *  Use the IShellFolder API to rename the connection.
 */
static HRESULT rename_shellfolder(PCWSTR pwszGuid, PCWSTR pwszNewName)
{
    /* Build the display name in the form "::{GUID}". Do this first in case it overflows. */
    WCHAR wszAdapterGuid[MAX_PATH + 2] = {0};
    ssize_t cwc = RTUtf16Printf(wszAdapterGuid, RT_ELEMENTS(wszAdapterGuid), "::%ls", pwszGuid);
    if (cwc < 0)
        return E_INVALIDARG;

    /* This is the GUID for the network connections folder. It is constant.
     * {7007ACC7-3202-11D1-AAD2-00805FC1270E} */
    const GUID MY_CLSID_NetworkConnections = {
        0x7007ACC7, 0x3202, 0x11D1, {
            0xAA, 0xD2, 0x00, 0x80, 0x5F, 0xC1, 0x27, 0x0E
        }
    };

    /* Create an instance of the network connections folder. */
    IShellFolder *pShellFolder = NULL;
    HRESULT hr = CoCreateInstance(MY_CLSID_NetworkConnections, NULL, CLSCTX_INPROC_SERVER, IID_IShellFolder,
                                  reinterpret_cast<LPVOID *>(&pShellFolder));
    if (SUCCEEDED(hr))
    {
        /* Parse the display name. */
        LPITEMIDLIST pidl = NULL;
        hr = pShellFolder->ParseDisplayName(NULL, NULL, wszAdapterGuid, NULL, &pidl, NULL);
        if (SUCCEEDED(hr))
            hr = pShellFolder->SetNameOf(NULL, pidl, pwszNewName, SHGDN_NORMAL, &pidl);
        CoTaskMemFree(pidl);
        pShellFolder->Release();
    }
    return hr;
}

/**
 * Loads a system DLL.
 *
 * @returns Module handle or NULL
 * @param   pwszName            The DLL name.
 */
static HMODULE loadSystemDll(const wchar_t *pwszName)
{
    WCHAR  wszPath[MAX_PATH];
    UINT   cwcPath = GetSystemDirectoryW(wszPath, RT_ELEMENTS(wszPath));
    size_t cwcName = RTUtf16Len(pwszName) + 1;
    if (cwcPath + 1 + cwcName > RT_ELEMENTS(wszPath))
        return NULL;

    wszPath[cwcPath++] = '\\';
    memcpy(&wszPath[cwcPath], pwszName, cwcName * sizeof(wszPath[0]));
    return LoadLibraryW(wszPath);
}

static bool vboxNetCfgWinDetectStaleConnection(PCWSTR pwszName)
{
    HKEY    hKeyAdapters = NULL;
    LSTATUS lrc = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                               L"SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}",
                               0 /*ulOptions*/, KEY_ALL_ACCESS, &hKeyAdapters);
    if (lrc != ERROR_SUCCESS)
        return false;

    bool fFailureImminent = false;
    for (DWORD i = 0; !fFailureImminent; ++i)
    {
        WCHAR wszAdapterSubKeyName[MAX_PATH];
        DWORD cwcAdapterSubKeyName = MAX_PATH;
        lrc = RegEnumKeyEx(hKeyAdapters, i, wszAdapterSubKeyName, &cwcAdapterSubKeyName, NULL, NULL, NULL, NULL);
        if (lrc != ERROR_SUCCESS)
            break;

        HKEY hKeyAdapter = NULL;
        lrc = RegOpenKeyEx(hKeyAdapters, wszAdapterSubKeyName, 0, KEY_ALL_ACCESS, &hKeyAdapter);
        if (lrc == ERROR_SUCCESS)
        {
            HKEY hKeyConnection = NULL;
            lrc = RegOpenKeyEx(hKeyAdapter, L"Connection", 0, KEY_ALL_ACCESS, &hKeyConnection);
            if (lrc == ERROR_SUCCESS)
            {
                WCHAR wszCurName[MAX_PATH + 1];
                DWORD cbCurName = sizeof(wszCurName) - sizeof(WCHAR);
                DWORD dwType    = REG_SZ;
                lrc = RegQueryValueEx(hKeyConnection, L"Name", NULL, NULL, (LPBYTE)wszCurName, &cbCurName);
                if (   lrc == ERROR_SUCCESS
                    /** @todo r=bird: The original code didn't do any value type checks, thus allowing all SZ types. */
                    && (dwType == REG_SZ || dwType == REG_EXPAND_SZ || dwType == REG_MULTI_SZ))
                {
                    wszCurName[MAX_PATH] = '\0'; /* returned values doesn't necessarily need to be terminated */

                    if (RTUtf16ICmp(pwszName, pwszName) == 0)
                        fFailureImminent = true;
                }
                RegCloseKey(hKeyConnection);
            }
            RegCloseKey(hKeyAdapter);
        }
    }
    RegCloseKey(hKeyAdapters);

    return fFailureImminent;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinRenameConnection(LPWSTR pwszGuid, PCWSTR NewName)
{
    /*
     * Before attempting to rename the connection, check if there is a stale
     * connection with the same name. We must return ok, so the rest of
     * configuration process proceeds normally.
     */
    if (vboxNetCfgWinDetectStaleConnection(NewName))
        return S_OK;

    /* First try the IShellFolder interface, which was unimplemented
     * for the network connections folder before XP. */
    HRESULT hrc = rename_shellfolder(pwszGuid, NewName);
    if (hrc == E_NOTIMPL)
    {
/** @todo that code doesn't seem to work! */
        /* The IShellFolder interface is not implemented on this platform.
         * Try the (undocumented) HrRenameConnection API in the netshell
         * library. */
        CLSID     clsid;
        hrc = CLSIDFromString((LPOLESTR)pwszGuid, &clsid);
        if (FAILED(hrc))
            return E_FAIL;

        HINSTANCE hNetShell = loadSystemDll(L"netshell.dll");
        if (hNetShell == NULL)
            return E_FAIL;

        typedef HRESULT (WINAPI *PFNHRRENAMECONNECTION)(const GUID *, PCWSTR);
        PFNHRRENAMECONNECTION pfnRenameConnection = (PFNHRRENAMECONNECTION)GetProcAddress(hNetShell, "HrRenameConnection");
        if (pfnRenameConnection != NULL)
            hrc = pfnRenameConnection(&clsid, NewName);
        else
            hrc = E_FAIL;

        FreeLibrary(hNetShell);
    }
    if (FAILED(hrc))
        return hrc;
    return S_OK;
}


VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinRemoveHostOnlyNetworkInterface(IN const GUID *pGUID, OUT BSTR *pBstrErrMsg)
{
    HRESULT hrc = S_OK;
    com::Bstr bstrError;

    do /* break non-loop */
    {
        WCHAR wszPnPInstanceId[512] = {0};

        /*
         * We have to find the device instance ID through a registry search
         */
        HKEY hkeyNetwork    = NULL;
        HKEY hkeyConnection = NULL;
        do /* another non-loop for breaking out of */
        {
            WCHAR wszGuid[50];
            int cwcGuid = StringFromGUID2(*pGUID, wszGuid, RT_ELEMENTS(wszGuid));
            if (!cwcGuid)
                SetErrBreak(("Failed to create a Guid string"));

            WCHAR wszRegLocation[128 + RT_ELEMENTS(wszGuid)];
            RTUtf16Printf(wszRegLocation, RT_ELEMENTS(wszRegLocation),
                          "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\%ls", wszGuid);

            LSTATUS lrc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, wszRegLocation, 0, KEY_READ, &hkeyNetwork);
            if (lrc != ERROR_SUCCESS || !hkeyNetwork)
                SetErrBreak(("Host interface network is not found in registry (%S): lrc=%u [1]", wszRegLocation, lrc));

            lrc = RegOpenKeyExW(hkeyNetwork, L"Connection", 0, KEY_READ, &hkeyConnection);
            if (lrc != ERROR_SUCCESS || !hkeyConnection)
                SetErrBreak(("Host interface network is not found in registry (%S): lrc=%u [2]", wszRegLocation, lrc));

            DWORD cbValue = sizeof(wszPnPInstanceId) - sizeof(WCHAR);
            DWORD dwType  = ~0U;
            lrc = RegQueryValueExW(hkeyConnection, L"PnPInstanceID", NULL, &dwType, (LPBYTE)wszPnPInstanceId, &cbValue);
            if (lrc != ERROR_SUCCESS || dwType != REG_SZ)
                SetErrBreak(("Host interface network is not found in registry (%S): lrc=%u, dwType=%u [3]",
                             wszRegLocation, lrc, dwType));
        } while (0);

        if (hkeyConnection)
            RegCloseKey(hkeyConnection);
        if (hkeyNetwork)
            RegCloseKey(hkeyNetwork);
        if (FAILED(hrc))
            break;

        /*
         * Now we are going to enumerate all network devices and
         * wait until we encounter the right device instance ID
         */
        HDEVINFO hDeviceInfo = INVALID_HANDLE_VALUE;
        do /* break-only, not-a-loop */
        {
            BOOL  ok;

            /* initialize the structure size */
            SP_DEVINFO_DATA DeviceInfoData = { sizeof(DeviceInfoData) };

            /* copy the net class GUID */
            GUID netGuid;
            memcpy(&netGuid, &GUID_DEVCLASS_NET, sizeof(GUID_DEVCLASS_NET));

            /* return a device info set contains all installed devices of the Net class */
            hDeviceInfo = SetupDiGetClassDevs(&netGuid, NULL, NULL, DIGCF_PRESENT);
            if (hDeviceInfo == INVALID_HANDLE_VALUE)
                SetErrBreak(("SetupDiGetClassDevs failed (0x%08X)", GetLastError()));

            /* Enumerate the driver info list. */
            bool fFound = false;
            for (DWORD index = 0; !fFound; index++)
            {
                if (!SetupDiEnumDeviceInfo(hDeviceInfo, index, &DeviceInfoData))
                {
                    if (GetLastError() == ERROR_NO_MORE_ITEMS)
                        break;
                    continue;
                }

                /* try to get the hardware ID registry property */
                DWORD cbValue = 0;
                if (SetupDiGetDeviceRegistryPropertyW(hDeviceInfo,
                                                      &DeviceInfoData,
                                                      SPDRP_HARDWAREID,
                                                      NULL,
                                                      NULL,
                                                      0,
                                                      &cbValue))
                    continue; /* Something is wrong.  This shouldn't have worked with a NULL buffer! */
                if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                    continue;

                WCHAR *pwszzDeviceHwId = (WCHAR *)RTMemAllocZ(cbValue + sizeof(WCHAR) * 2);
                if (!pwszzDeviceHwId)
                    break;
                if (SetupDiGetDeviceRegistryPropertyW(hDeviceInfo,
                                                      &DeviceInfoData,
                                                      SPDRP_HARDWAREID,
                                                      NULL,
                                                      (PBYTE)pwszzDeviceHwId,
                                                      cbValue,
                                                      &cbValue))
                {
                    /* search the string list. */
                    for (WCHAR *pwszCurHwId = pwszzDeviceHwId;
                         (uintptr_t)pwszCurHwId - (uintptr_t)pwszzDeviceHwId < cbValue && *pwszCurHwId != L'\0';
                         pwszCurHwId += RTUtf16Len(pwszCurHwId) + 1)
                        if (RTUtf16ICmp(DRIVERHWID, pwszCurHwId) == 0)
                        {
                            /* get the device instance ID */
                            WCHAR wszDevId[MAX_DEVICE_ID_LEN];
                            if (CM_Get_Device_IDW(DeviceInfoData.DevInst, wszDevId, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS)
                            {
                                /* compare to what we determined before */
                                if (RTUtf16Cmp(wszDevId, wszPnPInstanceId) == 0)
                                {
                                    fFound = true;
                                    break;
                                }
                            }
                        }
                }
                RTMemFree(pwszzDeviceHwId);
            }

            if (!fFound)
                SetErrBreak(("Host Interface Network driver not found (0x%08X)", GetLastError()));

            ok = SetupDiSetSelectedDevice(hDeviceInfo, &DeviceInfoData);
            if (!ok)
                SetErrBreak(("SetupDiSetSelectedDevice failed (0x%08X)", GetLastError()));

            ok = SetupDiCallClassInstaller(DIF_REMOVE, hDeviceInfo, &DeviceInfoData);
            if (!ok)
                SetErrBreak(("SetupDiCallClassInstaller (DIF_REMOVE) failed (0x%08X)", GetLastError()));
        } while (0);

        /* clean up the device info set */
        if (hDeviceInfo != INVALID_HANDLE_VALUE)
            SetupDiDestroyDeviceInfoList (hDeviceInfo);
    } while (0);

    if (pBstrErrMsg)
    {
        *pBstrErrMsg = NULL;
        if (bstrError.isNotEmpty())
            bstrError.detachToEx(pBstrErrMsg);
    }
    return hrc;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinUpdateHostOnlyNetworkInterface(LPCWSTR pcsxwInf, BOOL *pfRebootRequired, LPCWSTR pcsxwId)
{
    return VBoxDrvCfgDrvUpdate(pcsxwId, pcsxwInf, pfRebootRequired);
}

static const char *vboxNetCfgWinGetStateText(DWORD dwState)
{
    switch (dwState)
    {
        case SERVICE_STOPPED:           return "is not running";
        case SERVICE_STOP_PENDING:      return "is stopping";
        case SERVICE_CONTINUE_PENDING:  return "continue is pending";
        case SERVICE_PAUSE_PENDING:     return "pause is pending";
        case SERVICE_PAUSED:            return "is paused";
        case SERVICE_RUNNING:           return "is running";
        case SERVICE_START_PENDING:     return "is starting";
    }
    return "state is invalid";
}

static DWORD vboxNetCfgWinGetNetSetupState(SC_HANDLE hService)
{
    SERVICE_STATUS status;
    status.dwCurrentState = SERVICE_RUNNING;
    if (hService) {
        if (QueryServiceStatus(hService, &status))
            NonStandardLogFlow(("NetSetupSvc %s\n", vboxNetCfgWinGetStateText(status.dwCurrentState)));
        else
            NonStandardLogFlow(("QueryServiceStatus failed (0x%x)\n", GetLastError()));
    }
    return status.dwCurrentState;
}

DECLINLINE(bool) vboxNetCfgWinIsNetSetupRunning(SC_HANDLE hService)
{
    return vboxNetCfgWinGetNetSetupState(hService) == SERVICE_RUNNING;
}

DECLINLINE(bool) vboxNetCfgWinIsNetSetupStopped(SC_HANDLE hService)
{
    return vboxNetCfgWinGetNetSetupState(hService) == SERVICE_STOPPED;
}

typedef struct
{
    BSTR bstrName;
    GUID *pGuid;
    HRESULT hr;
} RENAMING_CONTEXT;

static BOOL vboxNetCfgWinRenameHostOnlyNetworkInterface(IN INetCfg *pNc, IN INetCfgComponent *pNcc, PVOID pContext)
{
    RT_NOREF1(pNc);
    RENAMING_CONTEXT *pParams = (RENAMING_CONTEXT *)pContext;

    GUID guid;
    pParams->hr = pNcc->GetInstanceGuid(&guid);
    if ( pParams->hr == S_OK && guid == *pParams->pGuid)
    {
        /* Located our component, rename it */
        pParams->hr = pNcc->SetDisplayName(pParams->bstrName);
        return FALSE;
    }
    return TRUE;
}

/**
 * Enumerate all host-only adapters collecting their names into a set, then
 * come up with the next available name by taking the first unoccupied index.
 */
static HRESULT vboxNetCfgWinNextAvailableDevName(com::Bstr *pbstrName)
{
    SP_DEVINFO_DATA DeviceInfoData = { sizeof(SP_DEVINFO_DATA) };
    HDEVINFO hDeviceInfoSet = SetupDiGetClassDevsW(&GUID_DEVCLASS_NET, NULL, NULL, DIGCF_PRESENT);
    if (hDeviceInfoSet == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());

    typedef struct VBOXDEVNAMEENTRY
    {
        RTLISTNODE  ListEntry;
        WCHAR       wszDevName[64];
        WCHAR       wcZeroParanoia;
    } VBOXDEVNAMEENTRY;

#if 0
    /*
     * Build a list of names starting with HOSTONLY_ADAPTER_NAME_WSZ belonging to our device.
     */
    RTLISTANCHOR Head; /* VBOXDEVNAMEENTRY */
    RTListInit(&Head);
    HRESULT      hrc = S_OK;
#else
    /*
     * Build a bitmap of in-use index values of devices starting with HOSTONLY_ADAPTER_NAME_WSZ.
     * Reserving 0 for one w/o a suffix, and marking 1 as unusable.
     */
    uint64_t bmIndexes[_32K / 64]; /* 4KB - 32767 device should be sufficient. */
    RT_ZERO(bmIndexes);
    ASMBitSet(&bmIndexes, 1);
#endif
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDeviceInfoSet, i, &DeviceInfoData); ++i)
    {
        /* Should be more than enough for both our device id and our device name, we do not care about the rest */
        VBOXDEVNAMEENTRY Entry = { { 0, 0 }, L"", 0 }; /* (initialize it to avoid the wrath of asan) */
        if (!SetupDiGetDeviceRegistryPropertyW(hDeviceInfoSet, &DeviceInfoData, SPDRP_HARDWAREID,
                                               NULL, (PBYTE)Entry.wszDevName, sizeof(Entry.wszDevName), NULL))
            continue;

        /* Ignore everything except our host-only adapters */
        if (RTUtf16ICmp(Entry.wszDevName, DRIVERHWID) == 0)
        {
            if (   SetupDiGetDeviceRegistryPropertyW(hDeviceInfoSet, &DeviceInfoData, SPDRP_FRIENDLYNAME,
                                                     NULL, (PBYTE)Entry.wszDevName, sizeof(Entry.wszDevName), NULL)
                || SetupDiGetDeviceRegistryPropertyW(hDeviceInfoSet, &DeviceInfoData, SPDRP_DEVICEDESC,
                                                     NULL, (PBYTE)Entry.wszDevName, sizeof(Entry.wszDevName), NULL))
            {
                /* We can ignore any host-only adapter with a non-standard name. */
                if (RTUtf16NICmp(Entry.wszDevName, HOSTONLY_ADAPTER_NAME_WSZ,
                                 RT_ELEMENTS(HOSTONLY_ADAPTER_NAME_WSZ) - 1) == 0)
                {
#if 0
                    VBOXDEVNAMEENTRY *pEntry = (VBOXDEVNAMEENTRY *)RTMemDup(&Entry, sizeof(Entry));
                    if (pEntry)
                        RTListAppend(&Head, &pEntry->ListEntry);
                    else
                    {
                        hrc = E_OUTOFMEMORY;
                        break;
                    }
#else
                    WCHAR const *pwc = &Entry.wszDevName[RT_ELEMENTS(HOSTONLY_ADAPTER_NAME_WSZ) - 1];

                    /* skip leading space */
                    WCHAR wc = *pwc;
                    while (wc == L' ' || wc == L'\t' || wc == L'\n' || wc == L'\r')
                        wc = *++pwc;

                    /* If end of string, use index 0. */
                    if (wc == L'\0')
                        ASMBitSet(bmIndexes, 0);

                    /* Hash and digit? */
                    else if (wc == L'#')
                    {
                        wc = *++pwc;
                        while (wc == L' ' || wc == L'\t' || wc == L'\n' || wc == L'\r') /* just in case */
                            wc = *++pwc;
                        if (wc >= L'0' && wc <= L'9')
                        {
                            /* Convert what we can to a number and mark it as allocated in the bitmap. */
                            uint64_t uIndex = wc - L'0';
                            while ((wc = *++pwc) >= L'0' && wc <= L'9')
                                uIndex = uIndex * 10 + wc - L'0';
                            if (uIndex < sizeof(bmIndexes) * 8 && uIndex > 0)
                                ASMBitSet(bmIndexes, (int32_t)uIndex);
                        }
                    }
#endif
                }
            }
        }
    }
#if 0
    if (SUCCEEDED(hrc))
    {
        /*
         * First try a name w/o an index, then try index #2 and up.
         *
         * Note! We have to use ASCII/UTF-8 strings here as Bstr will confuse WCHAR
         *       with BSTR/OLECHAR strings and use SysAllocString to duplicate it .
         */
        char         szName[sizeof(HOSTONLY_ADAPTER_NAME_SZ " #4294967296") + 32] = HOSTONLY_ADAPTER_NAME_SZ;
        size_t const cchBase = sizeof(HOSTONLY_ADAPTER_NAME_SZ) - 1;
        for (DWORD idx = 2;; idx++)
        {
            bool              fFound = false;
            VBOXDEVNAMEENTRY *pCur;
            RTListForEach(&Head, pCur, VBOXDEVNAMEENTRY, ListEntry)
            {
                fFound = RTUtf16ICmpAscii(pCur->wszDevName, szName) == 0;
                if (fFound)
                {
                    hrc = pbstrName->assignEx(szName);
                    break;
                }
            }
            if (fFound)
                break;
            RTStrPrintf(&szName[cchBase], sizeof(szName) - cchBase, " #%u", idx);
        }
    }

    VBOXDEVNAMEENTRY *pFirst;
    while ((pFirst = RTListRemoveFirst(&Head, VBOXDEVNAMEENTRY, ListEntry)) != NULL)
        RTMemFree(pFirst);

#else
    /*
     * Find an unused index value and format the corresponding name.
     */
    HRESULT hrc;
    int32_t iBit = ASMBitFirstClear(bmIndexes, sizeof(bmIndexes) * 8);
    if (iBit >= 0)
    {
        if (iBit == 0)
            hrc = pbstrName->assignEx(HOSTONLY_ADAPTER_NAME_SZ); /* Not _WSZ! */
        else
            hrc = pbstrName->printfNoThrow(HOSTONLY_ADAPTER_NAME_SZ " #%u", iBit);
    }
    else
    {
        NonStandardLogFlow(("vboxNetCfgWinNextAvailableDevName: no unused index in the first 32K!\n"));
        hrc = E_FAIL;
    }
#endif

    if (hDeviceInfoSet)
        SetupDiDestroyDeviceInfoList(hDeviceInfoSet);
    return hrc;
}

static HRESULT vboxNetCfgWinCreateHostOnlyNetworkInterface(IN LPCWSTR pwszInfPath, IN bool fIsInfPathFile,
                                                           IN BSTR pBstrDesiredName,
                                                           OUT GUID *pGuid, OUT BSTR *pBstrName, OUT BSTR *pBstrErrMsg)
{
    com::Bstr bstrError;

    /* Determine the interface name. We make a copy of the input here for
       renaming reasons, see futher down. */
    com::Bstr bstrNewInterfaceName;
    HRESULT   hrc;
    if (SysStringLen(pBstrDesiredName) != 0)
        hrc = bstrNewInterfaceName.assignEx(pBstrDesiredName);
    else
    {
        hrc = vboxNetCfgWinNextAvailableDevName(&bstrNewInterfaceName);
        if (FAILED(hrc))
            NonStandardLogFlow(("vboxNetCfgWinNextAvailableDevName failed with 0x%x\n", hrc));
    }
    if (FAILED(hrc))
        return hrc;

    WCHAR           wszCfgGuidString[50]  = {0};
    WCHAR           wszDevName[256 + 1]   = {0};
    SP_DEVINFO_DATA DeviceInfoData        = { sizeof(DeviceInfoData) };
    HDEVINFO        hDeviceInfo           = INVALID_HANDLE_VALUE;
    PVOID           pQueueCallbackContext = NULL;
    BOOL            fRegistered           = FALSE;
    BOOL            destroyList           = FALSE;
    HKEY            hkey                  = (HKEY)INVALID_HANDLE_VALUE;
    LSTATUS         lrcRet                = ERROR_SUCCESS; /* the */

    do /* non-loop, for breaking. */
    {
        /* copy the net class GUID */
        GUID netGuid;
        memcpy(&netGuid, &GUID_DEVCLASS_NET, sizeof(GUID_DEVCLASS_NET));

        /* Create an empty device info set associated with the net class GUID: */
        hDeviceInfo = SetupDiCreateDeviceInfoList(&netGuid, NULL);
        if (hDeviceInfo == INVALID_HANDLE_VALUE)
            SetErrBreak(("SetupDiCreateDeviceInfoList failed (%Rwc)", GetLastError()));

        /* Translate the GUID to a class name: */
        WCHAR wszClassName[MAX_PATH];
        if (!SetupDiClassNameFromGuid(&netGuid, wszClassName, MAX_PATH, NULL))
            SetErrBreak(("SetupDiClassNameFromGuid failed (%Rwc)", GetLastError()));

        /* Create a device info element and add the new device instance key to registry: */
        if (!SetupDiCreateDeviceInfo(hDeviceInfo, wszClassName, &netGuid, NULL, NULL, DICD_GENERATE_ID, &DeviceInfoData))
            SetErrBreak(("SetupDiCreateDeviceInfo failed (%Rwc)", GetLastError()));

        /* Select the newly created device info to be the currently selected member: */
        if (!SetupDiSetSelectedDevice(hDeviceInfo, &DeviceInfoData))
            SetErrBreak(("SetupDiSetSelectedDevice failed (%Rwc)", GetLastError()));

        SP_DEVINSTALL_PARAMS DeviceInstallParams;
        if (pwszInfPath)
        {
            /* get the device install parameters and disable filecopy */
            DeviceInstallParams.cbSize = sizeof(SP_DEVINSTALL_PARAMS);
            if (SetupDiGetDeviceInstallParams(hDeviceInfo, &DeviceInfoData, &DeviceInstallParams))
            {
                memset(DeviceInstallParams.DriverPath, 0, sizeof(DeviceInstallParams.DriverPath));
                size_t pathLenght = wcslen(pwszInfPath) + 1/* null terminator */;
                if (pathLenght < sizeof(DeviceInstallParams.DriverPath)/sizeof(DeviceInstallParams.DriverPath[0]))
                {
                    memcpy(DeviceInstallParams.DriverPath, pwszInfPath, pathLenght*sizeof(DeviceInstallParams.DriverPath[0]));

                    if (fIsInfPathFile)
                        DeviceInstallParams.Flags |= DI_ENUMSINGLEINF;

                    if (!SetupDiSetDeviceInstallParams(hDeviceInfo, &DeviceInfoData, &DeviceInstallParams))
                    {
                        NonStandardLogFlow(("SetupDiSetDeviceInstallParams failed (%Rwc)\n", GetLastError()));
                        break;
                    }
                }
                else
                {
                    NonStandardLogFlow(("SetupDiSetDeviceInstallParams faileed: INF path is too long\n"));
                    break;
                }
            }
            else
                NonStandardLogFlow(("SetupDiGetDeviceInstallParams failed (%Rwc)\n", GetLastError()));
        }

        /* build a list of class drivers */
        if (!SetupDiBuildDriverInfoList(hDeviceInfo, &DeviceInfoData, SPDIT_CLASSDRIVER))
            SetErrBreak(("SetupDiBuildDriverInfoList failed (%Rwc)", GetLastError()));

        destroyList = TRUE;

        /*
         * Enumerate the driver info list.
         */
        /* For our purposes, 2k buffer is more than enough to obtain the
           hardware ID of the VBoxNetAdp driver. */ /** @todo r=bird: The buffer isn't 2KB, it's 8KB, but whatever. */
        DWORD           detailBuf[2048];
        SP_DRVINFO_DATA DriverInfoData = { sizeof(DriverInfoData) };
        bool            fFound         = false;
        for (DWORD index = 0; !fFound; index++)
        {
            /* If the function fails with last error set to ERROR_NO_MORE_ITEMS,
               then we have reached the end of the list.  Otherwise there was
               something wrong with this particular driver. */
            if (!SetupDiEnumDriverInfo(hDeviceInfo, &DeviceInfoData, SPDIT_CLASSDRIVER, index, &DriverInfoData))
            {
                if (GetLastError() == ERROR_NO_MORE_ITEMS)
                    break;
                continue;
            }

            /* if we successfully find the hardware ID and it turns out to
             * be the one for the loopback driver, then we are done. */
            PSP_DRVINFO_DETAIL_DATA_W pDriverInfoDetail = (PSP_DRVINFO_DETAIL_DATA_W)detailBuf;
            pDriverInfoDetail->cbSize = sizeof(SP_DRVINFO_DETAIL_DATA);
            DWORD cbValue = 0;
            if (SetupDiGetDriverInfoDetailW(hDeviceInfo,
                                            &DeviceInfoData,
                                            &DriverInfoData,
                                            pDriverInfoDetail,
                                            sizeof(detailBuf) - sizeof(detailBuf[0]),
                                            &cbValue))
            {
                /* Sure that the HardwareID string list is properly zero terminated (paranoia). */
                detailBuf[RT_ELEMENTS(detailBuf) - 1] = 0; AssertCompile(sizeof(detailBuf[0]) == sizeof(WCHAR) * 2);

                /* pDriverInfoDetail->HardwareID is a MULTISZ string.  Go through the whole
                   list and see if there is a match somewhere: */
                for (WCHAR *pwszCurHwId = pDriverInfoDetail->HardwareID;
                     (uintptr_t)pwszCurHwId - (uintptr_t)pDriverInfoDetail < cbValue && *pwszCurHwId != L'\0';
                     pwszCurHwId += RTUtf16Len(pwszCurHwId) + 1)
                    if (RTUtf16ICmp(DRIVERHWID, pwszCurHwId) == 0)
                    {
                        fFound = true;
                        break;
                    }
            }
        }

        if (!fFound)
            SetErrBreak(("Could not find Host Interface Networking driver! Please reinstall"));

        /* set the loopback driver to be the currently selected */
        if (!SetupDiSetSelectedDriver(hDeviceInfo, &DeviceInfoData, &DriverInfoData))
            SetErrBreak(("SetupDiSetSelectedDriver failed (%u)", GetLastError()));

        /* register the phantom device to prepare for install */
        if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, hDeviceInfo, &DeviceInfoData))
            SetErrBreak(("SetupDiCallClassInstaller failed (%u)", GetLastError()));

        /* registered, but remove if errors occur in the following code */
        fRegistered = TRUE;

        /* ask the installer if we can install the device */
        if (!SetupDiCallClassInstaller(DIF_ALLOW_INSTALL, hDeviceInfo, &DeviceInfoData))
        {
            if (GetLastError() != ERROR_DI_DO_DEFAULT)
                SetErrBreak(("SetupDiCallClassInstaller (DIF_ALLOW_INSTALL) failed (%u)", GetLastError()));
            /* that's fine */
        }

        /* get the device install parameters and disable filecopy */
        DeviceInstallParams.cbSize = sizeof(SP_DEVINSTALL_PARAMS);
        if (SetupDiGetDeviceInstallParams(hDeviceInfo, &DeviceInfoData, &DeviceInstallParams))
        {
            pQueueCallbackContext = SetupInitDefaultQueueCallback(NULL);
            if (pQueueCallbackContext)
            {
                DeviceInstallParams.InstallMsgHandlerContext = pQueueCallbackContext;
                DeviceInstallParams.InstallMsgHandler = (PSP_FILE_CALLBACK)vboxNetCfgWinPspFileCallback;
                if (!SetupDiSetDeviceInstallParamsW(hDeviceInfo, &DeviceInfoData, &DeviceInstallParams))
                {
                    DWORD winEr = GetLastError();
                    NonStandardLogFlow(("SetupDiSetDeviceInstallParamsW failed, winEr (%d)\n", winEr));
                    Assert(0);
                }
            }
            else
            {
                DWORD winEr = GetLastError();
                NonStandardLogFlow(("SetupInitDefaultQueueCallback failed, winEr (%d)\n", winEr));
            }
        }
        else
        {
            DWORD winEr = GetLastError();
            NonStandardLogFlow(("SetupDiGetDeviceInstallParams failed, winEr (%d)\n", winEr));
        }

        /* install the files first */
        if (!SetupDiCallClassInstaller(DIF_INSTALLDEVICEFILES, hDeviceInfo, &DeviceInfoData))
            SetErrBreak(("SetupDiCallClassInstaller (DIF_INSTALLDEVICEFILES) failed (%Rwc)", GetLastError()));

        /* get the device install parameters and disable filecopy */
        DeviceInstallParams.cbSize = sizeof(SP_DEVINSTALL_PARAMS);
        if (SetupDiGetDeviceInstallParams(hDeviceInfo, &DeviceInfoData, &DeviceInstallParams))
        {
            DeviceInstallParams.Flags |= DI_NOFILECOPY;
            if (!SetupDiSetDeviceInstallParamsW(hDeviceInfo, &DeviceInfoData, &DeviceInstallParams))
                SetErrBreak(("SetupDiSetDeviceInstallParamsW failed (%Rwc)", GetLastError()));
        }
        /** @todo r=bird: Why isn't SetupDiGetDeviceInstallParams failure fatal here? */

        /*
         * Register any device-specific co-installers for this device,
         */
        if (!SetupDiCallClassInstaller(DIF_REGISTER_COINSTALLERS, hDeviceInfo, &DeviceInfoData))
            SetErrBreak(("SetupDiCallClassInstaller (DIF_REGISTER_COINSTALLERS) failed (%Rwc)", GetLastError()));

        /*
         * install any installer-specified interfaces.
         * and then do the real install
         */
        if (!SetupDiCallClassInstaller(DIF_INSTALLINTERFACES, hDeviceInfo, &DeviceInfoData))
            SetErrBreak(("SetupDiCallClassInstaller (DIF_INSTALLINTERFACES) failed (%Rwc)", GetLastError()));

        if (!SetupDiCallClassInstaller(DIF_INSTALLDEVICE, hDeviceInfo, &DeviceInfoData))
            SetErrBreak(("SetupDiCallClassInstaller (DIF_INSTALLDEVICE) failed (%Rwc)", GetLastError()));

        /*
         * Query the instance ID; on Windows 10, the registry key may take a short
         * while to appear. Microsoft recommends waiting for up to 5 seconds, but
         * we want to be on the safe side, so let's wait for 20 seconds. Waiting
         * longer is harmful as network setup service will shut down after a period
         * of inactivity.
         */
        for (int retries = 0; retries < 2 * 20; ++retries)
        {
            Sleep(500); /* half second */

            /* Figure out NetCfgInstanceId */
            hkey = SetupDiOpenDevRegKey(hDeviceInfo, &DeviceInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
            if (hkey == INVALID_HANDLE_VALUE)
                break;

            DWORD cbSize       = sizeof(wszCfgGuidString);
            DWORD dwValueType = 0;
            lrcRet = RegQueryValueExW(hkey, L"NetCfgInstanceId", NULL, &dwValueType, (LPBYTE)wszCfgGuidString, &cbSize);
            /* As long as the return code is FILE_NOT_FOUND, sleep and retry. */
            if (lrcRet != ERROR_FILE_NOT_FOUND)
                break;

            RegCloseKey(hkey);
            hkey = (HKEY)INVALID_HANDLE_VALUE;
        }

        if (lrcRet == ERROR_FILE_NOT_FOUND)
        {
            hrc = E_ABORT;
            break;
        }

        /*
         * We need to check 'hkey' after we check 'lrcRet' to distinguish the case
         * of failed SetupDiOpenDevRegKey from the case when we timed out.
         */
        if (hkey == INVALID_HANDLE_VALUE)
            SetErrBreak(("SetupDiOpenDevRegKey failed (%Rwc)", GetLastError()));

        if (lrcRet != ERROR_SUCCESS)
            SetErrBreak(("Querying NetCfgInstanceId failed (%Rwc)", lrcRet));

        NET_LUID luid;
        HRESULT hrcSMRes = vboxNetCfgWinGetInterfaceLUID(hkey, &luid);

        /* Close the key as soon as possible. See @bugref{7973}. */
        RegCloseKey(hkey);
        hkey = (HKEY)INVALID_HANDLE_VALUE;

        if (FAILED(hrcSMRes))
        {
            /*
             * The setting of Metric is not very important functionality,
             * So we will not break installation process due to this error.
             */
            NonStandardLogFlow(("vboxNetCfgWinCreateHostOnlyNetworkInterface: Warning! "
                                "vboxNetCfgWinGetInterfaceLUID failed, default metric for new interface will not be set: %Rhrc\n",
                                hrcSMRes));
        }
        else
        {
            /*
             * Set default metric value of interface to fix multicast issue
             * See @bugref{6379} for details.
             */
            hrcSMRes = vboxNetCfgWinSetupMetric(&luid);
            if (FAILED(hrcSMRes))
            {
                /*
                 * The setting of Metric is not very important functionality,
                 * So we will not break installation process due to this error.
                 */
                NonStandardLogFlow(("vboxNetCfgWinCreateHostOnlyNetworkInterface: Warning! "
                                    "vboxNetCfgWinSetupMetric failed, default metric for new interface will not be set: %Rhrc\n",
                                    hrcSMRes));
            }
        }

        /*
         * We need to query the device name after we have succeeded in querying its
         * instance ID to avoid similar waiting-and-retrying loop (see @bugref{7973}).
         */
        if (!SetupDiGetDeviceRegistryPropertyW(hDeviceInfo, &DeviceInfoData,
                                               SPDRP_FRIENDLYNAME , /* IN DWORD Property,*/
                                               NULL, /*OUT PDWORD PropertyRegDataType, OPTIONAL*/
                                               (PBYTE)wszDevName, /*OUT PBYTE PropertyBuffer,*/
                                               sizeof(wszDevName) - sizeof(WCHAR), /* IN DWORD PropertyBufferSize,*/
                                               NULL /*OUT PDWORD RequiredSize OPTIONAL*/ ))
        {
            DWORD dwErr = GetLastError();
            if (dwErr != ERROR_INVALID_DATA)
                SetErrBreak(("SetupDiGetDeviceRegistryProperty failed (%Rwc)", dwErr));

            RT_ZERO(wszDevName);
            if (!SetupDiGetDeviceRegistryPropertyW(hDeviceInfo, &DeviceInfoData,
                                                   SPDRP_DEVICEDESC, /* IN DWORD Property,*/
                                                   NULL, /*OUT PDWORD PropertyRegDataType, OPTIONAL*/
                                                   (PBYTE)wszDevName, /*OUT PBYTE PropertyBuffer,*/
                                                   sizeof(wszDevName) - sizeof(WCHAR), /* IN DWORD PropertyBufferSize,*/
                                                   NULL /*OUT PDWORD RequiredSize OPTIONAL*/ ))
                SetErrBreak(("SetupDiGetDeviceRegistryProperty failed (%Rwc)", GetLastError()));
        }

        /* No need to rename the device if the names match. */
        if (RTUtf16Cmp(bstrNewInterfaceName.raw(), wszDevName) == 0)
            bstrNewInterfaceName.setNull();

#ifdef VBOXNETCFG_DELAYEDRENAME
        /* Re-use wszDevName for device instance id retrieval. */
        DWORD cwcReturned = 0;
        RT_ZERO(wszDevName);
        if (!SetupDiGetDeviceInstanceIdW(hDeviceInfo, &DeviceInfoData, wszDevName, RT_ELEMENTS(wszDevName) - 1, &cwcReturned))
            SetErrBreak(("SetupDiGetDeviceInstanceId failed (%Rwc)", GetLastError()));
#endif /* VBOXNETCFG_DELAYEDRENAME */
    } while (0);

    /*
     * Cleanup.
     */
    if (hkey != INVALID_HANDLE_VALUE)
        RegCloseKey(hkey);

    if (pQueueCallbackContext)
        SetupTermDefaultQueueCallback(pQueueCallbackContext);

    if (hDeviceInfo != INVALID_HANDLE_VALUE)
    {
        /* an error has occurred, but the device is registered, we must remove it */
        if (lrcRet != ERROR_SUCCESS && fRegistered)
            SetupDiCallClassInstaller(DIF_REMOVE, hDeviceInfo, &DeviceInfoData);

        SetupDiDeleteDeviceInfo(hDeviceInfo, &DeviceInfoData);

        /* destroy the driver info list */
#if 0
        /* I've remove this, as I was consistently getting crashes in
         * SetupDiDestroyDeviceInfoList otherwise during MSI installation
         * (W10 build 19044, VBox r153431 + nocrt changes).
         *
         * Some details from windbg:
         *
         * (175e8.1596c): Access violation - code c0000005 (first chance)
         * First chance exceptions are reported before any exception handling.
         * This exception may be expected and handled.
         * SETUPAPI!DereferenceClassDriverList+0x4e:
         * 00007ffa`83e2a42a 834008ff        add     dword ptr [rax+8],0FFFFFFFFh ds:00000000`00000008=????????
         * 0:006> k
         *  # Child-SP          RetAddr           Call Site
         * 00 0000007e`ccd7c070 00007ffa`83e2a287 SETUPAPI!DereferenceClassDriverList+0x4e
         * 01 0000007e`ccd7c0a0 00007ffa`56b96bd3 SETUPAPI!SetupDiDestroyDriverInfoList+0x117
         * 02 0000007e`ccd7c0f0 00007ffa`56b982a3 MSIF170!vboxNetCfgWinCreateHostOnlyNetworkInterface+0xb23 [E:\vbox\svn\trunk\src\VBox\HostDrivers\VBoxNetFlt\win\cfg\VBoxNetCfg.cpp @ 3378]
         * 03 0000007e`ccd7ef10 00007ffa`56b92cb8 MSIF170!VBoxNetCfgWinCreateHostOnlyNetworkInterface+0x53 [E:\vbox\svn\trunk\src\VBox\HostDrivers\VBoxNetFlt\win\cfg\VBoxNetCfg.cpp @ 3479]
         * 04 0000007e`ccd7efc0 00007ffa`610f59d3 MSIF170!_createHostOnlyInterface+0x218 [E:\vbox\svn\trunk\src\VBox\Installer\win\InstallHelper\VBoxInstallHelper.cpp @ 1453]
         * 05 0000007e`ccd7f260 00007ffa`610d80ac msi!CallCustomDllEntrypoint+0x2b
         * 06 0000007e`ccd7f2d0 00007ffa`84567034 msi!CMsiCustomAction::CustomActionThread+0x34c
         * 07 0000007e`ccd7f8c0 00007ffa`849a2651 KERNEL32!BaseThreadInitThunk+0x14
         * 08 0000007e`ccd7f8f0 00000000`00000000 ntdll!RtlUserThreadStart+0x21
         * 0:006> r
         * rax=0000000000000000 rbx=0000025f4f03af90 rcx=00007ffa83f23b40
         * rdx=0000025f4f03af90 rsi=0000025f5108be50 rdi=0000025f4f066f10
         * rip=00007ffa83e2a42a rsp=0000007eccd7c070 rbp=00007ffa83f23000
         *  r8=0000007eccd7c0a8  r9=0000007eccd7c1f0 r10=0000000000000000
         * r11=0000007eccd7c020 r12=0000007eccd7c3d0 r13=00007ffa83f23000
         * r14=0000000000000000 r15=0000025f4f03af90
         * iopl=0         nv up ei pl nz na po nc
         * cs=0033  ss=002b  ds=002b  es=002b  fs=0053  gs=002b             efl=00010206
         * SETUPAPI!DereferenceClassDriverList+0x4e:
         * 00007ffa`83e2a42a 834008ff        add     dword ptr [rax+8],0FFFFFFFFh ds:00000000`00000008=????????
         *
         */
        if (destroyList)
            SetupDiDestroyDriverInfoList(hDeviceInfo, &DeviceInfoData, SPDIT_CLASSDRIVER);
#else
        RT_NOREF(destroyList);
#endif

        /* clean up the device info set */
        SetupDiDestroyDeviceInfoList(hDeviceInfo);
    }

    /*
     * Return the network connection GUID on success.
     */
    if (SUCCEEDED(hrc))
    {
        /** @todo r=bird: Some please explain this mess. It's not just returning a
         *        GUID here, it's a bit more than that. */
        RENAMING_CONTEXT context;
        context.hr = E_FAIL;

        if (pGuid)
        {
            hrc = CLSIDFromString(wszCfgGuidString, (LPCLSID)pGuid);
            if (FAILED(hrc))
                NonStandardLogFlow(("CLSIDFromString failed, hrc (0x%x)\n", hrc));
        }

        INetCfg *pNetCfg = NULL;
        LPWSTR   pwszApp = NULL;
        HRESULT hrc2 = VBoxNetCfgWinQueryINetCfg(&pNetCfg, TRUE, L"VirtualBox Host-Only Creation",
                                                 30 * 1000, /* on Vista we often get 6to4svc.dll holding the lock, wait for 30 sec.  */
                                                 /** @todo special handling for 6to4svc.dll ???, i.e. several retrieves */
                                                 &pwszApp);
        if (hrc2 == S_OK)
        {
            if (bstrNewInterfaceName.isNotEmpty())
            {
                /* The assigned name does not match the desired one, rename the device */
                context.bstrName = bstrNewInterfaceName.raw();
                context.pGuid    = pGuid;
                hrc2 = vboxNetCfgWinEnumNetCfgComponents(pNetCfg, &GUID_DEVCLASS_NET,
                                                         vboxNetCfgWinRenameHostOnlyNetworkInterface, &context);
            }
            if (SUCCEEDED(hrc2))
                hrc2 = vboxNetCfgWinEnumNetCfgComponents(pNetCfg, &GUID_DEVCLASS_NETSERVICE,
                                                         vboxNetCfgWinAdjustHostOnlyNetworkInterfacePriority, pGuid);
            if (SUCCEEDED(hrc2))
                hrc2 = vboxNetCfgWinEnumNetCfgComponents(pNetCfg, &GUID_DEVCLASS_NETTRANS,
                                                         vboxNetCfgWinAdjustHostOnlyNetworkInterfacePriority, pGuid);
            if (SUCCEEDED(hrc2))
                hrc2 = vboxNetCfgWinEnumNetCfgComponents(pNetCfg, &GUID_DEVCLASS_NETCLIENT,
                                                         vboxNetCfgWinAdjustHostOnlyNetworkInterfacePriority, pGuid);
            if (SUCCEEDED(hrc2))
                hrc2 = pNetCfg->Apply();
            else
                NonStandardLogFlow(("Enumeration failed, hrc2=%Rhrc\n", hrc2));

            VBoxNetCfgWinReleaseINetCfg(pNetCfg, TRUE);
        }
        else if (hrc2 == NETCFG_E_NO_WRITE_LOCK && pwszApp)
        {
            NonStandardLogFlow(("Application '%ls' is holding the lock, failed\n", pwszApp));
            CoTaskMemFree(pwszApp);
            pwszApp = NULL;
        }
        else
            NonStandardLogFlow(("VBoxNetCfgWinQueryINetCfg failed, hrc2=%Rhrc\n", hrc2));

#ifndef VBOXNETCFG_DELAYEDRENAME
        /* If the device has been successfully renamed, replace the name now. */
        if (SUCCEEDED(hrc2) && SUCCEEDED(context.hr))
            RTUtf16Copy(wszDevName, RT_ELEMENTS(wszDevName), pBstrDesiredName);

        WCHAR wszConnectionName[128];
        hrc2 = VBoxNetCfgWinGenHostonlyConnectionName(wszDevName, wszConnectionName, RT_ELEMENTS(wszConnectionName), NULL);
        if (SUCCEEDED(hrc2))
            hrc2 = VBoxNetCfgWinRenameConnection(wszCfgGuidString, wszConnectionName);
#endif

        /*
         * Now, return the network connection GUID/name.
         */
        if (pBstrName)
        {
            *pBstrName = SysAllocString((const OLECHAR *)wszDevName);
            if (!*pBstrName)
            {
                NonStandardLogFlow(("SysAllocString failed\n"));
                hrc = E_OUTOFMEMORY;
            }
        }
    }

    if (pBstrErrMsg)
    {
        *pBstrErrMsg = NULL;
        if (bstrError.isNotEmpty())
            bstrError.detachToEx(pBstrErrMsg);
    }
    return hrc;
}

VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinCreateHostOnlyNetworkInterface(IN LPCWSTR pwszInfPath, IN bool fIsInfPathFile,
                                                                        IN BSTR pwszDesiredName, OUT GUID *pGuid,
                                                                        OUT BSTR *pBstrName, OUT BSTR *pBstrErrMsg)
{
    HRESULT hrc = vboxNetCfgWinCreateHostOnlyNetworkInterface(pwszInfPath, fIsInfPathFile, pwszDesiredName,
                                                              pGuid, pBstrName, pBstrErrMsg);
    if (hrc == E_ABORT)
    {
        NonStandardLogFlow(("Timed out while waiting for NetCfgInstanceId, try again immediately...\n"));

        /*
         * This is the first time we fail to obtain NetCfgInstanceId, let us
         * retry it once. It is needed to handle the situation when network
         * setup fails to recognize the arrival of our device node while it
         * is busy removing another host-only interface, and it gets stuck
         * with no matching network interface created for our device node.
         * See @bugref{7973} for details.
         */
        hrc = vboxNetCfgWinCreateHostOnlyNetworkInterface(pwszInfPath, fIsInfPathFile, pwszDesiredName,
                                                          pGuid, pBstrName, pBstrErrMsg);
        if (hrc == E_ABORT)
        {
            NonStandardLogFlow(("Timed out again while waiting for NetCfgInstanceId, try again after a while...\n"));

            /*
             * This is the second time we fail to obtain NetCfgInstanceId, let us
             * retry it once more. This time we wait to network setup service
             * to go down before retrying. Hopefully it will resolve all error
             * conditions. See @bugref{7973} for details.
             */
            SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, GENERIC_READ);
            if (hSCM)
            {
                SC_HANDLE hService = OpenServiceW(hSCM, L"NetSetupSvc", GENERIC_READ);
                if (hService)
                {
                    for (int retries = 0; retries < 60 && !vboxNetCfgWinIsNetSetupStopped(hService); ++retries)
                        Sleep(1000);
                    CloseServiceHandle(hService);
                    hrc = vboxNetCfgWinCreateHostOnlyNetworkInterface(pwszInfPath, fIsInfPathFile, pwszDesiredName,
                                                                      pGuid, pBstrName, pBstrErrMsg);
                }
                else
                    NonStandardLogFlow(("OpenService failed (%Rwc)\n", GetLastError()));
                CloseServiceHandle(hSCM);
            }
            else
                NonStandardLogFlow(("OpenSCManager failed (%Rwc)", GetLastError()));

            /* Give up and report the error. */
            if (hrc == E_ABORT)
            {
                if (pBstrErrMsg)
                {
                    com::Bstr bstrError;
                    bstrError.printfNoThrow("Querying NetCfgInstanceId failed (ERROR_FILE_NOT_FOUND)");
                    bstrError.detachToEx(pBstrErrMsg);
                }
                hrc = E_FAIL;
            }
        }
    }
    return hrc;
}

static HRESULT vboxNetCfgWinGetLoopbackMetric(OUT DWORD *Metric)
{
    Assert(g_pfnInitializeIpInterfaceEntry != NULL);
    Assert(g_pfnGetIpInterfaceEntry != NULL);

    MIB_IPINTERFACE_ROW row;
    RT_ZERO(row); /* paranoia */
    g_pfnInitializeIpInterfaceEntry(&row);

    row.Family = AF_INET;
    row.InterfaceLuid.Info.IfType = IF_TYPE_SOFTWARE_LOOPBACK;

    NETIO_STATUS dwErr = g_pfnGetIpInterfaceEntry(&row);
    if (dwErr == NO_ERROR)
    {
        *Metric = row.Metric;
        return S_OK;
    }
    return HRESULT_FROM_WIN32(dwErr);
}

static HRESULT vboxNetCfgWinSetInterfaceMetric(IN NET_LUID *pInterfaceLuid, IN DWORD metric)
{
    Assert(g_pfnInitializeIpInterfaceEntry != NULL);
    Assert(g_pfnSetIpInterfaceEntry != NULL);

    MIB_IPINTERFACE_ROW newRow;
    RT_ZERO(newRow); /* paranoia */
    g_pfnInitializeIpInterfaceEntry(&newRow);

    // identificate the interface to change
    newRow.InterfaceLuid = *pInterfaceLuid;
    newRow.Family = AF_INET;

    // changed settings
    newRow.UseAutomaticMetric = false;
    newRow.Metric = metric;

    // change settings
    NETIO_STATUS dwErr = g_pfnSetIpInterfaceEntry(&newRow);
    if (dwErr == NO_ERROR)
        return S_OK;
    return HRESULT_FROM_WIN32(dwErr);
}

static HRESULT vboxNetCfgWinSetupMetric(IN NET_LUID* pLuid)
{
    HRESULT  hrc  = E_FAIL;
    HMODULE  hmod = loadSystemDll(L"Iphlpapi.dll");
    if (hmod)
    {
        g_pfnInitializeIpInterfaceEntry = (PFNINITIALIZEIPINTERFACEENTRY)GetProcAddress(hmod, "InitializeIpInterfaceEntry");
        g_pfnGetIpInterfaceEntry        = (PFNGETIPINTERFACEENTRY)       GetProcAddress(hmod, "GetIpInterfaceEntry");
        g_pfnSetIpInterfaceEntry        = (PFNSETIPINTERFACEENTRY)       GetProcAddress(hmod, "SetIpInterfaceEntry");
        Assert(g_pfnInitializeIpInterfaceEntry);
        Assert(g_pfnGetIpInterfaceEntry);
        Assert(g_pfnSetIpInterfaceEntry);

        if (   g_pfnInitializeIpInterfaceEntry
            && g_pfnGetIpInterfaceEntry
            && g_pfnSetIpInterfaceEntry)
        {
            DWORD loopbackMetric = 0;
            hrc = vboxNetCfgWinGetLoopbackMetric(&loopbackMetric);
            if (SUCCEEDED(hrc))
                hrc = vboxNetCfgWinSetInterfaceMetric(pLuid, loopbackMetric - 1);
        }

        g_pfnInitializeIpInterfaceEntry = NULL;
        g_pfnSetIpInterfaceEntry        = NULL;
        g_pfnGetIpInterfaceEntry        = NULL;

        FreeLibrary(hmod);
    }
    return hrc;
}

static HRESULT vboxNetCfgWinGetInterfaceLUID(IN HKEY hKey, OUT NET_LUID *pLUID)
{
    if (pLUID == NULL)
        return E_INVALIDARG;

    DWORD dwLuidIndex = 0;
    DWORD cbSize      = sizeof(dwLuidIndex);
    DWORD dwValueType = REG_DWORD; /** @todo r=bird: This is output only. No checked after the call. So, only for debugging? */
    LSTATUS lrc = RegQueryValueExW(hKey, L"NetLuidIndex", NULL, &dwValueType, (LPBYTE)&dwLuidIndex, &cbSize);
    if (lrc == ERROR_SUCCESS)
    {
        DWORD dwIfType = 0;
        cbSize         = sizeof(dwIfType);
        dwValueType    = REG_DWORD; /** @todo r=bird: This is output only. No checked after the call. So, only for debugging? */
        lrc = RegQueryValueExW(hKey, L"*IfType", NULL, &dwValueType, (LPBYTE)&dwIfType, &cbSize);
        if (lrc == ERROR_SUCCESS)
        {
            RT_ZERO(*pLUID);
            pLUID->Info.IfType       = dwIfType;
            pLUID->Info.NetLuidIndex = dwLuidIndex;
            return S_OK;
        }
    }

    RT_ZERO(*pLUID);
    return HRESULT_FROM_WIN32(lrc);
}


#ifdef VBOXNETCFG_DELAYEDRENAME
VBOXNETCFGWIN_DECL(HRESULT) VBoxNetCfgWinRenameHostOnlyConnection(IN const GUID *pGuid, IN LPCWSTR pwszId, OUT BSTR *pDevName)
{
    if (pDevName)
        *pDevName = NULL;

    HRESULT  hr = S_OK; /** @todo r=bird: ODD return status for SetupDiCreateDeviceInfoList failures! */
    HDEVINFO hDevInfo = SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_NET, NULL);
    if (hDevInfo != INVALID_HANDLE_VALUE)
    {
        SP_DEVINFO_DATA DevInfoData = { sizeof(SP_DEVINFO_DATA) };
        if (SetupDiOpenDeviceInfo(hDevInfo, pwszId, NULL, 0, &DevInfoData))
        {
            WCHAR wszDevName[256 + 1] = {0};
            DWORD err = ERROR_SUCCESS;
            if (!SetupDiGetDeviceRegistryPropertyW(hDevInfo, &DevInfoData, SPDRP_FRIENDLYNAME, NULL /*PropertyRegDataType*/,
                                                   (PBYTE)wszDevName, sizeof(wszDevName) - sizeof(WCHAR) /*yes, bytes*/,
                                                   NULL /*RequiredSize*/))
            {
                err = GetLastError();
                if (err == ERROR_INVALID_DATA)
                {
                    RT_ZERO(wszDevName);
                    if (SetupDiGetDeviceRegistryPropertyW(hDevInfo, &DevInfoData, SPDRP_DEVICEDESC, NULL /*PropertyRegDataType*/,
                                                          (PBYTE)wszDevName, sizeof(wszDevName) - sizeof(WCHAR) /*yes, bytes*/,
                                                          NULL /*RequiredSize*/))
                        err = ERROR_SUCCESS;
                    else
                        err = GetLastError();
                }
            }
            if (err == ERROR_SUCCESS)
            {
                WCHAR wszConnectionNewName[128] = {0};
                hr = VBoxNetCfgWinGenHostonlyConnectionName(wszDevName, wszConnectionNewName,
                                                            RT_ELEMENTS(wszConnectionNewName), NULL);
                if (SUCCEEDED(hr))
                {
                    WCHAR wszGuid[50];
                    int cbWGuid = StringFromGUID2(*pGuid, wszGuid, RT_ELEMENTS(wszGuid));
                    if (cbWGuid)
                    {
                        hr = VBoxNetCfgWinRenameConnection(wszGuid, wszConnectionNewName);
                        if (FAILED(hr))
                            NonStandardLogFlow(("VBoxNetCfgWinRenameHostOnlyConnection: VBoxNetCfgWinRenameConnection failed (0x%x)\n", hr));
                    }
                    else
                    {
                        err = GetLastError();
                        hr = HRESULT_FROM_WIN32(err);
                        if (SUCCEEDED(hr))
                            hr = E_FAIL;
                        NonStandardLogFlow(("StringFromGUID2 failed err=%u, hr=0x%x\n", err, hr));
                    }
                }
                else
                    NonStandardLogFlow(("VBoxNetCfgWinRenameHostOnlyConnection: VBoxNetCfgWinGenHostonlyConnectionName failed (0x%x)\n", hr));
                if (SUCCEEDED(hr) && pDevName)
                {
                    *pDevName = SysAllocString((const OLECHAR *)wszDevName);
                    if (!*pDevName)
                    {
                        NonStandardLogFlow(("SysAllocString failed\n"));
                        hr = HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
                    }
                }
            }
            else
            {
                hr = HRESULT_FROM_WIN32(err);
                NonStandardLogFlow(("VBoxNetCfgWinRenameHostOnlyConnection: SetupDiGetDeviceRegistryPropertyW failed (0x%x)\n", err));
            }
        }
        else
        {
            DWORD err = GetLastError();
            hr = HRESULT_FROM_WIN32(err);
            NonStandardLogFlow(("VBoxNetCfgWinRenameHostOnlyConnection: SetupDiOpenDeviceInfo failed (0x%x)\n", err));
        }
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }

    return hr;
}
#endif /* VBOXNETCFG_DELAYEDRENAME */

#undef SetErrBreak

