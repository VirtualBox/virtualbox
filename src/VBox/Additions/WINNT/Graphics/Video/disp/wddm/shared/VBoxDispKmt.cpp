/* $Id: VBoxDispKmt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVideo Display D3D User Mode Dll.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "../VBoxDispD3DBase.h"
#include <VBoxDispKmt.h>

#include <iprt/assert.h>
#include <iprt/log.h>


/**
 * Loads a system DLL.
 *
 * @returns Module handle or NULL
 * @param   pszName             The DLL name.
 */
static HMODULE loadSystemDll(const char *pszName)
{
    char   szPath[MAX_PATH];
    UINT   cchPath = GetSystemDirectoryA(szPath, sizeof(szPath));
    size_t cbName  = strlen(pszName) + 1;
    if (cchPath + 1 + cbName > sizeof(szPath))
        return NULL;
    szPath[cchPath] = '\\';
    memcpy(&szPath[cchPath + 1], pszName, cbName);
    return LoadLibraryA(szPath);
}

HRESULT vboxDispKmtCallbacksInit(PVBOXDISPKMT_CALLBACKS pCallbacks)
{
    HRESULT hr = S_OK;

    memset(pCallbacks, 0, sizeof (*pCallbacks));

    pCallbacks->hGdi32 = loadSystemDll("gdi32.dll");
    if (pCallbacks->hGdi32 != NULL)
    {
        bool bSupported = true;
        bool bSupportedWin8 = true;
        pCallbacks->pfnD3DKMTOpenAdapterFromHdc = (PFND3DKMT_OPENADAPTERFROMHDC)GetProcAddress(pCallbacks->hGdi32, "D3DKMTOpenAdapterFromHdc");
        LogFunc(("pfnD3DKMTOpenAdapterFromHdc = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTOpenAdapterFromHdc)));
        bSupported &= !!(pCallbacks->pfnD3DKMTOpenAdapterFromHdc);

        pCallbacks->pfnD3DKMTOpenAdapterFromGdiDisplayName = (PFND3DKMT_OPENADAPTERFROMGDIDISPLAYNAME)GetProcAddress(pCallbacks->hGdi32, "D3DKMTOpenAdapterFromGdiDisplayName");
        LogFunc(("pfnD3DKMTOpenAdapterFromGdiDisplayName = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTOpenAdapterFromGdiDisplayName)));
        bSupported &= !!(pCallbacks->pfnD3DKMTOpenAdapterFromGdiDisplayName);

        pCallbacks->pfnD3DKMTCloseAdapter = (PFND3DKMT_CLOSEADAPTER)GetProcAddress(pCallbacks->hGdi32, "D3DKMTCloseAdapter");
        LogFunc(("pfnD3DKMTCloseAdapter = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTCloseAdapter)));
        bSupported &= !!(pCallbacks->pfnD3DKMTCloseAdapter);

        pCallbacks->pfnD3DKMTEscape = (PFND3DKMT_ESCAPE)GetProcAddress(pCallbacks->hGdi32, "D3DKMTEscape");
        LogFunc(("pfnD3DKMTEscape = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTEscape)));
        bSupported &= !!(pCallbacks->pfnD3DKMTEscape);

        pCallbacks->pfnD3DKMTQueryAdapterInfo = (PFND3DKMT_QUERYADAPTERINFO)GetProcAddress(pCallbacks->hGdi32, "D3DKMTQueryAdapterInfo");
        LogFunc(("pfnD3DKMTQueryAdapterInfo = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTQueryAdapterInfo)));
        bSupported &= !!(pCallbacks->pfnD3DKMTQueryAdapterInfo);

        pCallbacks->pfnD3DKMTCreateDevice = (PFND3DKMT_CREATEDEVICE)GetProcAddress(pCallbacks->hGdi32, "D3DKMTCreateDevice");
        LogFunc(("pfnD3DKMTCreateDevice = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTCreateDevice)));
        bSupported &= !!(pCallbacks->pfnD3DKMTCreateDevice);

        pCallbacks->pfnD3DKMTDestroyDevice = (PFND3DKMT_DESTROYDEVICE)GetProcAddress(pCallbacks->hGdi32, "D3DKMTDestroyDevice");
        LogFunc(("pfnD3DKMTDestroyDevice = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTDestroyDevice)));
        bSupported &= !!(pCallbacks->pfnD3DKMTDestroyDevice);

        pCallbacks->pfnD3DKMTCreateContext = (PFND3DKMT_CREATECONTEXT)GetProcAddress(pCallbacks->hGdi32, "D3DKMTCreateContext");
        LogFunc(("pfnD3DKMTCreateContext = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTCreateContext)));
        bSupported &= !!(pCallbacks->pfnD3DKMTCreateContext);

        pCallbacks->pfnD3DKMTDestroyContext = (PFND3DKMT_DESTROYCONTEXT)GetProcAddress(pCallbacks->hGdi32, "D3DKMTDestroyContext");
        LogFunc(("pfnD3DKMTDestroyContext = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTDestroyContext)));
        bSupported &= !!(pCallbacks->pfnD3DKMTDestroyContext);

        pCallbacks->pfnD3DKMTRender = (PFND3DKMT_RENDER)GetProcAddress(pCallbacks->hGdi32, "D3DKMTRender");
        LogFunc(("pfnD3DKMTRender = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTRender)));
        bSupported &= !!(pCallbacks->pfnD3DKMTRender);

        pCallbacks->pfnD3DKMTCreateAllocation = (PFND3DKMT_CREATEALLOCATION)GetProcAddress(pCallbacks->hGdi32, "D3DKMTCreateAllocation");
        LogFunc(("pfnD3DKMTCreateAllocation = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTCreateAllocation)));
        bSupported &= !!(pCallbacks->pfnD3DKMTCreateAllocation);

        pCallbacks->pfnD3DKMTDestroyAllocation = (PFND3DKMT_DESTROYALLOCATION)GetProcAddress(pCallbacks->hGdi32, "D3DKMTDestroyAllocation");
        LogFunc(("pfnD3DKMTDestroyAllocation = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTDestroyAllocation)));
        bSupported &= !!(pCallbacks->pfnD3DKMTDestroyAllocation);

        pCallbacks->pfnD3DKMTLock = (PFND3DKMT_LOCK)GetProcAddress(pCallbacks->hGdi32, "D3DKMTLock");
        LogFunc(("pfnD3DKMTLock = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTLock)));
        bSupported &= !!(pCallbacks->pfnD3DKMTLock);

        pCallbacks->pfnD3DKMTUnlock = (PFND3DKMT_UNLOCK)GetProcAddress(pCallbacks->hGdi32, "D3DKMTUnlock");
        LogFunc(("pfnD3DKMTUnlock = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTUnlock)));
        bSupported &= !!(pCallbacks->pfnD3DKMTUnlock);

        pCallbacks->pfnD3DKMTInvalidateActiveVidPn = (PFND3DKMT_INVALIDATEACTIVEVIDPN)GetProcAddress(pCallbacks->hGdi32, "D3DKMTInvalidateActiveVidPn");
        LogFunc(("pfnD3DKMTInvalidateActiveVidPn = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTInvalidateActiveVidPn)));
        bSupported &= !!(pCallbacks->pfnD3DKMTInvalidateActiveVidPn);

        pCallbacks->pfnD3DKMTPollDisplayChildren = (PFND3DKMT_POLLDISPLAYCHILDREN)GetProcAddress(pCallbacks->hGdi32, "D3DKMTPollDisplayChildren");
        LogFunc(("pfnD3DKMTPollDisplayChildren = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTPollDisplayChildren)));
        bSupported &= !!(pCallbacks->pfnD3DKMTPollDisplayChildren);

        pCallbacks->pfnD3DKMTEnumAdapters = (PFND3DKMT_ENUMADAPTERS)GetProcAddress(pCallbacks->hGdi32, "D3DKMTEnumAdapters");
        LogFunc(("pfnD3DKMTEnumAdapters = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTEnumAdapters)));
        /* this present starting win8 release preview only, so keep going if it is not available,
         * i.e. do not clear the bSupported on its absence */
        bSupportedWin8 &= !!(pCallbacks->pfnD3DKMTEnumAdapters);

        pCallbacks->pfnD3DKMTOpenAdapterFromLuid = (PFND3DKMT_OPENADAPTERFROMLUID)GetProcAddress(pCallbacks->hGdi32, "D3DKMTOpenAdapterFromLuid");
        LogFunc(("pfnD3DKMTOpenAdapterFromLuid = %p\n", RT_CB_LOG_CAST(pCallbacks->pfnD3DKMTOpenAdapterFromLuid)));
        /* this present starting win8 release preview only, so keep going if it is not available,
         * i.e. do not clear the bSupported on its absence */
        bSupportedWin8 &= !!(pCallbacks->pfnD3DKMTOpenAdapterFromLuid);

        /*Assert(bSupported);*/
        if (bSupported)
        {
            if (bSupportedWin8)
                pCallbacks->enmVersion = VBOXDISPKMT_CALLBACKS_VERSION_WIN8;
            else
                pCallbacks->enmVersion = VBOXDISPKMT_CALLBACKS_VERSION_VISTA_WIN7;
            return S_OK;
        }
        else
        {
            LogFunc(("one of pfnD3DKMT function pointers failed to initialize\n"));
            hr = E_NOINTERFACE;
        }

        FreeLibrary(pCallbacks->hGdi32);
    }
    else
    {
        DWORD winEr = GetLastError();
        hr = HRESULT_FROM_WIN32(winEr);
        Assert(0);
        Assert(hr != S_OK);
        Assert(hr != S_FALSE);
        if (hr == S_OK || hr == S_FALSE)
            hr = E_FAIL;
    }

    return hr;
}

HRESULT vboxDispKmtCallbacksTerm(PVBOXDISPKMT_CALLBACKS pCallbacks)
{
    FreeLibrary(pCallbacks->hGdi32);
#ifdef DEBUG_misha
    memset(pCallbacks, 0, sizeof (*pCallbacks));
#endif
    return S_OK;
}

HRESULT vboxDispKmtAdpHdcCreate(HDC *phDc)
{
    HRESULT hr = E_FAIL;
    DISPLAY_DEVICE DDev;
    memset(&DDev, 0, sizeof (DDev));
    DDev.cb = sizeof (DDev);

    *phDc = NULL;

    for (int i = 0; ; ++i)
    {
        if (EnumDisplayDevices(NULL, /* LPCTSTR lpDevice */ i, /* DWORD iDevNum */
                &DDev, 0 /* DWORD dwFlags*/))
        {
            if (DDev.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
            {
                HDC hDc = CreateDC(NULL, DDev.DeviceName, NULL, NULL);
                if (hDc)
                {
                    *phDc = hDc;
                    return S_OK;
                }
                else
                {
                    DWORD winEr = GetLastError();
                    Assert(0);
                    hr = HRESULT_FROM_WIN32(winEr);
                    Assert(FAILED(hr));
                    break;
                }
            }
        }
        else
        {
            DWORD winEr = GetLastError();
//            BP_WARN();
            hr = HRESULT_FROM_WIN32(winEr);
#ifdef DEBUG_misha
            Assert(FAILED(hr));
#endif
            if (!FAILED(hr))
            {
                hr = E_FAIL;
            }
            break;
        }
    }

    return hr;
}

static HRESULT vboxDispKmtOpenAdapterViaHdc(const VBOXDISPKMT_CALLBACKS *pCallbacks, PVBOXDISPKMT_ADAPTER pAdapter)
{
    D3DKMT_OPENADAPTERFROMHDC OpenAdapterData = {0};
    HRESULT hr = vboxDispKmtAdpHdcCreate(&OpenAdapterData.hDc);
    if (!SUCCEEDED(hr))
        return hr;

    Assert(OpenAdapterData.hDc);
    NTSTATUS Status = pCallbacks->pfnD3DKMTOpenAdapterFromHdc(&OpenAdapterData);
    if (NT_SUCCESS(Status))
    {
        pAdapter->hAdapter = OpenAdapterData.hAdapter;
        pAdapter->hDc = OpenAdapterData.hDc;
        pAdapter->pCallbacks = pCallbacks;
        memset(&pAdapter->Luid, 0, sizeof (pAdapter->Luid));
        return S_OK;
    }
    else
    {
        LogFunc(("pfnD3DKMTOpenAdapterFromGdiDisplayName failed, Status (0x%x)\n", Status));
        hr = E_FAIL;
    }

    DeleteDC(OpenAdapterData.hDc);

    return hr;
}

static HRESULT vboxDispKmtOpenAdapterViaLuid(const VBOXDISPKMT_CALLBACKS *pCallbacks, PVBOXDISPKMT_ADAPTER pAdapter)
{
    if (pCallbacks->enmVersion < VBOXDISPKMT_CALLBACKS_VERSION_WIN8)
        return E_NOTIMPL;

    D3DKMT_ENUMADAPTERS EnumAdapters = {0};
    EnumAdapters.NumAdapters = RT_ELEMENTS(EnumAdapters.Adapters);

    NTSTATUS Status = pCallbacks->pfnD3DKMTEnumAdapters(&EnumAdapters);
#ifdef DEBUG_misha
    Assert(!Status);
#endif
    if (!NT_SUCCESS(Status))
        return E_FAIL;

    Assert(EnumAdapters.NumAdapters);

    /* try the same twice: if we fail to open the adapter containing present sources,
     * try to open any adapter */
    for (ULONG f = 0; f < 2; ++f)
    {
        for (ULONG i = 0; i < EnumAdapters.NumAdapters; ++i)
        {
            if (f || EnumAdapters.Adapters[i].NumOfSources)
            {
                D3DKMT_OPENADAPTERFROMLUID OpenAdapterData = {{0}};
                OpenAdapterData.AdapterLuid = EnumAdapters.Adapters[i].AdapterLuid;
                Status = pCallbacks->pfnD3DKMTOpenAdapterFromLuid(&OpenAdapterData);
#ifdef DEBUG_misha
                Assert(!Status);
#endif
                if (NT_SUCCESS(Status))
                {
                    pAdapter->hAdapter = OpenAdapterData.hAdapter;
                    pAdapter->hDc = NULL;
                    pAdapter->Luid = EnumAdapters.Adapters[i].AdapterLuid;
                    pAdapter->pCallbacks = pCallbacks;
                    return S_OK;
                }
            }
        }
    }

#ifdef DEBUG_misha
    Assert(0);
#endif
    return E_FAIL;
}

HRESULT vboxDispKmtOpenAdapter(const VBOXDISPKMT_CALLBACKS *pCallbacks, PVBOXDISPKMT_ADAPTER pAdapter)
{
    HRESULT hr = vboxDispKmtOpenAdapterViaHdc(pCallbacks, pAdapter);
    if (SUCCEEDED(hr))
        return S_OK;

    hr = vboxDispKmtOpenAdapterViaLuid(pCallbacks, pAdapter);
    if (SUCCEEDED(hr))
        return S_OK;

    return hr;
}

HRESULT vboxDispKmtCloseAdapter(PVBOXDISPKMT_ADAPTER pAdapter)
{
    D3DKMT_CLOSEADAPTER ClosaAdapterData = {0};
    ClosaAdapterData.hAdapter = pAdapter->hAdapter;
    NTSTATUS Status = pAdapter->pCallbacks->pfnD3DKMTCloseAdapter(&ClosaAdapterData);
    Assert(!Status);
    if (!Status)
    {
        DeleteDC(pAdapter->hDc);
#ifdef DEBUG_misha
        memset(pAdapter, 0, sizeof (*pAdapter));
#endif
        return S_OK;
    }

    LogFunc(("pfnD3DKMTCloseAdapter failed, Status (0x%x)\n", Status));

    return E_FAIL;
}

HRESULT vboxDispKmtCreateDevice(PVBOXDISPKMT_ADAPTER pAdapter, PVBOXDISPKMT_DEVICE pDevice)
{
    D3DKMT_CREATEDEVICE CreateDeviceData = {0};
    CreateDeviceData.hAdapter = pAdapter->hAdapter;
    NTSTATUS Status = pAdapter->pCallbacks->pfnD3DKMTCreateDevice(&CreateDeviceData);
    Assert(!Status);
    if (!Status)
    {
        pDevice->pAdapter = pAdapter;
        pDevice->hDevice = CreateDeviceData.hDevice;
        pDevice->pCommandBuffer = CreateDeviceData.pCommandBuffer;
        pDevice->CommandBufferSize = CreateDeviceData.CommandBufferSize;
        pDevice->pAllocationList = CreateDeviceData.pAllocationList;
        pDevice->AllocationListSize = CreateDeviceData.AllocationListSize;
        pDevice->pPatchLocationList = CreateDeviceData.pPatchLocationList;
        pDevice->PatchLocationListSize = CreateDeviceData.PatchLocationListSize;

        return S_OK;
    }

    return E_FAIL;
}

HRESULT vboxDispKmtDestroyDevice(PVBOXDISPKMT_DEVICE pDevice)
{
    D3DKMT_DESTROYDEVICE DestroyDeviceData = {0};
    DestroyDeviceData.hDevice = pDevice->hDevice;
    NTSTATUS Status = pDevice->pAdapter->pCallbacks->pfnD3DKMTDestroyDevice(&DestroyDeviceData);
    Assert(!Status);
    if (!Status)
    {
#ifdef DEBUG_misha
        memset(pDevice, 0, sizeof (*pDevice));
#endif
        return S_OK;
    }
    return E_FAIL;
}

/// @todo Used for resize and seamless. Drop crVersion* params.
HRESULT vboxDispKmtCreateContext(PVBOXDISPKMT_DEVICE pDevice, PVBOXDISPKMT_CONTEXT pContext,
                                    VBOXWDDM_CONTEXT_TYPE enmType,
                                    HANDLE hEvent, uint64_t u64UmInfo)
{
    VBOXWDDM_CREATECONTEXT_INFO Info = {0};
    Info.u32IfVersion = 9;
    Info.enmType = enmType;
    Info.u.vbox.crVersionMajor = 0; /* Not used */
    Info.u.vbox.crVersionMinor = 0; /* Not used */
    Info.u.vbox.hUmEvent = (uintptr_t)hEvent;
    Info.u.vbox.u64UmInfo = u64UmInfo;
    D3DKMT_CREATECONTEXT ContextData = {0};
    ContextData.hDevice = pDevice->hDevice;
    ContextData.NodeOrdinal = VBOXWDDM_NODE_ID_3D_KMT;
    ContextData.EngineAffinity = VBOXWDDM_ENGINE_ID_3D_KMT;
    ContextData.pPrivateDriverData = &Info;
    ContextData.PrivateDriverDataSize = sizeof (Info);
    ContextData.ClientHint = D3DKMT_CLIENTHINT_DX9;
    NTSTATUS Status = pDevice->pAdapter->pCallbacks->pfnD3DKMTCreateContext(&ContextData);
    Assert(!Status);
    if (!Status)
    {
        pContext->pDevice = pDevice;
        pContext->hContext = ContextData.hContext;
        pContext->pCommandBuffer = ContextData.pCommandBuffer;
        pContext->CommandBufferSize = ContextData.CommandBufferSize;
        pContext->pAllocationList = ContextData.pAllocationList;
        pContext->AllocationListSize = ContextData.AllocationListSize;
        pContext->pPatchLocationList = ContextData.pPatchLocationList;
        pContext->PatchLocationListSize = ContextData.PatchLocationListSize;
        return S_OK;
    }
    return E_FAIL;
}

HRESULT vboxDispKmtDestroyContext(PVBOXDISPKMT_CONTEXT pContext)
{
    D3DKMT_DESTROYCONTEXT DestroyContextData = {0};
    DestroyContextData.hContext = pContext->hContext;
    NTSTATUS Status = pContext->pDevice->pAdapter->pCallbacks->pfnD3DKMTDestroyContext(&DestroyContextData);
    Assert(!Status);
    if (!Status)
    {
#ifdef DEBUG_misha
        memset(pContext, 0, sizeof (*pContext));
#endif
        return S_OK;
    }
    return E_FAIL;
}
