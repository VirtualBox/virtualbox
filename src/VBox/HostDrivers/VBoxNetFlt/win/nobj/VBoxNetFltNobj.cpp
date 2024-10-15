/* $Id: VBoxNetFltNobj.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetFltNobj.cpp - Notify Object for Bridged Networking Driver.
 *
 * Used to filter Bridged Networking Driver bindings
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
#include "VBoxNetFltNobj.h"
#include <iprt/win/ntddndis.h>
#include <iprt/win/windows.h>
#include <winreg.h>
#include <Olectl.h>

#include <VBoxNetFltNobjT_i.c>

#include <iprt/assert.h>
#include <iprt/utf16.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
//# define VBOXNETFLTNOTIFY_DEBUG_BIND

#ifdef DEBUG
# define NonStandardAssert(a)           Assert(a)
# define NonStandardAssertBreakpoint()  AssertFailed()
#else
# define NonStandardAssert(a) do{}while (0)
# define NonStandardAssertBreakpoint() do{}while (0)
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static HMODULE g_hModSelf = (HMODULE)~(uintptr_t)0;


VBoxNetFltNobj::VBoxNetFltNobj()
    : mpNetCfg(NULL)
    , mpNetCfgComponent(NULL)
    , mbInstalling(FALSE)
{
}

VBoxNetFltNobj::~VBoxNetFltNobj()
{
    cleanup();
}

void VBoxNetFltNobj::cleanup()
{
    if (mpNetCfg)
    {
        mpNetCfg->Release();
        mpNetCfg = NULL;
    }

    if (mpNetCfgComponent)
    {
        mpNetCfgComponent->Release();
        mpNetCfgComponent = NULL;
    }
}

void VBoxNetFltNobj::init(IN INetCfgComponent *pNetCfgComponent, IN INetCfg *pNetCfg, IN BOOL bInstalling)
{
    cleanup();

    NonStandardAssert(pNetCfg);
    NonStandardAssert(pNetCfgComponent);
    if (pNetCfg)
    {
        pNetCfg->AddRef();
        mpNetCfg = pNetCfg;
    }

    if (pNetCfgComponent)
    {
        pNetCfgComponent->AddRef();
        mpNetCfgComponent = pNetCfgComponent;
    }

    mbInstalling = bInstalling;
}

/* INetCfgComponentControl methods */
STDMETHODIMP VBoxNetFltNobj::Initialize(IN INetCfgComponent *pNetCfgComponent, IN INetCfg *pNetCfg, IN BOOL bInstalling)
{
    init(pNetCfgComponent, pNetCfg, bInstalling);
    return S_OK;
}

STDMETHODIMP VBoxNetFltNobj::ApplyRegistryChanges()
{
    return S_OK;
}

STDMETHODIMP VBoxNetFltNobj::ApplyPnpChanges(IN INetCfgPnpReconfigCallback *pCallback)
{
    RT_NOREF1(pCallback);
    return S_OK;
}

STDMETHODIMP VBoxNetFltNobj::CancelChanges()
{
    return S_OK;
}

static HRESULT vboxNetFltWinQueryInstanceKey(IN INetCfgComponent *pComponent, OUT PHKEY phKey)
{
    LPWSTR pwszPnpId;
    HRESULT hrc = pComponent->GetPnpDevNodeId(&pwszPnpId);
    if (hrc == S_OK)
    {
        WCHAR wszKeyName[MAX_PATH];
        RTUtf16Copy(wszKeyName, MAX_PATH, L"SYSTEM\\CurrentControlSet\\Enum\\");
        int rc = RTUtf16Cat(wszKeyName, MAX_PATH, pwszPnpId);
        if (RT_SUCCESS(rc))
        {
            LSTATUS lrc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, wszKeyName, 0 /*ulOptions*/, KEY_READ, phKey);
            if (lrc != ERROR_SUCCESS)
            {
                hrc = HRESULT_FROM_WIN32(lrc);
                NonStandardAssertBreakpoint();
            }
        }
        else
            AssertRCStmt(rc, hrc = ERROR_BUFFER_OVERFLOW);

        CoTaskMemFree(pwszPnpId);
    }
    else
        NonStandardAssertBreakpoint();
    return hrc;
}

