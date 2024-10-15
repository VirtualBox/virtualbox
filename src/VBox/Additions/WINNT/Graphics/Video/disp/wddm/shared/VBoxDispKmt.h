/* $Id: VBoxDispKmt.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_shared_VBoxDispKmt_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_shared_VBoxDispKmt_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/win/d3dkmthk.h>

#include "../../../common/wddm/VBoxMPIf.h"

#ifndef DXGKDDI_INTERFACE_VERSION_WIN8
# define DXGKDDI_INTERFACE_VERSION_WIN8 0x300e
#endif
#if DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WIN8

/* win8 release preview-specific stuff */
typedef struct _D3DKMT_ADAPTERINFO
{
  D3DKMT_HANDLE hAdapter;
  LUID          AdapterLuid;
  ULONG         NumOfSources;
  BOOL          bPresentMoveRegionsPreferred;
} D3DKMT_ADAPTERINFO;

# define MAX_ENUM_ADAPTERS 16

typedef struct _D3DKMT_ENUMADAPTERS
{
  ULONG              NumAdapters;
  D3DKMT_ADAPTERINFO Adapters[MAX_ENUM_ADAPTERS];
} D3DKMT_ENUMADAPTERS;

typedef DECLCALLBACKPTR_EX(NTSTATUS, APIENTRY, PFND3DKMT_ENUMADAPTERS,(IN OUT D3DKMT_ENUMADAPTERS *));

typedef struct _D3DKMT_OPENADAPTERFROMLUID
{
  LUID          AdapterLuid;
  D3DKMT_HANDLE hAdapter;
} D3DKMT_OPENADAPTERFROMLUID;

typedef DECLCALLBACKPTR_EX(NTSTATUS, APIENTRY, PFND3DKMT_OPENADAPTERFROMLUID,(IN OUT D3DKMT_OPENADAPTERFROMLUID *));

#endif /* DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WIN8  */

typedef enum
{
    VBOXDISPKMT_CALLBACKS_VERSION_UNDEFINED = 0,
    VBOXDISPKMT_CALLBACKS_VERSION_VISTA_WIN7,
    VBOXDISPKMT_CALLBACKS_VERSION_WIN8
} VBOXDISPKMT_CALLBACKS_VERSION;

typedef struct VBOXDISPKMT_CALLBACKS
{
    HMODULE hGdi32;
    VBOXDISPKMT_CALLBACKS_VERSION enmVersion;
    /* open adapter */
    PFND3DKMT_OPENADAPTERFROMHDC pfnD3DKMTOpenAdapterFromHdc;
    PFND3DKMT_OPENADAPTERFROMGDIDISPLAYNAME pfnD3DKMTOpenAdapterFromGdiDisplayName;
    /* close adapter */
    PFND3DKMT_CLOSEADAPTER pfnD3DKMTCloseAdapter;
    /* escape */
    PFND3DKMT_ESCAPE pfnD3DKMTEscape;

    PFND3DKMT_QUERYADAPTERINFO pfnD3DKMTQueryAdapterInfo;

    PFND3DKMT_CREATEDEVICE pfnD3DKMTCreateDevice;
    PFND3DKMT_DESTROYDEVICE pfnD3DKMTDestroyDevice;
    PFND3DKMT_CREATECONTEXT pfnD3DKMTCreateContext;
    PFND3DKMT_DESTROYCONTEXT pfnD3DKMTDestroyContext;

    PFND3DKMT_RENDER pfnD3DKMTRender;

    PFND3DKMT_CREATEALLOCATION pfnD3DKMTCreateAllocation;
    PFND3DKMT_DESTROYALLOCATION pfnD3DKMTDestroyAllocation;

    PFND3DKMT_LOCK pfnD3DKMTLock;
    PFND3DKMT_UNLOCK pfnD3DKMTUnlock;

    /* auto resize support */
    PFND3DKMT_INVALIDATEACTIVEVIDPN pfnD3DKMTInvalidateActiveVidPn;
    PFND3DKMT_POLLDISPLAYCHILDREN pfnD3DKMTPollDisplayChildren;

    /* win8 specifics */
    PFND3DKMT_ENUMADAPTERS pfnD3DKMTEnumAdapters;
    PFND3DKMT_OPENADAPTERFROMLUID pfnD3DKMTOpenAdapterFromLuid;
} VBOXDISPKMT_CALLBACKS, *PVBOXDISPKMT_CALLBACKS;

typedef struct VBOXDISPKMT_ADAPTER
{
    D3DKMT_HANDLE hAdapter;
    HDC hDc;
    LUID Luid;
    const VBOXDISPKMT_CALLBACKS *pCallbacks;
}VBOXDISPKMT_ADAPTER, *PVBOXDISPKMT_ADAPTER;

typedef struct VBOXDISPKMT_DEVICE
{
    struct VBOXDISPKMT_ADAPTER *pAdapter;
    D3DKMT_HANDLE hDevice;
    VOID *pCommandBuffer;
    UINT CommandBufferSize;
    D3DDDI_ALLOCATIONLIST *pAllocationList;
    UINT AllocationListSize;
    D3DDDI_PATCHLOCATIONLIST *pPatchLocationList;
    UINT PatchLocationListSize;
}VBOXDISPKMT_DEVICE, *PVBOXDISPKMT_DEVICE;

typedef struct VBOXDISPKMT_CONTEXT
{
    struct VBOXDISPKMT_DEVICE *pDevice;
    D3DKMT_HANDLE hContext;
    VOID *pCommandBuffer;
    UINT CommandBufferSize;
    D3DDDI_ALLOCATIONLIST *pAllocationList;
    UINT AllocationListSize;
    D3DDDI_PATCHLOCATIONLIST *pPatchLocationList;
    UINT PatchLocationListSize;
} VBOXDISPKMT_CONTEXT, *PVBOXDISPKMT_CONTEXT;

HRESULT vboxDispKmtCallbacksInit(PVBOXDISPKMT_CALLBACKS pCallbacks);
HRESULT vboxDispKmtCallbacksTerm(PVBOXDISPKMT_CALLBACKS pCallbacks);

HRESULT vboxDispKmtOpenAdapter(const VBOXDISPKMT_CALLBACKS *pCallbacks, PVBOXDISPKMT_ADAPTER pAdapter);
HRESULT vboxDispKmtCloseAdapter(PVBOXDISPKMT_ADAPTER pAdapter);
HRESULT vboxDispKmtCreateDevice(PVBOXDISPKMT_ADAPTER pAdapter, PVBOXDISPKMT_DEVICE pDevice);
HRESULT vboxDispKmtDestroyDevice(PVBOXDISPKMT_DEVICE pDevice);
HRESULT vboxDispKmtCreateContext(PVBOXDISPKMT_DEVICE pDevice, PVBOXDISPKMT_CONTEXT pContext,
        VBOXWDDM_CONTEXT_TYPE enmType,
        HANDLE hEvent, uint64_t u64UmInfo);
HRESULT vboxDispKmtDestroyContext(PVBOXDISPKMT_CONTEXT pContext);


#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_shared_VBoxDispKmt_h */
