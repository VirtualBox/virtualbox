/* $Id: VBoxDispD3D.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVideo Display D3D User mode dll
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

#define INITGUID

#include <iprt/initterm.h>
#include <iprt/log.h>
#include <iprt/mem.h>

#include <VBox/Log.h>

#include <VBox/VBoxGuestLib.h>

#include "VBoxDispD3D.h"
#include "VBoxDispDbg.h"

#include <Psapi.h>

#define VBOXDISP_IS_MODULE_FUNC(_pvModule, _cbModule, _pfn) ( \
           (((uintptr_t)(_pfn)) >= ((uintptr_t)(_pvModule))) \
        && (((uintptr_t)(_pfn)) < (((uintptr_t)(_pvModule)) + ((DWORD)(_cbModule)))) \
        )

static BOOL vboxDispIsDDraw(D3DDDIARG_OPENADAPTER const *pOpenData)
{
    /*if we are loaded by ddraw module, the Interface version should be 7
     * and pAdapterCallbacks should be ddraw-supplied, i.e. reside in ddraw module */
    if (pOpenData->Interface != 7)
        return FALSE;

    HMODULE hDDraw = GetModuleHandleA("ddraw.dll");
    if (!hDDraw)
        return FALSE;

    HANDLE hProcess = GetCurrentProcess();
    MODULEINFO ModuleInfo = {0};

    if (!GetModuleInformation(hProcess, hDDraw, &ModuleInfo, sizeof (ModuleInfo)))
    {
        DWORD winEr = GetLastError(); NOREF(winEr);
        WARN(("GetModuleInformation failed, %d", winEr));
        return FALSE;
    }

    if (VBOXDISP_IS_MODULE_FUNC(ModuleInfo.lpBaseOfDll, ModuleInfo.SizeOfImage,
                                pOpenData->pAdapterCallbacks->pfnQueryAdapterInfoCb))
        return TRUE;
    if (VBOXDISP_IS_MODULE_FUNC(ModuleInfo.lpBaseOfDll, ModuleInfo.SizeOfImage,
                                pOpenData->pAdapterCallbacks->pfnGetMultisampleMethodListCb))
        return TRUE;

    return FALSE;
}

static HRESULT vboxDispQueryAdapterInfo(D3DDDIARG_OPENADAPTER const *pOpenData, VBOXWDDM_QAI **ppAdapterInfo)
{
    VBOXWDDM_QAI *pAdapterInfo = (VBOXWDDM_QAI *)RTMemAllocZ(sizeof(VBOXWDDM_QAI));
    AssertReturn(pAdapterInfo, E_OUTOFMEMORY);

    D3DDDICB_QUERYADAPTERINFO DdiQuery;
    DdiQuery.PrivateDriverDataSize = sizeof(VBOXWDDM_QAI);
    DdiQuery.pPrivateDriverData = pAdapterInfo;
    HRESULT hr = pOpenData->pAdapterCallbacks->pfnQueryAdapterInfoCb(pOpenData->hAdapter, &DdiQuery);
    AssertReturnStmt(SUCCEEDED(hr), RTMemFree(pAdapterInfo), hr);

    /* Check that the miniport version match display version. */
    if (pAdapterInfo->u32Version == VBOXVIDEOIF_VERSION)
    {
        *ppAdapterInfo = pAdapterInfo;
    }
    else
    {
        LOGREL_EXACT((__FUNCTION__": miniport version mismatch, expected (%d), but was (%d)\n",
                      VBOXVIDEOIF_VERSION, pAdapterInfo->u32Version));
        hr = E_FAIL;
    }

    return hr;
}

static HRESULT vboxDispAdapterInit(D3DDDIARG_OPENADAPTER const *pOpenData, VBOXWDDM_QAI *pAdapterInfo,
                                   PVBOXWDDMDISP_ADAPTER *ppAdapter)
{
#ifdef VBOX_WITH_VIDEOHWACCEL
    Assert(pAdapterInfo->cInfos >= 1);
    PVBOXWDDMDISP_ADAPTER pAdapter = (PVBOXWDDMDISP_ADAPTER)RTMemAllocZ(RT_UOFFSETOF_DYN(VBOXWDDMDISP_ADAPTER,
                                                                                         aHeads[pAdapterInfo->cInfos]));
#else
    Assert(pAdapterInfo->cInfos == 0);
    PVBOXWDDMDISP_ADAPTER pAdapter = (PVBOXWDDMDISP_ADAPTER)RTMemAllocZ(sizeof(VBOXWDDMDISP_ADAPTER));
#endif
    AssertReturn(pAdapter, E_OUTOFMEMORY);

    pAdapter->hAdapter    = pOpenData->hAdapter;
    pAdapter->uIfVersion  = pOpenData->Interface;
    pAdapter->uRtVersion  = pOpenData->Version;
    pAdapter->RtCallbacks = *pOpenData->pAdapterCallbacks;
    pAdapter->enmHwType   = pAdapterInfo->enmHwType;
    if (pAdapter->enmHwType == VBOXVIDEO_HWTYPE_VBOX)
        pAdapter->u32VBox3DCaps = pAdapterInfo->u.vbox.u32VBox3DCaps;
    pAdapter->AdapterInfo = *pAdapterInfo;
    pAdapter->f3D         =    RT_BOOL(pAdapterInfo->u32AdapterCaps & VBOXWDDM_QAI_CAP_3D)
                            && !vboxDispIsDDraw(pOpenData);
#ifdef VBOX_WITH_VIDEOHWACCEL
    pAdapter->cHeads      = pAdapterInfo->cInfos;
    for (uint32_t i = 0; i < pAdapter->cHeads; ++i)
        pAdapter->aHeads[i].Vhwa.Settings = pAdapterInfo->aInfos[i];
#endif

    *ppAdapter = pAdapter;
    return S_OK;
}