static HRESULT vboxNetFltWinQueryDriverKey(IN HKEY InstanceKey, OUT PHKEY phKey)
{
    HRESULT hrc = S_OK;

    WCHAR   wszValue[MAX_PATH];
    DWORD   cbValue = sizeof(wszValue) - sizeof(WCHAR);
    DWORD   dwType  = REG_SZ;
    LSTATUS lrc = RegQueryValueExW(InstanceKey, L"Driver", NULL /*lpReserved*/, &dwType, (LPBYTE)wszValue, &cbValue);
    if (lrc == ERROR_SUCCESS)
    {
        if (dwType == REG_SZ)
        {
            wszValue[RT_ELEMENTS(wszValue) - 1] = '\0'; /* registry strings does not need to be zero terminated. */

            WCHAR wszKeyName[MAX_PATH];
            RTUtf16Copy(wszKeyName, MAX_PATH, L"SYSTEM\\CurrentControlSet\\Control\\Class\\");
            int rc = RTUtf16Cat(wszKeyName, MAX_PATH, wszValue);
            if (RT_SUCCESS(rc))
            {
                lrc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, wszKeyName, 0 /*ulOptions*/, KEY_READ, phKey);
                if (lrc != ERROR_SUCCESS)
                {
                    hrc = HRESULT_FROM_WIN32(lrc);
                    NonStandardAssertBreakpoint();
                }
            }
            else
                AssertRCStmt(rc, hrc = ERROR_BUFFER_OVERFLOW);
        }
        else
        {
            hrc = HRESULT_FROM_WIN32(ERROR_DATATYPE_MISMATCH);
            NonStandardAssertBreakpoint();
        }
    }
    else
    {
        hrc = HRESULT_FROM_WIN32(lrc);
        NonStandardAssertBreakpoint();
    }

    return hrc;
}

static HRESULT vboxNetFltWinQueryDriverKey(IN INetCfgComponent *pComponent, OUT PHKEY phKey)
{
    HKEY    hKeyInstance = NULL;
    HRESULT hrc = vboxNetFltWinQueryInstanceKey(pComponent, &hKeyInstance);
    if (hrc == S_OK)
    {
        hrc = vboxNetFltWinQueryDriverKey(hKeyInstance, phKey);
        if (hrc != S_OK)
            NonStandardAssertBreakpoint();
        RegCloseKey(hKeyInstance);
    }
    else
        NonStandardAssertBreakpoint();
    return hrc;
}

static HRESULT vboxNetFltWinNotifyCheckNetAdp(IN INetCfgComponent *pComponent, OUT bool *pfShouldBind)
{
    *pfShouldBind = false;

    LPWSTR  pwszDevId = NULL;
    HRESULT hrc = pComponent->GetId(&pwszDevId);
    if (hrc == S_OK)
    {
        /** @todo r=bird: This was _wcsnicmp(pwszDevId, L"sun_VBoxNetAdp", sizeof(L"sun_VBoxNetAdp")/2))
         * which includes the terminator, so it translates to a full compare. Goes way back. */
        if (RTUtf16ICmpAscii(pwszDevId, "sun_VBoxNetAdp") == 0)
            *pfShouldBind = false;
        else
            hrc = S_FALSE;
        CoTaskMemFree(pwszDevId);
    }
    else
        NonStandardAssertBreakpoint();

    return hrc;
}

