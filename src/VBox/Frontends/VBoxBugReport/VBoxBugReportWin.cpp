/* $Id: VBoxBugReportWin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxBugReportWin - VirtualBox command-line diagnostics tool, Windows-specific part.
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

#include <VBox/com/com.h>
#include <VBox/com/string.h>

#include <iprt/cpp/exception.h>

#include "VBoxBugReport.h"

#include <netcfgx.h>
#include <iprt/win/setupapi.h>
#include <initguid.h>
#include <devguid.h>
#include <usbiodef.h>
#include <usbioctl.h>
#include <psapi.h>

#define ReleaseAndReset(obj) \
    if (obj) \
        obj->Release(); \
    obj = NULL;


class BugReportNetworkAdaptersWin : public BugReportStream
{
public:
    BugReportNetworkAdaptersWin() : BugReportStream("NetworkAdapters") {};
    virtual ~BugReportNetworkAdaptersWin() {};
    virtual RTVFSIOSTREAM getStream(void) { collect(); return BugReportStream::getStream(); };
private:
    struct CharacteristicsName
    {
        DWORD dwChar;
        const char *szName;
    };
    void printCharteristics(DWORD dwChars);
    void collect();
    void collectNetCfgComponentInfo(int ident, bool fEnabled, INetCfgComponent *pComponent);
};



void BugReportNetworkAdaptersWin::printCharteristics(DWORD dwChars)
{
    static CharacteristicsName cMap[] =
    {
        { NCF_VIRTUAL, "virtual" },
        { NCF_SOFTWARE_ENUMERATED, "software_enumerated" },
        { NCF_PHYSICAL, "physical" },
        { NCF_HIDDEN, "hidden" },
        { NCF_NO_SERVICE, "no_service" },
        { NCF_NOT_USER_REMOVABLE, "not_user_removable" },
        { NCF_MULTIPORT_INSTANCED_ADAPTER, "multiport_instanced_adapter" },
        { NCF_HAS_UI, "has_ui" },
        { NCF_SINGLE_INSTANCE, "single_instance" },
        { NCF_FILTER, "filter" },
        { NCF_DONTEXPOSELOWER, "dontexposelower" },
        { NCF_HIDE_BINDING, "hide_binding" },
        { NCF_NDIS_PROTOCOL, "ndis_protocol" },
        { NCF_FIXED_BINDING, "fixed_binding" },
        { NCF_LW_FILTER, "lw_filter" }
    };
    bool fPrintDelim = false;

    for (int i = 0; i < RT_ELEMENTS(cMap); ++i)
    {
        if (dwChars & cMap[i].dwChar)
        {
            if (fPrintDelim)
            {
                putStr(", ");
                fPrintDelim = false;
            }
            putStr(cMap[i].szName);
            fPrintDelim = true;
        }
    }
}

void BugReportNetworkAdaptersWin::collectNetCfgComponentInfo(int ident, bool fEnabled, INetCfgComponent *pComponent)
{
    LPWSTR pwszName = NULL;
    HRESULT hr = pComponent->GetDisplayName(&pwszName);
    if (FAILED(hr))
        throw RTCError(com::Utf8StrFmt("Failed to get component display name, hr=0x%x.\n", hr));
    printf("%s%c %ls [", RTCString(ident, ' ').c_str(), fEnabled ? '+' : '-', pwszName);
    if (pwszName)
        CoTaskMemFree(pwszName);

    DWORD dwChars = 0;
    hr = pComponent->GetCharacteristics(&dwChars);
    if (FAILED(hr))
        throw RTCError(com::Utf8StrFmt("Failed to get component characteristics, hr=0x%x.\n", hr));
    printCharteristics(dwChars);
    putStr("]\n");
}

void BugReportNetworkAdaptersWin::collect(void)
{
    INetCfg                     *pNetCfg          = NULL;
    IEnumNetCfgComponent        *pEnumAdapters    = NULL;
    INetCfgComponent            *pNetCfgAdapter   = NULL;
    INetCfgComponentBindings    *pAdapterBindings = NULL;
    IEnumNetCfgBindingPath      *pEnumBp          = NULL;
    INetCfgBindingPath          *pBp              = NULL;
    IEnumNetCfgBindingInterface *pEnumBi          = NULL;
    INetCfgBindingInterface     *pBi              = NULL;
    INetCfgComponent            *pUpperComponent  = NULL;

    try
    {
        HRESULT hr = CoCreateInstance(CLSID_CNetCfg, NULL, CLSCTX_INPROC_SERVER, IID_INetCfg, (PVOID*)&pNetCfg);
        if (FAILED(hr))
            throw RTCError(com::Utf8StrFmt("Failed to create instance of INetCfg, hr=0x%x.\n", hr));
        hr = pNetCfg->Initialize(NULL);
        if (FAILED(hr))
            throw RTCError(com::Utf8StrFmt("Failed to initialize instance of INetCfg, hr=0x%x.\n", hr));

        hr = pNetCfg->EnumComponents(&GUID_DEVCLASS_NET, &pEnumAdapters);
        if (FAILED(hr))
            throw RTCError(com::Utf8StrFmt("Failed enumerate network adapters, hr=0x%x.\n", hr));

        hr = pEnumAdapters->Reset();
        Assert(SUCCEEDED(hr));
        do
        {
            hr = pEnumAdapters->Next(1, &pNetCfgAdapter, NULL);
            if (hr == S_FALSE)
                break;
            if (hr != S_OK)
                throw RTCError(com::Utf8StrFmt("Failed to get next network adapter, hr=0x%x.\n", hr));
            hr = pNetCfgAdapter->QueryInterface(IID_INetCfgComponentBindings, (PVOID*)&pAdapterBindings);
            if (FAILED(hr))
                throw RTCError(com::Utf8StrFmt("Failed to query INetCfgComponentBindings, hr=0x%x.\n", hr));
            hr = pAdapterBindings->EnumBindingPaths(EBP_ABOVE, &pEnumBp);
            if (FAILED(hr))
                throw RTCError(com::Utf8StrFmt("Failed to enumerate binding paths, hr=0x%x.\n", hr));
            hr = pEnumBp->Reset();
            if (FAILED(hr))
                throw RTCError(com::Utf8StrFmt("Failed to reset enumeration of binding paths (0x%x)\n", hr));
            do
            {
                hr = pEnumBp->Next(1, &pBp, NULL);
                if (hr == S_FALSE)
                    break;
                if (hr != S_OK)
                    throw RTCError(com::Utf8StrFmt("Failed to get next network adapter, hr=0x%x.\n", hr));
                bool fBpEnabled;
                hr =  pBp->IsEnabled();
                if (hr == S_FALSE)
                    fBpEnabled = false;
                else if (hr != S_OK)
                    throw RTCError(com::Utf8StrFmt("Failed to check if bind path is enabled, hr=0x%x.\n", hr));
                else
                    fBpEnabled = true;
                hr = pBp->EnumBindingInterfaces(&pEnumBi);
                if (FAILED(hr))
                    throw RTCError(com::Utf8StrFmt("Failed to enumerate binding interfaces (0x%x)\n", hr));
                hr = pEnumBi->Reset();
                if (FAILED(hr))
                    throw RTCError(com::Utf8StrFmt("Failed to reset enumeration of binding interfaces (0x%x)\n", hr));
                int ident;
                for (ident = 0;; ++ident)
                {
                    hr = pEnumBi->Next(1, &pBi, NULL);
                    if (hr == S_FALSE)
                        break;
                    if (hr != S_OK)
                        throw RTCError(com::Utf8StrFmt("Failed to get next binding interface, hr=0x%x.\n", hr));
                    hr = pBi->GetUpperComponent(&pUpperComponent);
                    if (FAILED(hr))
                        throw RTCError(com::Utf8StrFmt("Failed to get upper component, hr=0x%x.\n", hr));
                    collectNetCfgComponentInfo(ident, fBpEnabled, pUpperComponent);
                    ReleaseAndReset(pUpperComponent);
                    ReleaseAndReset(pBi);
                }
                collectNetCfgComponentInfo(ident, fBpEnabled, pNetCfgAdapter);
                ReleaseAndReset(pEnumBi);
                ReleaseAndReset(pBp);
            } while (true);

            ReleaseAndReset(pEnumBp);
            ReleaseAndReset(pAdapterBindings);
            ReleaseAndReset(pNetCfgAdapter);
        } while (true);
        ReleaseAndReset(pEnumAdapters);
        ReleaseAndReset(pNetCfg);
    }

    catch (RTCError &e)
    {
        ReleaseAndReset(pUpperComponent);
        ReleaseAndReset(pBi);
        ReleaseAndReset(pEnumBi);
        ReleaseAndReset(pBp);
        ReleaseAndReset(pEnumBp);
        ReleaseAndReset(pAdapterBindings);
        ReleaseAndReset(pNetCfgAdapter);
        ReleaseAndReset(pEnumAdapters);
        ReleaseAndReset(pNetCfg);
        RTPrintf("ERROR in osCollect: %s\n", e.what());
        throw;
    }

}


class ErrorHandler
{
public:
    ErrorHandler(const char *pszFunction, int iLine)
        : m_function(pszFunction), m_line(iLine)
    { }

    void handleWinError(DWORD uError, const char *pszMsgFmt, ...)
    {
        if (uError != ERROR_SUCCESS)
        {
            va_list va;
            va_start(va, pszMsgFmt);
            RTCString msgArgs(pszMsgFmt, va);
            va_end(va);

            LPSTR pBuf = NULL;
            FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           NULL, uError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&pBuf, 0, NULL);
            RTCStringFmt msg("%s at %s(%d): err=%u %s", msgArgs.c_str(), m_function, m_line, uError, pBuf);
            LocalFree(pBuf);
            throw RTCError(msg.c_str());
        }
    }

private:
    const char *m_function;
    int m_line;
};
#define handleWinError ErrorHandler(__FUNCTION__, __LINE__).handleWinError


class BugReportUsbTreeWin : public BugReportStream
{
public:
    BugReportUsbTreeWin();
    virtual ~BugReportUsbTreeWin();
    virtual RTVFSIOSTREAM getStream(void) { enumerate(); return BugReportStream::getStream(); }
private:
    class AutoHandle {
    public:
        AutoHandle(HANDLE h) { m_h = h; }
        ~AutoHandle() { close(); }
        bool isValid() { return m_h != INVALID_HANDLE_VALUE; }
        operator HANDLE() { return m_h; }
        void close(void)  {   if (isValid()) { CloseHandle(m_h); m_h = INVALID_HANDLE_VALUE; }    }
    private:
        HANDLE m_h;
    };
    void enumerate();

    void enumerateController(PSP_DEVINFO_DATA pInfoData, PSP_DEVICE_INTERFACE_DATA pInterfaceData);
    void enumerateHub(RTCString strFullName, RTCString strPrefix);
    void enumeratePorts(HANDLE hHub, unsigned cPorts, RTCString strPrefix);
    PBYTE getDeviceRegistryProperty(HDEVINFO hDev, PSP_DEVINFO_DATA pInfoData, DWORD uProperty,
                                    DWORD uExpectedType, PDWORD puSize);
    RTCString getDeviceRegistryPropertyString(HDEVINFO hDev, PSP_DEVINFO_DATA pInfoData, DWORD uProperty);

    RTCString getDeviceDescByDriverName(RTCString strDrvName);
    RTCString getDriverKeyName(HANDLE hHub, int iPort);
    RTCString getExternalHubName(HANDLE hHub, int iPort);

    HDEVINFO m_hDevInfo;
    PSP_DEVICE_INTERFACE_DETAIL_DATA m_pDetailData;
    HANDLE m_hHostCtrlDev;
};

BugReportUsbTreeWin::BugReportUsbTreeWin() : BugReportStream("HostUsbTree")
{
    m_hDevInfo = INVALID_HANDLE_VALUE;
    m_pDetailData = NULL;
    m_hHostCtrlDev = INVALID_HANDLE_VALUE;
}

BugReportUsbTreeWin::~BugReportUsbTreeWin()
{
    if (m_hHostCtrlDev != INVALID_HANDLE_VALUE)
        CloseHandle(m_hHostCtrlDev);
    if (m_pDetailData)
        RTMemFree(m_pDetailData);
    if (m_hDevInfo != INVALID_HANDLE_VALUE)
        SetupDiDestroyDeviceInfoList(m_hDevInfo);
}


PBYTE BugReportUsbTreeWin::getDeviceRegistryProperty(HDEVINFO hDev,
                                                     PSP_DEVINFO_DATA pInfoData,
                                                     DWORD uProperty,
                                                     DWORD uExpectedType,
                                                     PDWORD puSize)
{
    DWORD uActualType, cbNeeded = 0;
    if (!SetupDiGetDeviceRegistryProperty(hDev, pInfoData, uProperty, &uActualType,
                                          NULL, 0, &cbNeeded)
        && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        if (GetLastError() == ERROR_INVALID_DATA)
            return NULL;
        handleWinError(GetLastError(), "SetupDiGetDeviceRegistryProperty(0x%x) failed", uProperty);
    }
    if (uExpectedType != REG_NONE && uActualType != uExpectedType)
        throw RTCError(RTCStringFmt("SetupDiGetDeviceRegistryProperty(0x%x) returned type %d instead of %d",
                                    uActualType, uExpectedType).c_str());
    PBYTE pBuffer = (PBYTE)RTMemAlloc(cbNeeded);
    if (!pBuffer)
        throw RTCError(RTCStringFmt("Failed to allocate %u bytes", cbNeeded).c_str());
    if (!SetupDiGetDeviceRegistryProperty(hDev, pInfoData, uProperty, NULL,
                                          pBuffer, cbNeeded, &cbNeeded))
    {
        DWORD dwErr = GetLastError();
        RTMemFree(pBuffer);
        pBuffer = NULL;
        handleWinError(dwErr, "SetupDiGetDeviceRegistryProperty(0x%x) failed", uProperty);
    }
    if (puSize)
        *puSize = cbNeeded;

    return pBuffer;
}

RTCString BugReportUsbTreeWin::getDeviceRegistryPropertyString(HDEVINFO hDev, PSP_DEVINFO_DATA pInfoData, DWORD uProperty)
{
    PWSTR pUnicodeString = (PWSTR)getDeviceRegistryProperty(hDev, pInfoData, uProperty, REG_SZ, NULL);

    if (!pUnicodeString)
        return RTCString();

    RTCStringFmt utf8string("%ls", pUnicodeString);
    RTMemFree(pUnicodeString);
    return utf8string;
}


RTCString BugReportUsbTreeWin::getDeviceDescByDriverName(RTCString strDrvName)
{
    DWORD dwErr;
    SP_DEVINFO_DATA devInfoData;
    HDEVINFO hDevInfo = SetupDiGetClassDevs(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);

    if (hDevInfo == INVALID_HANDLE_VALUE)
        handleWinError(GetLastError(), "SetupDiGetClassDevs failed");

    bool fFound = false;
    devInfoData.cbSize = sizeof(devInfoData);
    for (int i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i)
    {
        if (getDeviceRegistryPropertyString(hDevInfo, &devInfoData, SPDRP_DRIVER).equals(strDrvName))
        {
            fFound = true;
            break;
        }
    }
    if (!fFound)
    {
        dwErr = GetLastError();
        SetupDiDestroyDeviceInfoList(hDevInfo);
        handleWinError(dwErr, "SetupDiEnumDeviceInfo failed");
    }

    RTCString strDesc = getDeviceRegistryPropertyString(hDevInfo, &devInfoData, SPDRP_DEVICEDESC);
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return strDesc;
}


RTCString BugReportUsbTreeWin::getDriverKeyName(HANDLE hHub, int iPort)
{
    USB_NODE_CONNECTION_DRIVERKEY_NAME name;
    ULONG cbNeeded = 0;

    name.ConnectionIndex = iPort;
    if (!DeviceIoControl(hHub, IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME,
                         &name, sizeof(name), &name, sizeof(name), &cbNeeded, NULL))
        handleWinError(GetLastError(), "DeviceIoControl(IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME) failed");
    cbNeeded = name.ActualLength;
    PUSB_NODE_CONNECTION_DRIVERKEY_NAME pName = (PUSB_NODE_CONNECTION_DRIVERKEY_NAME)RTMemAlloc(cbNeeded);
    if (!pName)
        throw RTCError(RTCStringFmt("Failed to allocate %u bytes", cbNeeded).c_str());
    pName->ConnectionIndex = iPort;
    if (!DeviceIoControl(hHub, IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME,
                         pName, cbNeeded, pName, cbNeeded, &cbNeeded, NULL))
    {
        DWORD dwErr = GetLastError();
        RTMemFree(pName);
        handleWinError(dwErr, "DeviceIoControl(IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME) failed");
    }
    RTCStringFmt strName("%ls", pName->DriverKeyName);
    RTMemFree(pName);
    return strName;
}


RTCString BugReportUsbTreeWin::getExternalHubName(HANDLE hHub, int iPort)
{
    USB_NODE_CONNECTION_NAME name;
    ULONG cbNeeded = 0;

    name.ConnectionIndex = iPort;
    if (!DeviceIoControl(hHub, IOCTL_USB_GET_NODE_CONNECTION_NAME,
                         &name, sizeof(name), &name, sizeof(name), &cbNeeded, NULL))
        handleWinError(GetLastError(), "DeviceIoControl(IOCTL_USB_GET_NODE_CONNECTION_NAME) failed");
    cbNeeded = name.ActualLength;
    PUSB_NODE_CONNECTION_NAME pName = (PUSB_NODE_CONNECTION_NAME)RTMemAlloc(cbNeeded);
    if (!pName)
        throw RTCError(RTCStringFmt("Failed to allocate %u bytes", cbNeeded).c_str());
    pName->ConnectionIndex = iPort;
    if (!DeviceIoControl(hHub, IOCTL_USB_GET_NODE_CONNECTION_NAME,
                         pName, cbNeeded, pName, cbNeeded, &cbNeeded, NULL))
    {
        DWORD dwErr = GetLastError();
        RTMemFree(pName);
        handleWinError(dwErr, "DeviceIoControl(IOCTL_USB_GET_NODE_CONNECTION_NAME) failed");
    }
    RTCStringFmt strName("%ls", pName->NodeName);
    RTMemFree(pName);
    return strName;
}


void BugReportUsbTreeWin::enumeratePorts(HANDLE hHub, unsigned cPorts, RTCString strPrefix)
{
    DWORD cbInfo = sizeof(USB_NODE_CONNECTION_INFORMATION_EX) + 30 * sizeof(USB_PIPE_INFO);
    PUSB_NODE_CONNECTION_INFORMATION_EX pInfo = (PUSB_NODE_CONNECTION_INFORMATION_EX)RTMemAlloc(cbInfo);
    if (!pInfo)
        throw RTCError(RTCStringFmt("Failed to allocate %u bytes", cbInfo).c_str());
    for (unsigned i = 1; i <= cPorts; ++i)
    {
        pInfo->ConnectionIndex = i;
        if (!DeviceIoControl(hHub, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
                             pInfo, cbInfo, pInfo, cbInfo, &cbInfo, NULL))
        {
            DWORD dwErr = GetLastError();
            RTMemFree(pInfo);
            handleWinError(dwErr, "DeviceIoControl(IOCTL_USB_GET_NODE_CONNECTION_INFORMATION) failed");
        }
        if (pInfo->ConnectionStatus == NoDeviceConnected)
            printf("%s[Port %d]\n", strPrefix.c_str(), i);
        else
        {
            RTCString strName = getDeviceDescByDriverName(getDriverKeyName(hHub, i));
            printf("%s[Port %d] %s\n", strPrefix.c_str(), i, strName.c_str());
            if (pInfo->DeviceIsHub)
                enumerateHub(getExternalHubName(hHub, i), strPrefix + "   ");
        }
    }
    RTMemFree(pInfo);
}

void BugReportUsbTreeWin::enumerateHub(RTCString strFullName, RTCString strPrefix)
{
    AutoHandle hHubDev(CreateFileA(RTCString("\\\\.\\").append(strFullName).c_str(),
                                   GENERIC_WRITE, FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, 0, NULL));
    if (!hHubDev.isValid())
        handleWinError(GetLastError(), "CreateFile(%s) failed", strFullName.c_str());
    ULONG cb;
    USB_NODE_INFORMATION hubInfo;
    if (!DeviceIoControl(hHubDev,
                         IOCTL_USB_GET_NODE_INFORMATION,
                         &hubInfo,
                         sizeof(USB_NODE_INFORMATION),
                         &hubInfo,
                         sizeof(USB_NODE_INFORMATION),
                         &cb,
                         NULL))
        handleWinError(GetLastError(), "DeviceIoControl(IOCTL_USB_GET_NODE_INFORMATION) failed");
    enumeratePorts(hHubDev, hubInfo.u.HubInformation.HubDescriptor.bNumberOfPorts, strPrefix);
}

void BugReportUsbTreeWin::enumerateController(PSP_DEVINFO_DATA pInfoData, PSP_DEVICE_INTERFACE_DATA pInterfaceData)
{
    RT_NOREF(pInterfaceData);
    RTCString strCtrlDesc = getDeviceRegistryPropertyString(m_hDevInfo, pInfoData, SPDRP_DEVICEDESC);
    printf("%s\n", strCtrlDesc.c_str());

    ULONG cbNeeded;
    USB_ROOT_HUB_NAME rootHub;
    /* Find out the name length first */
    if (!DeviceIoControl(m_hHostCtrlDev, IOCTL_USB_GET_ROOT_HUB_NAME, NULL, 0,
                         &rootHub, sizeof(rootHub),
                         &cbNeeded, NULL))
        handleWinError(GetLastError(), "DeviceIoControl(IOCTL_USB_GET_ROOT_HUB_NAME) failed");
    cbNeeded = rootHub.ActualLength;
    PUSB_ROOT_HUB_NAME pUnicodeName = (PUSB_ROOT_HUB_NAME)RTMemAlloc(cbNeeded);
    if (!pUnicodeName)
        throw RTCError(RTCStringFmt("Failed to allocate %u bytes", cbNeeded).c_str());

    if (!DeviceIoControl(m_hHostCtrlDev, IOCTL_USB_GET_ROOT_HUB_NAME, NULL, 0,
                         pUnicodeName, cbNeeded,
                         &cbNeeded, NULL))
    {
        DWORD dwErr = GetLastError();
        RTMemFree(pUnicodeName);
        handleWinError(dwErr, "DeviceIoControl(IOCTL_USB_GET_ROOT_HUB_NAME) failed");
    }

    RTCStringFmt strRootHubName("%ls", pUnicodeName->RootHubName);
    RTMemFree(pUnicodeName);
    printf("   Root Hub\n");
    enumerateHub(strRootHubName, "      ");
}

