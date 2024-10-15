/* $Id: D3DKMT.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * WDDM Kernel Mode Thunks helpers.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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

/* We're unable to use standard r3 vbgl-based backdoor logging API because win8 Metro apps
 * can not do CreateFile/Read/Write by default.
 * This is why we use miniport escape functionality to issue backdoor log string to the miniport
 * and submit it to host via standard r0 backdoor logging api accordingly
 */

#include "UmHlpInternal.h"


/** Loads a system DLL.
 *
 * @returns Module handle or NULL
 * @param   pszName             The DLL name.
 */
DECLCALLBACK(HMODULE) VBoxWddmLoadSystemDll(const char *pszName)
{
    char   szPath[MAX_PATH];
    UINT   cchPath = GetSystemDirectoryA(szPath, sizeof(szPath));
    size_t cbName  = strlen(pszName) + 1;
    if (cchPath + 1 + cbName > sizeof(szPath))
    {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return NULL;
    }
    szPath[cchPath] = '\\';
    memcpy(&szPath[cchPath + 1], pszName, cbName);
    return LoadLibraryA(szPath);
}

DECLCALLBACK(void) VBoxWddmLoadAdresses(HMODULE hmod, VBOXWDDMDLLPROC *paProcs)
{
    struct VBOXWDDMDLLPROC *pIter = paProcs;
    while (pIter->pszName)
    {
        FARPROC pfn = GetProcAddress(hmod, pIter->pszName);
        *pIter->ppfn = pfn;
        ++pIter;
    }
}

/*
 * Kernel Mode Thunks (KMT) initialization.
 */

#define D3DKMT_LOAD_ENTRY(a) { #a, (FARPROC *)&g_D3DKMT.pfn##a }

static D3DKMTFUNCTIONS g_D3DKMT = { 0 };
static VBOXWDDMDLLPROC g_D3DKMTLoadTable[] =
{
    D3DKMT_LOAD_ENTRY(D3DKMTOpenAdapterFromHdc),
    D3DKMT_LOAD_ENTRY(D3DKMTOpenAdapterFromDeviceName),
    D3DKMT_LOAD_ENTRY(D3DKMTCloseAdapter),
    D3DKMT_LOAD_ENTRY(D3DKMTQueryAdapterInfo),
    D3DKMT_LOAD_ENTRY(D3DKMTEscape),
    D3DKMT_LOAD_ENTRY(D3DKMTCreateDevice),
    D3DKMT_LOAD_ENTRY(D3DKMTDestroyDevice),
    D3DKMT_LOAD_ENTRY(D3DKMTCreateContext),
    D3DKMT_LOAD_ENTRY(D3DKMTDestroyContext),
    D3DKMT_LOAD_ENTRY(D3DKMTCreateAllocation),
    D3DKMT_LOAD_ENTRY(D3DKMTDestroyAllocation),
    D3DKMT_LOAD_ENTRY(D3DKMTRender),
    D3DKMT_LOAD_ENTRY(D3DKMTPresent),
    D3DKMT_LOAD_ENTRY(D3DKMTGetSharedPrimaryHandle),
    D3DKMT_LOAD_ENTRY(D3DKMTQueryResourceInfo),
    D3DKMT_LOAD_ENTRY(D3DKMTOpenResource),
    D3DKMT_LOAD_ENTRY(D3DKMTEnumAdapters),
    D3DKMT_LOAD_ENTRY(D3DKMTOpenAdapterFromLuid),
    {NULL, NULL},
};

#undef D3DKMT_LOAD_ENTRY

/** Initialize Kernel Mode Thunks (KMT) pointers in the g_D3DKMT structure.
 *
 * @returns True if successful.
 */
DECLCALLBACK(int) D3DKMTLoad(void)
{
    /* Modules which use D3DKMT must link with gdi32. */
    HMODULE hmod = GetModuleHandleA("gdi32.dll");
    Assert(hmod);
    if (hmod)
    {
        VBoxWddmLoadAdresses(hmod, g_D3DKMTLoadTable);
    }
    return hmod != NULL;
}

DECLCALLBACK(D3DKMTFUNCTIONS const *) D3DKMTFunctions(void)
{
    return &g_D3DKMT;
}


/*
 * Getting VirtualBox Graphics Adapter handle.
 */