static HRESULT vboxNetFltWinNotifyCheckMsLoop(IN INetCfgComponent *pComponent, OUT bool *pfShouldBind)
{
    *pfShouldBind = false;

    LPWSTR  pwszDevId = NULL;
    HRESULT hrc = pComponent->GetId(&pwszDevId);
    if (hrc == S_OK)
    {
        /** @todo r=bird: This was _wcsnicmp(pwszDevId, L"*msloop", sizeof(L"*msloop")/2)
         * which includes the terminator, making it a full compare. Goes way back. */
        if (RTUtf16ICmpAscii(pwszDevId, "*msloop") == 0)
        {
            /* we need to detect the medium the adapter is presenting
             * to do that we could examine in the registry the *msloop params */
            HKEY hKeyDriver;
            hrc = vboxNetFltWinQueryDriverKey(pComponent, &hKeyDriver);
            if (hrc == S_OK)
            {
                WCHAR wszValue[64]; /* 2 should be enough actually, paranoid check for extra spaces */
                DWORD cbValue = sizeof(wszValue) - sizeof(WCHAR);
                DWORD dwType  = REG_SZ;
                LSTATUS lrc = RegQueryValueExW(hKeyDriver, L"Medium", NULL /*lpReserved*/, &dwType, (LPBYTE)wszValue, &cbValue);
                if (lrc == ERROR_SUCCESS)
                {
                    if (dwType == REG_SZ)
                    {
                        wszValue[RT_ELEMENTS(wszValue) - 1] = '\0';

                        char szUtf8[256];
                        char *pszUtf8 = szUtf8;
                        RTUtf16ToUtf8Ex(wszValue, RTSTR_MAX, &pszUtf8, sizeof(szUtf8), NULL);
                        pszUtf8 = RTStrStrip(pszUtf8);

                        uint64_t uValue = 0;
                        int rc = RTStrToUInt64Ex(pszUtf8, NULL, 0, &uValue);
                        if (RT_SUCCESS(rc))
                        {
                            if (uValue == 0) /* 0 is Ethernet */
                                *pfShouldBind = true;
                            else
                                *pfShouldBind = false;
                        }
                        else
                        {
                            NonStandardAssertBreakpoint();
                            *pfShouldBind = true;
                        }
                    }
                    else
                        NonStandardAssertBreakpoint();
                }
                else
                {
                    /** @todo we should check the default medium in HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Class\{4D36E972-E325-11CE-BFC1-08002bE10318}\<driver_id>\Ndi\Params\Medium, REG_SZ "Default" value */
                    NonStandardAssertBreakpoint();
                    *pfShouldBind = true;
                }

                RegCloseKey(hKeyDriver);
            }
            else
                NonStandardAssertBreakpoint();
        }
        else
            hrc = S_FALSE;
        CoTaskMemFree(pwszDevId);
    }
    else
        NonStandardAssertBreakpoint();
    return hrc;
}

static HRESULT vboxNetFltWinNotifyCheckLowerRange(IN INetCfgComponent *pComponent, OUT bool *pfShouldBind)
{
    *pfShouldBind = false;

    HKEY    hKeyDriver = NULL;
    HRESULT hrc = vboxNetFltWinQueryDriverKey(pComponent, &hKeyDriver);
    if (hrc == S_OK)
    {
        HKEY    hKeyInterfaces = NULL;
        LSTATUS lrc = RegOpenKeyExW(hKeyDriver, L"Ndi\\Interfaces", 0 /*ulOptions*/, KEY_READ, &hKeyInterfaces);
        if (lrc == ERROR_SUCCESS)
        {
            WCHAR wszValue[MAX_PATH];
            DWORD cbValue = sizeof(wszValue) - sizeof(WCHAR);
            DWORD dwType  = REG_SZ;
            lrc = RegQueryValueExW(hKeyInterfaces, L"LowerRange", NULL /*lpReserved*/, &dwType, (LPBYTE)wszValue, &cbValue);
            if (lrc == ERROR_SUCCESS)
            {
                if (dwType == REG_SZ)
                {
                    if (RTUtf16FindAscii(wszValue, "ethernet") >= 0 || RTUtf16FindAscii(wszValue, "wan") >= 0)
                        *pfShouldBind = true;
                    else
                        *pfShouldBind = false;
                }
            }
            else
            {
                /* do not set err status to it */
                *pfShouldBind = false;
                NonStandardAssertBreakpoint();
            }

            RegCloseKey(hKeyInterfaces);
        }
        else
        {
            hrc = HRESULT_FROM_WIN32(lrc);
            NonStandardAssertBreakpoint();
        }

        RegCloseKey(hKeyDriver);
    }
    else
        NonStandardAssertBreakpoint();
    return hrc;
}

static HRESULT vboxNetFltWinNotifyShouldBind(IN INetCfgComponent *pComponent, OUT bool *pfShouldBind)
{
    *pfShouldBind = false;

    /* filter out only physical adapters */
    DWORD fCharacteristics = 0;
    HRESULT hrc = pComponent->GetCharacteristics(&fCharacteristics);
    if (hrc != S_OK)
    {
        NonStandardAssertBreakpoint();
        return hrc;
    }

    /* we are not binding to hidden adapters */
    if (fCharacteristics & NCF_HIDDEN)
        return S_OK;

    hrc = vboxNetFltWinNotifyCheckMsLoop(pComponent, pfShouldBind);
    if (   hrc == S_OK /* this is a loopback adapter, the pfShouldBind already contains the result */
        || hrc != S_FALSE /* error occurred */)
        return hrc;

    hrc = vboxNetFltWinNotifyCheckNetAdp(pComponent, pfShouldBind);
    if (   hrc == S_OK /* this is a VBoxNetAdp adapter, the pfShouldBind already contains the result */
        || hrc != S_FALSE /* error occurred */)
        return hrc;

    //if (!(fCharacteristics & NCF_PHYSICAL))
    //{
    //    *pfShouldBind = false; /* we are binding to physical adapters only */
    //    return S_OK;
    //}

    return vboxNetFltWinNotifyCheckLowerRange(pComponent, pfShouldBind);
}