void BugReportUsbTreeWin::enumerate()
{
    m_hDevInfo = SetupDiGetClassDevs((LPGUID)&GUID_DEVINTERFACE_USB_HOST_CONTROLLER, NULL, NULL,
                                     DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (m_hDevInfo == INVALID_HANDLE_VALUE)
        handleWinError(GetLastError(), "SetupDiGetClassDevs(GUID_DEVINTERFACE_USB_HOST_CONTROLLER) failed");

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    for (int i = 0; SetupDiEnumDeviceInfo(m_hDevInfo, i, &deviceInfoData); ++i)
    {
        if (m_hHostCtrlDev != INVALID_HANDLE_VALUE)
            CloseHandle(m_hHostCtrlDev);
        if (m_pDetailData)
            RTMemFree(m_pDetailData);

        SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
        deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
        if (!SetupDiEnumDeviceInterfaces(m_hDevInfo, 0, (LPGUID)&GUID_DEVINTERFACE_USB_HOST_CONTROLLER,
                                         i, &deviceInterfaceData))
            handleWinError(GetLastError(), "SetupDiEnumDeviceInterfaces(GUID_DEVINTERFACE_USB_HOST_CONTROLLER) failed");

        ULONG cbNeeded = 0;
        if (!SetupDiGetDeviceInterfaceDetail(m_hDevInfo, &deviceInterfaceData, NULL, 0, &cbNeeded, NULL)
            && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            handleWinError(GetLastError(), "SetupDiGetDeviceInterfaceDetail failed");

        m_pDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)RTMemAlloc(cbNeeded);
        if (!m_pDetailData)
            throw RTCError(RTCStringFmt("Failed to allocate %u bytes", cbNeeded).c_str());

        m_pDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
        if (!SetupDiGetDeviceInterfaceDetail(m_hDevInfo, &deviceInterfaceData, m_pDetailData, cbNeeded, &cbNeeded, NULL))
            handleWinError(GetLastError(), "SetupDiGetDeviceInterfaceDetail failed");

        m_hHostCtrlDev = CreateFile(m_pDetailData->DevicePath, GENERIC_WRITE, FILE_SHARE_WRITE,
                                    NULL, OPEN_EXISTING, 0, NULL);
        if (m_hHostCtrlDev == INVALID_HANDLE_VALUE)
            handleWinError(GetLastError(), "CreateFile(%ls) failed", m_pDetailData);

        enumerateController(&deviceInfoData, &deviceInterfaceData);
    }
}

class BugReportDriversWin : public BugReportStream
{
public:
    BugReportDriversWin();
    virtual ~BugReportDriversWin();
    virtual RTVFSIOSTREAM getStream(void) { enumerateDrivers(); return BugReportStream::getStream(); }
private:
    void enumerateDrivers(void);

    WCHAR  *m_pwszSystemRoot;
    UINT    m_cSystemRoot;
    LPVOID *m_pDrivers;
    DWORD   m_cDrivers;
    LPVOID  m_pVerInfo;
    DWORD   m_cbVerInfo;
};

BugReportDriversWin::BugReportDriversWin() : BugReportStream("DriverVersions")
{
    m_cSystemRoot = MAX_PATH;
    m_pwszSystemRoot = new WCHAR[MAX_PATH];
    m_cDrivers = 1024;
    m_pDrivers = new LPVOID[m_cDrivers];
    m_pVerInfo = NULL;
    m_cbVerInfo = 0;
}

BugReportDriversWin::~BugReportDriversWin()
{
    if (m_pVerInfo)
        RTMemTmpFree(m_pVerInfo);
    delete[] m_pDrivers;
    delete[] m_pwszSystemRoot;
}

void BugReportDriversWin::enumerateDrivers()
{
    UINT cNeeded = GetWindowsDirectory(m_pwszSystemRoot, m_cSystemRoot);
    if (cNeeded > m_cSystemRoot)
    {
        /* Re-allocate and try again */
        m_cSystemRoot = cNeeded;
        delete[] m_pwszSystemRoot;
        m_pwszSystemRoot = new WCHAR[m_cSystemRoot];
        cNeeded = GetWindowsDirectory(m_pwszSystemRoot, m_cSystemRoot);
    }
    if (cNeeded == 0)
        handleWinError(GetLastError(), "GetWindowsDirectory failed");

    DWORD cbNeeded = 0;
    if (    !EnumDeviceDrivers(m_pDrivers, m_cDrivers * sizeof(m_pDrivers[0]), &cbNeeded)
         || cbNeeded > m_cDrivers * sizeof(m_pDrivers[0]))
    {
        /* Re-allocate and try again */
        m_cDrivers = cbNeeded / sizeof(m_pDrivers[0]);
        delete[] m_pDrivers;
        m_pDrivers = new LPVOID[m_cDrivers];
        if (!EnumDeviceDrivers(m_pDrivers, cbNeeded, &cbNeeded))
            handleWinError(GetLastError(), "EnumDeviceDrivers failed (%p, %u)", m_pDrivers, cbNeeded);
    }

    WCHAR wszDriver[1024];
    for (unsigned i = 0; i < m_cDrivers; i++)
    {
        if (GetDeviceDriverBaseName(m_pDrivers[i], wszDriver, RT_ELEMENTS(wszDriver)))
        {
            if (_wcsnicmp(L"vbox", wszDriver, 4))
                continue;
        }
        else
            continue;
        if (GetDeviceDriverFileName(m_pDrivers[i], wszDriver, RT_ELEMENTS(wszDriver)))
        {
            WCHAR wszTmpDrv[1024];
            WCHAR *pwszDrv = wszDriver;
            if (!wcsncmp(L"\\SystemRoot", wszDriver, 11))
            {
                wcsncpy_s(wszTmpDrv, m_pwszSystemRoot, m_cSystemRoot);
                wcsncat_s(wszTmpDrv, wszDriver + 11, RT_ELEMENTS(wszTmpDrv) - m_cSystemRoot);
                pwszDrv = wszTmpDrv;
            }
            else if (!wcsncmp(L"\\??\\", wszDriver, 4))
                pwszDrv = wszDriver + 4;


            /* Allocate a buffer for version info. Reuse if large enough. */
            DWORD cbNewVerInfo = GetFileVersionInfoSize(pwszDrv, NULL);
            if (cbNewVerInfo > m_cbVerInfo)
            {
                if (m_pVerInfo)
                    RTMemTmpFree(m_pVerInfo);
                m_cbVerInfo = cbNewVerInfo;
                m_pVerInfo = RTMemTmpAlloc(m_cbVerInfo);
                if (!m_pVerInfo)
                    throw RTCError(RTCStringFmt("Failed to allocate %u bytes", m_cbVerInfo).c_str());
            }

            if (GetFileVersionInfo(pwszDrv, NULL, m_cbVerInfo, m_pVerInfo))
            {
                UINT   cbSize = 0;
                LPBYTE lpBuffer = NULL;
                if (VerQueryValue(m_pVerInfo, L"\\", (VOID FAR* FAR*)&lpBuffer, &cbSize))
                {
                    if (cbSize)
                    {
                        VS_FIXEDFILEINFO *pFileInfo = (VS_FIXEDFILEINFO *)lpBuffer;
                        if (pFileInfo->dwSignature == 0xfeef04bd)
                        {
                            printf("%ls (Version: %d.%d.%d.%d)\n", pwszDrv,
                                   (pFileInfo->dwFileVersionMS >> 16) & 0xffff,
                                   (pFileInfo->dwFileVersionMS >> 0) & 0xffff,
                                   (pFileInfo->dwFileVersionLS >> 16) & 0xffff,
                                   (pFileInfo->dwFileVersionLS >> 0) & 0xffff);
                        }
                        else
                            printf("%ls - invalid signature\n", pwszDrv);
                    }
                    else
                        printf("%ls - version info size is 0\n", pwszDrv);
                }
                else
                    printf("%ls - failed to query version info size\n", pwszDrv);
            }
            else
                printf("%ls - failed to get version info with 0x%x\n", pwszDrv, GetLastError());
        }
        else
            printf("%ls - GetDeviceDriverFileName failed with 0x%x\n", wszDriver, GetLastError());
    }
}


class BugReportFilterRegistryWin : public BugReportFilter
{
public:
    BugReportFilterRegistryWin() {};
    virtual ~BugReportFilterRegistryWin() {};
    virtual void *apply(void *pvSource, size_t *pcbInOut);
};

void *BugReportFilterRegistryWin::apply(void *pvSource, size_t *pcbInOut)
{
    /*
     * The following implementation is not optimal by any means. It serves to
     * illustrate and test the case when filter's output is longer than its
     * input.
     */
    RT_NOREF(pcbInOut);
    /* Registry export files are encoded in UTF-16 (little endian on Intel x86). */
    void *pvDest = pvSource;
    uint16_t *pwsSource = (uint16_t *)pvSource;
    if (*pwsSource++ == 0xFEFF && *pcbInOut > 48)
    {
        if (!memcmp(pwsSource, L"Windows Registry Editor", 46))
        {
            *pcbInOut += 2;
            pvDest = allocateBuffer(*pcbInOut);
            uint16_t *pwsDest = (uint16_t *)pvDest;
            *pwsDest++ = 0xFEFF;
            *pwsDest++ = '#';
            /* Leave space for 0xFEFF and '#' */
            memcpy(pwsDest, pwsSource, *pcbInOut - 4);
        }
    }
    return pvDest;
}


void createBugReportOsSpecific(BugReport* report, const char *pszHome)
{
    RT_NOREF(pszHome);
    WCHAR szWinDir[MAX_PATH];

    int cbNeeded = GetWindowsDirectory(szWinDir, RT_ELEMENTS(szWinDir));
    if (cbNeeded == 0)
        throw RTCError(RTCStringFmt("Failed to get Windows directory (err=%d)\n", GetLastError()));
    if (cbNeeded > MAX_PATH)
        throw RTCError(RTCStringFmt("Failed to get Windows directory (needed %d-byte buffer)\n", cbNeeded));
    RTCStringFmt WinInfDir("%ls/inf", szWinDir);
    report->addItem(new BugReportFile(PathJoin(WinInfDir.c_str(), "setupapi.app.log"), "setupapi.app.log"));
    report->addItem(new BugReportFile(PathJoin(WinInfDir.c_str(), "setupapi.dev.log"), "setupapi.dev.log"));
    report->addItem(new BugReportNetworkAdaptersWin);
    RTCStringFmt WinSysDir("%ls/System32", szWinDir);
    report->addItem(new BugReportCommand("IpConfig", PathJoin(WinSysDir.c_str(), "ipconfig.exe"), "/all", NULL));
    report->addItem(new BugReportCommand("RouteTable", PathJoin(WinSysDir.c_str(), "netstat.exe"), "-rn", NULL));
    report->addItem(new BugReportCommand("SystemEvents", PathJoin(WinSysDir.c_str(), "wevtutil.exe"),
                                         "qe", "System",
                                         "/q:*[System[Provider[@Name='VBoxUSBMon' or @Name='VBoxNetLwf']]]", NULL));
    report->addItem(new BugReportCommand("UpdateHistory", PathJoin(WinSysDir.c_str(), "wbem/wmic.exe"),
                                         "qfe", "list", "brief", NULL));
    report->addItem(new BugReportCommand("DriverServices", PathJoin(WinSysDir.c_str(), "sc.exe"),
                                         "query", "type=", "driver", "state=", "all", NULL));
    report->addItem(new BugReportCommand("DriverStore", PathJoin(WinSysDir.c_str(), "pnputil.exe"), "-e", NULL));
    report->addItem(new BugReportCommandTemp("RegDevKeys", PathJoin(WinSysDir.c_str(), "reg.exe"), "export",
                                             "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Enum\\Root\\NET", NULL),
                    new BugReportFilterRegistryWin());
    report->addItem(new BugReportCommandTemp("RegDrvKeys", PathJoin(WinSysDir.c_str(), "reg.exe"), "export",
        "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}", NULL),
                    new BugReportFilterRegistryWin());
    report->addItem(new BugReportCommandTemp("RegNetwork", PathJoin(WinSysDir.c_str(), "reg.exe"), "export",
        "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Network", NULL),
                    new BugReportFilterRegistryWin());
    report->addItem(new BugReportCommandTemp("RegNetFltNobj", PathJoin(WinSysDir.c_str(), "reg.exe"), "export",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Classes\\CLSID\\{f374d1a0-bf08-4bdc-9cb2-c15ddaeef955}", NULL),
                    new BugReportFilterRegistryWin());
    report->addItem(new BugReportUsbTreeWin);
    report->addItem(new BugReportDriversWin);
}