static NTSTATUS vboxDispKmtOpenAdapterFromHdc(D3DKMT_HANDLE *phAdapter, LUID *pLuid)
{
    *phAdapter = 0;

    D3DKMTFUNCTIONS const *d3dkmt = D3DKMTFunctions();
    if (d3dkmt->pfnD3DKMTOpenAdapterFromHdc == NULL)
        return STATUS_NOT_SUPPORTED;

    D3DKMT_OPENADAPTERFROMHDC OpenAdapterData;
    memset(&OpenAdapterData, 0, sizeof(OpenAdapterData));

    for (int i = 0; ; ++i)
    {
        DISPLAY_DEVICEA dd;
        memset(&dd, 0, sizeof(dd));
        dd.cb = sizeof(dd);

        if (!EnumDisplayDevicesA(NULL, i, &dd, 0))
            break;

        if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
        {
            OpenAdapterData.hDc = CreateDCA(NULL, dd.DeviceName, NULL, NULL);
            break;
        }
    }

    Assert(OpenAdapterData.hDc);

    NTSTATUS Status;
    if (OpenAdapterData.hDc)
    {
        Status = d3dkmt->pfnD3DKMTOpenAdapterFromHdc(&OpenAdapterData);
        Assert(Status == STATUS_SUCCESS);
        if (Status == STATUS_SUCCESS)
        {
            *phAdapter = OpenAdapterData.hAdapter;
            if (pLuid)
            {
               *pLuid = OpenAdapterData.AdapterLuid;
            }
        }

        DeleteDC(OpenAdapterData.hDc);
    }
    else
    {
        Status = STATUS_NOT_SUPPORTED;
    }

    return Status;
}

static NTSTATUS vboxDispKmtOpenAdapterFromLuid(D3DKMT_HANDLE *phAdapter, LUID *pLuid)
{
    *phAdapter = 0;

    D3DKMTFUNCTIONS const *d3dkmt = D3DKMTFunctions();
    if (   d3dkmt->pfnD3DKMTOpenAdapterFromLuid == NULL
        || d3dkmt->pfnD3DKMTEnumAdapters == NULL)
        return STATUS_NOT_SUPPORTED;

    D3DKMT_ENUMADAPTERS EnumAdaptersData;
    memset(&EnumAdaptersData, 0, sizeof(EnumAdaptersData));
    EnumAdaptersData.NumAdapters = RT_ELEMENTS(EnumAdaptersData.Adapters);

    NTSTATUS Status = d3dkmt->pfnD3DKMTEnumAdapters(&EnumAdaptersData);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        Assert(EnumAdaptersData.NumAdapters);

        /* Try the same twice: if we fail to open the adapter containing present sources,
         * then try to open any adapter.
         */
        for (int iPass = 0; iPass < 2 && *phAdapter == 0; ++iPass)
        {
            for (ULONG i = 0; i < EnumAdaptersData.NumAdapters; ++i)
            {
                if (iPass > 0 || EnumAdaptersData.Adapters[i].NumOfSources)
                {
                    D3DKMT_OPENADAPTERFROMLUID OpenAdapterData;
                    memset(&OpenAdapterData, 0, sizeof(OpenAdapterData));
                    OpenAdapterData.AdapterLuid = EnumAdaptersData.Adapters[i].AdapterLuid;

                    Status = d3dkmt->pfnD3DKMTOpenAdapterFromLuid(&OpenAdapterData);
                    Assert(Status == STATUS_SUCCESS);
                    if (Status == STATUS_SUCCESS)
                    {
                        *phAdapter = OpenAdapterData.hAdapter;
                        if (pLuid)
                        {
                            *pLuid = EnumAdaptersData.Adapters[i].AdapterLuid;
                        }
                        break;
                    }
                }
            }
        }
    }

    return Status;
}

NTSTATUS vboxDispKmtOpenAdapter2(D3DKMT_HANDLE *phAdapter, LUID *pLuid)
{
    NTSTATUS Status = vboxDispKmtOpenAdapterFromLuid(phAdapter, pLuid);
    if (Status != STATUS_SUCCESS)
    {
        /* Fallback for pre-Windows8 */
        Status = vboxDispKmtOpenAdapterFromHdc(phAdapter, pLuid);
    }

    return Status;
}

NTSTATUS vboxDispKmtOpenAdapter(D3DKMT_HANDLE *phAdapter)
{
    return vboxDispKmtOpenAdapter2(phAdapter, NULL);
}

NTSTATUS vboxDispKmtCloseAdapter(D3DKMT_HANDLE hAdapter)
{
    D3DKMTFUNCTIONS const *d3dkmt = D3DKMTFunctions();
    if (d3dkmt->pfnD3DKMTCloseAdapter == NULL)
        return STATUS_NOT_SUPPORTED;

    D3DKMT_CLOSEADAPTER CloseAdapterData;
    CloseAdapterData.hAdapter = hAdapter;

    NTSTATUS Status = d3dkmt->pfnD3DKMTCloseAdapter(&CloseAdapterData);
    Assert(Status == STATUS_SUCCESS);

    return Status;
}