static HRESULT vboxNetFltWinNotifyShouldBind(IN INetCfgBindingInterface *pIf, OUT bool *pfShouldBind)
{
    INetCfgComponent *pAdapterComponent = NULL;
    HRESULT hrc = pIf->GetLowerComponent(&pAdapterComponent);
    if (hrc == S_OK)
    {
        hrc = vboxNetFltWinNotifyShouldBind(pAdapterComponent, pfShouldBind);

        pAdapterComponent->Release();
    }
    else
    {
        NonStandardAssertBreakpoint();
        *pfShouldBind = false;
    }
    return hrc;
}

static HRESULT vboxNetFltWinNotifyShouldBind(IN INetCfgBindingPath *pPath, OUT bool *pfShouldBind)
{
    *pfShouldBind = false;

    IEnumNetCfgBindingInterface *pIEnumBinding = NULL;
    HRESULT hrc = pPath->EnumBindingInterfaces(&pIEnumBinding);
    if (hrc == S_OK)
    {
        hrc = pIEnumBinding->Reset();
        if (hrc == S_OK)
        {
            for (;;)
            {
                ULONG                    uCount    = 0;
                INetCfgBindingInterface *pIBinding = NULL;
                hrc = pIEnumBinding->Next(1, &pIBinding, &uCount);
                if (hrc == S_OK)
                {
                    hrc = vboxNetFltWinNotifyShouldBind(pIBinding, pfShouldBind);
                    pIBinding->Release();

                    if (hrc != S_OK)
                        break; /* break on failure. */
                    if (!*pfShouldBind)
                        break;
                }
                else if (hrc == S_FALSE)
                {
                    /* no more elements */
                    hrc = S_OK;
                    break;
                }
                else
                {
                    NonStandardAssertBreakpoint();
                    /* break on falure */
                    break;
                }
            }
        }
        else
            NonStandardAssertBreakpoint();

        pIEnumBinding->Release();
    }
    else
        NonStandardAssertBreakpoint();
    return hrc;
}

static bool vboxNetFltWinNotifyShouldBind(IN INetCfgBindingPath *pPath)
{
#ifdef VBOXNETFLTNOTIFY_DEBUG_BIND
    return VBOXNETFLTNOTIFY_DEBUG_BIND;
#else
    bool fShouldBind;
    HRESULT hrc = vboxNetFltWinNotifyShouldBind(pPath, &fShouldBind);
    if (hrc != S_OK)
        fShouldBind = VBOXNETFLTNOTIFY_ONFAIL_BINDDEFAULT;

    return fShouldBind;
#endif
}


/* INetCfgComponentNotifyBinding methods */
STDMETHODIMP VBoxNetFltNobj::NotifyBindingPath(IN DWORD dwChangeFlag, IN INetCfgBindingPath *pNetCfgBP)
{
    if (!(dwChangeFlag & NCN_ENABLE) || (dwChangeFlag & NCN_REMOVE) || vboxNetFltWinNotifyShouldBind(pNetCfgBP))
        return S_OK;
    return NETCFG_S_DISABLE_QUERY;
}

STDMETHODIMP VBoxNetFltNobj::QueryBindingPath(IN DWORD dwChangeFlag, IN INetCfgBindingPath *pNetCfgBP)
{
    RT_NOREF1(dwChangeFlag);
    if (vboxNetFltWinNotifyShouldBind(pNetCfgBP))
        return S_OK;
    return NETCFG_S_DISABLE_QUERY;
}


static ATL::CComModule _Module;

BEGIN_OBJECT_MAP(ObjectMap)
    OBJECT_ENTRY(CLSID_VBoxNetFltNobj, VBoxNetFltNobj)
END_OBJECT_MAP()


extern "C"
BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID /*lpReserved*/)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        g_hModSelf = (HMODULE)hInstance;

        _Module.Init(ObjectMap, hInstance);
        DisableThreadLibraryCalls(hInstance);
    }
    else if (dwReason == DLL_PROCESS_DETACH)
    {
        _Module.Term();
    }
    return TRUE;
}

STDAPI DllCanUnloadNow(void)
{
    return _Module.GetLockCount() == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    return _Module.GetClassObject(rclsid, riid, ppv);
}