HRESULT APIENTRY OpenAdapter(__inout D3DDDIARG_OPENADAPTER *pOpenData)
{
    LOG_EXACT(("==> "__FUNCTION__"\n"));

    LOGREL(("Built %s %s", __DATE__, __TIME__));

    VBOXWDDM_QAI *pAdapterInfo = NULL;
    PVBOXWDDMDISP_ADAPTER pAdapter = NULL;

    /* Query the miniport about virtual hardware capabilities. */
    HRESULT hr = vboxDispQueryAdapterInfo(pOpenData, &pAdapterInfo);
    if (SUCCEEDED(hr))
    {
        hr = vboxDispAdapterInit(pOpenData, pAdapterInfo, &pAdapter);
        if (SUCCEEDED(hr))
        {
            if (pAdapter->f3D)
            {
                /* 3D adapter. Try enable the 3D. */
                hr = VBoxDispD3DGlobalOpen(&pAdapter->D3D, &pAdapter->Formats, &pAdapter->AdapterInfo);
                if (hr == S_OK)
                {
                    LOG(("SUCCESS 3D Enabled, pAdapter (0x%p)", pAdapter));
                }
                else
                    WARN(("VBoxDispD3DOpen failed, hr (%d)", hr));
            }
#ifdef VBOX_WITH_VIDEOHWACCEL
            else
            {
                /* 2D adapter. */
                hr = VBoxDispD3DGlobal2DFormatsInit(pAdapter);
                if (FAILED(hr))
                    WARN(("VBoxDispD3DGlobal2DFormatsInit failed hr 0x%x", hr));
            }
#endif
        }
    }

    if (SUCCEEDED(hr))
    {
        /* Return data to the OS. */
        if (pAdapter->enmHwType == VBOXVIDEO_HWTYPE_VBOX)
        {
            /* Not supposed to work with this. */
            hr = E_FAIL;
        }
#ifdef VBOX_WITH_MESA3D
        else if (pAdapter->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
        {
            pOpenData->hAdapter                       = pAdapter;
            pOpenData->pAdapterFuncs->pfnGetCaps      = GaDdiAdapterGetCaps;
            pOpenData->pAdapterFuncs->pfnCreateDevice = GaDdiAdapterCreateDevice;
            pOpenData->pAdapterFuncs->pfnCloseAdapter = GaDdiAdapterCloseAdapter;
            pOpenData->DriverVersion                  = RT_BOOL(pAdapterInfo->u32AdapterCaps & VBOXWDDM_QAI_CAP_WIN7)
                                                      ? D3D_UMD_INTERFACE_VERSION_WIN7
                                                      : D3D_UMD_INTERFACE_VERSION_VISTA;
        }
#endif
        else
            hr = E_FAIL;
    }

    if (FAILED(hr))
    {
        WARN(("OpenAdapter failed hr 0x%x", hr));
        RTMemFree(pAdapter);
    }

    RTMemFree(pAdapterInfo);

    LOG_EXACT(("<== "__FUNCTION__", hr (%x)\n", hr));
    return hr;
}


/**
 * DLL entry point.
 */
BOOL WINAPI DllMain(HINSTANCE hInstance,
                    DWORD     dwReason,
                    LPVOID    lpReserved)
{
    RT_NOREF(hInstance, lpReserved);

    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            vboxVDbgPrint(("VBoxDispD3D: DLL loaded.\n"));
#ifdef VBOXWDDMDISP_DEBUG_VEHANDLER
            vboxVDbgVEHandlerRegister();
#endif
            int rc = RTR3InitDll(RTR3INIT_FLAGS_UNOBTRUSIVE);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                VBoxDispD3DGlobalInit();
                vboxVDbgPrint(("VBoxDispD3D: DLL loaded OK\n"));
                return TRUE;
            }

#ifdef VBOXWDDMDISP_DEBUG_VEHANDLER
            vboxVDbgVEHandlerUnregister();
#endif
            break;
        }

        case DLL_PROCESS_DETACH:
        {
#ifdef VBOXWDDMDISP_DEBUG_VEHANDLER
            vboxVDbgVEHandlerUnregister();
#endif
            /// @todo RTR3Term();
            VBoxDispD3DGlobalTerm();
            return TRUE;

            break;
        }

        default:
            return TRUE;
    }
    return FALSE;
}