/*
 * ATL::CComModule does not suport server registration/unregistration methods,
 * so we need to do it manually. Since this is the only place we do registraton
 * manually, we do it the quick-and-dirty way.
 */

#ifdef RT_EXCEPTIONS_ENABLED
/* Someday we may want to log errors. */
class AdHocRegError
{
public:
    AdHocRegError(LSTATUS rc) { RT_NOREF1(rc); };
};
#endif

/**
 * A simple wrapper on Windows registry functions.
 */
class AdHocRegKey
{
public:
    AdHocRegKey(HKEY hKey) : m_hKey(hKey) {};
    AdHocRegKey(LPCWSTR pcwszName, HKEY hParent = HKEY_CLASSES_ROOT);
    ~AdHocRegKey() { RegCloseKey(m_hKey); };

    AdHocRegKey *create(LPCWSTR pcwszSubkey);
    LSTATUS setValue(LPCWSTR pcwszName, LPCWSTR pcwszValue);
    HKEY getKey(void) { return m_hKey; };
private:
    HKEY m_hKey;
};

AdHocRegKey::AdHocRegKey(LPCWSTR pcwszName, HKEY hParent) : m_hKey(NULL)
{
    LSTATUS rc = RegOpenKeyExW(hParent, pcwszName, 0, KEY_ALL_ACCESS, &m_hKey);
    if (rc != ERROR_SUCCESS)
#ifdef RT_EXCEPTIONS_ENABLED
        throw AdHocRegError(rc);
#else
        m_hKey = NULL;
#endif
}

AdHocRegKey *AdHocRegKey::create(LPCWSTR pcwszSubkey)
{
    HKEY hSubkey;
    LSTATUS rc = RegCreateKeyExW(m_hKey, pcwszSubkey,
                                 0 /*Reserved*/, NULL /*pszClass*/, 0 /*fOptions*/,
                                 KEY_ALL_ACCESS, NULL /*pSecAttr*/, &hSubkey, NULL /*pdwDisposition*/);
    if (rc != ERROR_SUCCESS)
#ifdef RT_EXCEPTIONS_ENABLED
        throw AdHocRegError(rc);
#else
        return NULL;
#endif
    AdHocRegKey *pSubkey = new AdHocRegKey(hSubkey);
    if (!pSubkey)
        RegCloseKey(hSubkey);
    return pSubkey;
}

LSTATUS AdHocRegKey::setValue(LPCWSTR pcwszName, LPCWSTR pcwszValue)
{
    LSTATUS rc = RegSetValueExW(m_hKey, pcwszName, 0, REG_SZ, (const BYTE *)pcwszValue,
                                (DWORD)((RTUtf16Len(pcwszValue) + 1) * sizeof(WCHAR)));
#ifdef RT_EXCEPTIONS_ENABLED
    if (rc != ERROR_SUCCESS)
        throw AdHocRegError(rc);
#endif
    return rc;
}

/**
 * Auxiliary class that facilitates automatic destruction of AdHocRegKey objects
 * allocated in heap. No reference counting here!
 */
class AdHocRegKeyPtr
{
public:
    AdHocRegKeyPtr(AdHocRegKey *pKey) : m_pKey(pKey) {};
    ~AdHocRegKeyPtr()
    {
        if (m_pKey)
        {
            delete m_pKey;
            m_pKey = NULL;
        }
    }

    AdHocRegKey *create(LPCWSTR pcwszSubkey)
    { return m_pKey ? m_pKey->create(pcwszSubkey) : NULL; };

    LSTATUS setValue(LPCWSTR pcwszName, LPCWSTR pcwszValue)
    { return m_pKey ? m_pKey->setValue(pcwszName, pcwszValue) : ERROR_INVALID_STATE; };

private:
    AdHocRegKey *m_pKey;
    /* Prevent copying, since we do not support reference counting */
    AdHocRegKeyPtr(const AdHocRegKeyPtr&);
    AdHocRegKeyPtr& operator=(const AdHocRegKeyPtr&);
};


STDAPI DllRegisterServer(void)
{
    /* Get the path to the DLL we're running inside. */
    WCHAR wszModule[MAX_PATH + 1];
    UINT  cwcModule = GetModuleFileNameW(g_hModSelf, wszModule, MAX_PATH);
    if (cwcModule == 0 || cwcModule > MAX_PATH)
        return SELFREG_E_CLASS;
    wszModule[MAX_PATH] = '\0';

    /*
     * Create registry keys and values.  When exceptions are disabled, we depend
     * on setValue() to propagate fail key creation failures.
     */
#ifdef RT_EXCEPTIONS_ENABLED
    try
#endif
    {
        AdHocRegKey keyCLSID(L"CLSID");
        AdHocRegKeyPtr pkeyNobjClass(keyCLSID.create(L"{f374d1a0-bf08-4bdc-9cb2-c15ddaeef955}"));
        LSTATUS lrc = pkeyNobjClass.setValue(NULL, L"VirtualBox Bridged Networking Driver Notify Object v1.1");
        if (lrc != ERROR_SUCCESS)
            return SELFREG_E_CLASS;

        AdHocRegKeyPtr pkeyNobjSrv(pkeyNobjClass.create(L"InProcServer32"));
        lrc = pkeyNobjSrv.setValue(NULL, wszModule);
        if (lrc != ERROR_SUCCESS)
            return SELFREG_E_CLASS;
        lrc = pkeyNobjSrv.setValue(L"ThreadingModel", L"Both");
        if (lrc != ERROR_SUCCESS)
            return SELFREG_E_CLASS;
    }
#ifdef RT_EXCEPTIONS_ENABLED
    catch (AdHocRegError) { return SELFREG_E_CLASS; }
#endif

#ifdef RT_EXCEPTIONS_ENABLED
    try
#endif
    {
        AdHocRegKey keyTypeLib(L"TypeLib");
        AdHocRegKeyPtr pkeyNobjLib(keyTypeLib.create(L"{2A0C94D1-40E1-439C-8FE8-24107CAB0840}\\1.1"));
        LSTATUS lrc = pkeyNobjLib.setValue(NULL, L"VirtualBox Bridged Networking Driver Notify Object v1.1 Type Library");
        if (lrc != ERROR_SUCCESS)
            return SELFREG_E_TYPELIB;

        AdHocRegKeyPtr pkeyNobjLib0(pkeyNobjLib.create(L"0\\win64"));
        lrc = pkeyNobjLib0.setValue(NULL, wszModule);
        if (lrc != ERROR_SUCCESS)
            return SELFREG_E_TYPELIB;
        AdHocRegKeyPtr pkeyNobjLibFlags(pkeyNobjLib.create(L"FLAGS"));
        lrc = pkeyNobjLibFlags.setValue(NULL, L"0");
        if (lrc != ERROR_SUCCESS)
            return SELFREG_E_TYPELIB;

        if (GetSystemDirectoryW(wszModule, MAX_PATH) == 0)
            return SELFREG_E_TYPELIB;
        AdHocRegKeyPtr pkeyNobjLibHelpDir(pkeyNobjLib.create(L"HELPDIR"));
        lrc = pkeyNobjLibHelpDir.setValue(NULL, wszModule);
        if (lrc != ERROR_SUCCESS)
            return SELFREG_E_TYPELIB;
    }
#ifdef RT_EXCEPTIONS_ENABLED
    catch (AdHocRegError) { return SELFREG_E_TYPELIB; }
#endif

    return S_OK;
}


STDAPI DllUnregisterServer(void)
{
    static struct { HKEY hKeyRoot; wchar_t const *pwszParentKey; wchar_t const *pwszKeyToDelete; HRESULT hrcFail; }
    s_aKeys[] =
    {
        { HKEY_CLASSES_ROOT, L"TypeLib", L"{2A0C94D1-40E1-439C-8FE8-24107CAB0840}", SELFREG_E_TYPELIB },
        { HKEY_CLASSES_ROOT, L"CLSID",   L"{f374d1a0-bf08-4bdc-9cb2-c15ddaeef955}", SELFREG_E_CLASS },
    };

    HRESULT hrc = S_OK;
    for (size_t i = 0; i < RT_ELEMENTS(s_aKeys); i++)
    {
        HKEY hKey = NULL;
        LSTATUS lrc = RegOpenKeyExW(s_aKeys[i].hKeyRoot, s_aKeys[i].pwszParentKey, 0, KEY_ALL_ACCESS, &hKey);
        if (lrc == ERROR_SUCCESS)
        {
            lrc = RegDeleteTreeW(hKey, s_aKeys[i].pwszKeyToDelete); /* Vista and later */
            RegCloseKey(hKey);
        }

        if (lrc != ERROR_SUCCESS && lrc != ERROR_FILE_NOT_FOUND && hrc == S_OK)
            hrc = s_aKeys[i].hrcFail;
    }

    return S_OK;
}

