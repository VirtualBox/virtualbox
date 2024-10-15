/* $Id: VBoxGallium.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium driver interface. Constructs Gallium stack.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#include "VBoxGallium.h"
#include <VBoxGaNine.h>

#include "VBoxD3DAdapter9.h"
#include "VBoxPresent.h"
#include "VBoxGaD3DDevice9Ex.h"
#include "GaDrvEnvWddm.h"

#include <VBox/log.h>

#include <iprt/asm.h>
#include <iprt/string.h>

#if VBOX_MESA_V_MAJOR < 24
/*
 * Loading Gallium state tracker and driver:
 *   1) load the hardware driver VBoxVMSVGA or VBoxVirGL:
 *     a) get an entry point to create the pipe_screen;
 *     b) create the pipe_screen passing handles required to call the WDDM miniport driver.
 *   2) load VBoxNine:
 *     a) get an entry point to create the ID3DAdapter interface (GaNineD3DAdapter9Create);
 *     b) create ID3DAdapter passing the pipe_screen pointer.
 *   3) create GaDirect3D9Ex to have IDirect3DEx or GaDirect3DDevice9Ex to have IDirect3DDevice9Ex,
 *      which is returned to WDDM user mode driver to substitute wine's IDirect3DDevice9Ex.
 */

static const char *gpszNineDll =
#ifdef VBOX_WDDM_WOW64
    "VBoxNine-x86.dll"
#else
    "VBoxNine.dll"
#endif
;

static const char *gpszSvgaDll =
#ifdef VBOX_WDDM_WOW64
    "VBoxSVGA-x86.dll"
#else
    "VBoxSVGA.dll"
#endif
;
#else /* VBOX_MESA_V_MAJOR >= 24 */
/*
 * Loading Gallium D3D9 state tracker and driver:
 *   1) load the state tracker DLL (VBoxNine) which contains the driver code too
 *      (they have to be in the same dll because both use and compare adresses of global constant
 *       glsl_type glsl_type_builtin_* structures from Mesa code).
 *     a) get an entry point to create the ID3DAdapter interface (GaNineD3DAdapter9Create);
 *     b) create ID3DAdapter, which will create the pipe_screen to access the driver internally.
 *   3) create GaDirect3D9Ex to have IDirect3DEx or GaDirect3DDevice9Ex to have IDirect3DDevice9Ex,
 *      which is returned to WDDM user mode driver to substitute wine's IDirect3DDevice9Ex.
 */
static const char *gpszNineDll =
#ifdef VBOX_WDDM_WOW64
    "VBoxNine-x86.dll"
#else
    "VBoxNine.dll"
#endif
;
#endif /* VBOX_MESA_V_MAJOR >= 24 */

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
    {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return NULL;
    }
    szPath[cchPath] = '\\';
    memcpy(&szPath[cchPath + 1], pszName, cbName);
    return LoadLibraryA(szPath);
}

struct VBOXGAPROC
{
    const char *pszName;
    FARPROC *ppfn;
};

static HRESULT getProcAddresses(HMODULE hmod, struct VBOXGAPROC *paProcs)
{
    struct VBOXGAPROC *pIter = paProcs;
    while (pIter->pszName)
    {
        FARPROC pfn = GetProcAddress(hmod, pIter->pszName);
        if (pfn == NULL)
        {
            Log(("Failed to get the entry point: %s\n", pIter->pszName));
            return E_FAIL;
        }

        *pIter->ppfn = pfn;
        ++pIter;
    }

    return S_OK;
}

static HRESULT loadDll(const char *pszName, HMODULE *phmod, struct VBOXGAPROC *paProcs)
{
    *phmod = loadSystemDll(pszName);
    if (!*phmod)
    {
        Log(("Failed to load the DLL: %s\n", pszName));
        return E_FAIL;
    }

    return getProcAddresses(*phmod, paProcs);
}

/*
 * GalliumStack
 *
 * Load Gallium dlls and provide helpers to create D3D9 interfaces and call Gallium driver API.
 */
class VBoxGalliumStack: public IGalliumStack
{
    public:
        VBoxGalliumStack();
        virtual ~VBoxGalliumStack();

        HRESULT Load();

        /* IUnknown methods */
        STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj);
        STDMETHOD_(ULONG,AddRef)(THIS);
        STDMETHOD_(ULONG,Release)(THIS);

        /* IGalliumStack */
        STDMETHOD(CreateDirect3DEx)(HANDLE hAdapter,
                                    HANDLE hDevice,
                                    const D3DDDI_DEVICECALLBACKS *pDeviceCallbacks,
                                    const VBOXGAHWINFO *pHWInfo,
                                    IDirect3D9Ex **ppOut);
        STDMETHOD(GaCreateDeviceEx)(THIS_
                                    D3DDEVTYPE DeviceType,HWND hFocusWindow,DWORD BehaviorFlags,
                                    D3DPRESENT_PARAMETERS* pPresentationParameters,
                                    D3DDISPLAYMODEEX* pFullscreenDisplayMode,
                                    HANDLE hAdapter,
                                    HANDLE hDevice,
                                    const D3DDDI_DEVICECALLBACKS *pDeviceCallbacks,
                                    const VBOXGAHWINFO *pHWInfo,
                                    IDirect3DDevice9Ex** ppReturnedDeviceInterface);

#if VBOX_MESA_V_MAJOR < 24
        STDMETHOD(GaNineD3DAdapter9Create)(struct pipe_screen *s, ID3DAdapter9 **ppOut);
        STDMETHOD_(struct pipe_resource *, GaNinePipeResourceFromSurface)(IUnknown *pSurface);
        STDMETHOD_(struct pipe_context *, GaNinePipeContextFromDevice)(IDirect3DDevice9 *pDevice);
#else
        STDMETHOD(GaNineD3DAdapter9Create)(const WDDMGalliumDriverEnv *pEnv, ID3DAdapter9 **ppOut);
        STDMETHOD_(uint32_t, GaNineGetSurfaceId)(IUnknown *pSurface);
        STDMETHOD_(uint32_t, GaNineGetContextId)(IDirect3DDevice9 *pDevice);
        STDMETHOD_(void, GaNineFlush)(IDirect3DDevice9 *pDevice);
#endif

#if VBOX_MESA_V_MAJOR < 24
        STDMETHOD_(struct pipe_screen *, GaDrvScreenCreate)(const WDDMGalliumDriverEnv *pEnv);
        STDMETHOD_(void, GaDrvScreenDestroy)(struct pipe_screen *s);
        STDMETHOD_(WDDMGalliumDriverEnv const *, GaDrvGetWDDMEnv)(struct pipe_screen *pScreen);
        STDMETHOD_(uint32_t, GaDrvGetContextId)(struct pipe_context *pPipeContext);
        STDMETHOD_(uint32_t, GaDrvGetSurfaceId)(struct pipe_screen *pScreen, struct pipe_resource *pResource);
        STDMETHOD_(void, GaDrvContextFlush)(struct pipe_context *pPipeContext);
#endif

    private:
        void unload();

        volatile ULONG mcRefs;

        HMODULE mhmodStateTracker;
#if VBOX_MESA_V_MAJOR < 24
        HMODULE mhmodDriver;
#endif

        struct GaNineFunctions
        {
#if VBOX_MESA_V_MAJOR < 24
            PFNGaNineD3DAdapter9Create       pfnGaNineD3DAdapter9Create;
            PFNGaNinePipeResourceFromSurface pfnGaNinePipeResourceFromSurface;
            PFNGaNinePipeContextFromDevice   pfnGaNinePipeContextFromDevice;
#else
            PFNGaNineD3DAdapter9Create pfnGaNineD3DAdapter9Create;
            PFNGaNineGetSurfaceId      pfnGaNineGetSurfaceId;
            PFNGaNineGetContextId      pfnGaNineGetContextId;
            PFNGaNineFlush             pfnGaNineFlush;
#endif
        } mNine;

#if VBOX_MESA_V_MAJOR < 24
        struct GaDrvFunctions
        {
            PFNGaDrvScreenCreate  pfnGaDrvScreenCreate;
            PFNGaDrvScreenDestroy pfnGaDrvScreenDestroy;
            PFNGaDrvGetWDDMEnv    pfnGaDrvGetWDDMEnv;
            PFNGaDrvGetContextId  pfnGaDrvGetContextId;
            PFNGaDrvGetSurfaceId  pfnGaDrvGetSurfaceId;
            PFNGaDrvContextFlush  pfnGaDrvContextFlush;
        } mDrv;
#endif
};


/*
 * IDirect3D9Ex implementation corresponds to one WDDM device.
 */
class GaDirect3D9Ex: public IGaDirect3D9Ex
{
    public:
        GaDirect3D9Ex(VBoxGalliumStack *pStack);
        virtual ~GaDirect3D9Ex();

        HRESULT Init(HANDLE hAdapter,
                     HANDLE hDevice,
                     const D3DDDI_DEVICECALLBACKS *pDeviceCallbacks,
                     const VBOXGAHWINFO *pHWInfo);

        /*** IUnknown methods ***/
        STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj);
        STDMETHOD_(ULONG,AddRef)(THIS);
        STDMETHOD_(ULONG,Release)(THIS);

        /*** IDirect3D9 methods ***/
        STDMETHOD(RegisterSoftwareDevice)(THIS_ void* pInitializeFunction);
        STDMETHOD_(UINT, GetAdapterCount)(THIS);
        STDMETHOD(GetAdapterIdentifier)(THIS_ UINT Adapter,DWORD Flags,D3DADAPTER_IDENTIFIER9* pIdentifier);
        STDMETHOD_(UINT, GetAdapterModeCount)(THIS_ UINT Adapter,D3DFORMAT Format);
        STDMETHOD(EnumAdapterModes)(THIS_ UINT Adapter,D3DFORMAT Format,UINT Mode,D3DDISPLAYMODE* pMode);
        STDMETHOD(GetAdapterDisplayMode)(THIS_ UINT Adapter,D3DDISPLAYMODE* pMode);
        STDMETHOD(CheckDeviceType)(THIS_ UINT Adapter,D3DDEVTYPE DevType,D3DFORMAT AdapterFormat,
                                   D3DFORMAT BackBufferFormat,BOOL bWindowed);
        STDMETHOD(CheckDeviceFormat)(THIS_ UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT AdapterFormat,
                                     DWORD Usage,D3DRESOURCETYPE RType,D3DFORMAT CheckFormat);
        STDMETHOD(CheckDeviceMultiSampleType)(THIS_ UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT SurfaceFormat,
                                              BOOL Windowed,D3DMULTISAMPLE_TYPE MultiSampleType,DWORD* pQualityLevels);
        STDMETHOD(CheckDepthStencilMatch)(THIS_ UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT AdapterFormat,
                                          D3DFORMAT RenderTargetFormat,D3DFORMAT DepthStencilFormat);
        STDMETHOD(CheckDeviceFormatConversion)(THIS_ UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT SourceFormat,
                                               D3DFORMAT TargetFormat);
        STDMETHOD(GetDeviceCaps)(THIS_ UINT Adapter,D3DDEVTYPE DeviceType,D3DCAPS9* pCaps);
        STDMETHOD_(HMONITOR, GetAdapterMonitor)(THIS_ UINT Adapter);
        STDMETHOD(CreateDevice)(THIS_ UINT Adapter,D3DDEVTYPE DeviceType,HWND hFocusWindow,
                                DWORD BehaviorFlags,D3DPRESENT_PARAMETERS* pPresentationParameters,
                                IDirect3DDevice9** ppReturnedDeviceInterface);

        /*** IDirect3D9Ex methods ***/
        STDMETHOD_(UINT, GetAdapterModeCountEx)(THIS_ UINT Adapter,CONST D3DDISPLAYMODEFILTER* pFilter);
        STDMETHOD(EnumAdapterModesEx)(THIS_ UINT Adapter,CONST D3DDISPLAYMODEFILTER* pFilter,
                                      UINT Mode,D3DDISPLAYMODEEX* pMode);
        STDMETHOD(GetAdapterDisplayModeEx)(THIS_ UINT Adapter,D3DDISPLAYMODEEX* pMode,D3DDISPLAYROTATION* pRotation);
        STDMETHOD(CreateDeviceEx)(THIS_ UINT Adapter,D3DDEVTYPE DeviceType,HWND hFocusWindow,DWORD BehaviorFlags,
                                  D3DPRESENT_PARAMETERS* pPresentationParameters,
                                  D3DDISPLAYMODEEX* pFullscreenDisplayMode,
                                  IDirect3DDevice9Ex** ppReturnedDeviceInterface);
        STDMETHOD(GetAdapterLUID)(THIS_ UINT Adapter,LUID * pLUID);

        /* IGaDirect3D9Ex methods */
        STDMETHOD_(IGalliumStack *, GetGalliumStack)(THIS);
        STDMETHOD_(ID3DAdapter9 *, GetAdapter9)(THIS);
#if VBOX_MESA_V_MAJOR < 24
        STDMETHOD_(struct pipe_screen *, GetScreen)(THIS);
#else
        STDMETHOD_(const WDDMGalliumDriverEnv *, GetWDDMEnv)(THIS);
#endif

    private:
        void cleanup();

        volatile ULONG mcRefs;

        VBoxGalliumStack *mpStack;
#if VBOX_MESA_V_MAJOR < 24
        struct pipe_screen *mpPipeScreen;
#endif
        ID3DAdapter9 *mpD3DAdapter9;

        /* The Gallium driver environment helper object. */
        GaDrvEnvWddm mEnv;
};


/*
 * VBoxGalliumStack implementation.
 */

VBoxGalliumStack::VBoxGalliumStack()
    :
    mcRefs(0)
    , mhmodStateTracker(0)
#if VBOX_MESA_V_MAJOR < 24
    , mhmodDriver(0)
#endif
{
    RT_ZERO(mNine);
#if VBOX_MESA_V_MAJOR < 24
    RT_ZERO(mDrv);
#endif
}

VBoxGalliumStack::~VBoxGalliumStack()
{
    unload();
}

STDMETHODIMP_(ULONG) VBoxGalliumStack::AddRef()
{
    ULONG refs = InterlockedIncrement(&mcRefs);
    return refs;
}

STDMETHODIMP_(ULONG) VBoxGalliumStack::Release()
{
    ULONG refs = InterlockedDecrement(&mcRefs);
    if (refs == 0)
    {
        delete this;
    }
    return refs;
}

STDMETHODIMP VBoxGalliumStack::QueryInterface(REFIID riid,
                                              void **ppvObject)
{
    if (!ppvObject)
    {
        return E_POINTER;
    }

    if (IsEqualGUID(IID_IUnknown, riid))
    {
        AddRef();
        *ppvObject = this;
        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

HRESULT VBoxGalliumStack::Load()
{
    struct VBOXGAPROC aNineProcs[] =
    {
#if VBOX_MESA_V_MAJOR < 24
        { "GaNineD3DAdapter9Create",        (FARPROC *)&mNine.pfnGaNineD3DAdapter9Create },
        { "GaNinePipeResourceFromSurface",  (FARPROC *)&mNine.pfnGaNinePipeResourceFromSurface },
        { "GaNinePipeContextFromDevice",    (FARPROC *)&mNine.pfnGaNinePipeContextFromDevice },
#else
        { "GaNineD3DAdapter9Create", (FARPROC *)&mNine.pfnGaNineD3DAdapter9Create },
        { "GaNineGetSurfaceId",      (FARPROC *)&mNine.pfnGaNineGetSurfaceId },
        { "GaNineGetContextId",      (FARPROC *)&mNine.pfnGaNineGetContextId },
        { "GaNineFlush",             (FARPROC *)&mNine.pfnGaNineFlush },
#endif
        { NULL, NULL }
    };

#if VBOX_MESA_V_MAJOR < 24
    struct VBOXGAPROC aDrvProcs[] =
    {
        { "GaDrvScreenCreate",  (FARPROC *)&mDrv.pfnGaDrvScreenCreate },
        { "GaDrvScreenDestroy", (FARPROC *)&mDrv.pfnGaDrvScreenDestroy },
        { "GaDrvGetWDDMEnv",    (FARPROC *)&mDrv.pfnGaDrvGetWDDMEnv },
        { "GaDrvGetContextId",  (FARPROC *)&mDrv.pfnGaDrvGetContextId },
        { "GaDrvGetSurfaceId",  (FARPROC *)&mDrv.pfnGaDrvGetSurfaceId },
        { "GaDrvContextFlush",  (FARPROC *)&mDrv.pfnGaDrvContextFlush },
        { NULL, NULL }
    };

    /** @todo Select the driver dll according to the hardware. */
    const char *pszDriverDll = gpszSvgaDll;

    HRESULT hr = loadDll(pszDriverDll, &mhmodDriver, aDrvProcs);
    if (SUCCEEDED(hr))
    {
        hr = loadDll(gpszNineDll, &mhmodStateTracker, aNineProcs);
    }
#else
    HRESULT hr = loadDll(gpszNineDll, &mhmodStateTracker, aNineProcs);
#endif

    return hr;
}

void VBoxGalliumStack::unload()
{
    RT_ZERO(mNine);
#if VBOX_MESA_V_MAJOR < 24
    RT_ZERO(mDrv);
#endif

    if (mhmodStateTracker)
    {
        FreeLibrary(mhmodStateTracker);
        mhmodStateTracker = 0;
    }

#if VBOX_MESA_V_MAJOR < 24
    if (mhmodDriver)
    {
        FreeLibrary(mhmodDriver);
        mhmodDriver = 0;
    }
#endif
}

#if VBOX_MESA_V_MAJOR < 24
STDMETHODIMP VBoxGalliumStack::GaNineD3DAdapter9Create(struct pipe_screen *s,
                                                       ID3DAdapter9 **ppOut)
{
    return mNine.pfnGaNineD3DAdapter9Create(s, ppOut);
}

STDMETHODIMP_(struct pipe_resource *) VBoxGalliumStack::GaNinePipeResourceFromSurface(IUnknown *pSurface)
{
    return mNine.pfnGaNinePipeResourceFromSurface(pSurface);
}

STDMETHODIMP_(struct pipe_context *) VBoxGalliumStack::GaNinePipeContextFromDevice(IDirect3DDevice9 *pDevice)
{
    return mNine.pfnGaNinePipeContextFromDevice(pDevice);
}
#else
STDMETHODIMP VBoxGalliumStack::GaNineD3DAdapter9Create(const WDDMGalliumDriverEnv *pEnv,
                                                       ID3DAdapter9 **ppOut)
{
    return mNine.pfnGaNineD3DAdapter9Create(pEnv, ppOut);
}

STDMETHODIMP_(uint32_t) VBoxGalliumStack::GaNineGetSurfaceId(IUnknown *pSurface)
{
    return mNine.pfnGaNineGetSurfaceId(pSurface);
}

STDMETHODIMP_(uint32_t) VBoxGalliumStack::GaNineGetContextId(IDirect3DDevice9 *pDevice)
{
    return mNine.pfnGaNineGetContextId(pDevice);
}

STDMETHODIMP_(void) VBoxGalliumStack::GaNineFlush(IDirect3DDevice9 *pDevice)
{
    return mNine.pfnGaNineFlush(pDevice);
}
#endif

#if VBOX_MESA_V_MAJOR < 24
STDMETHODIMP_(struct pipe_screen *) VBoxGalliumStack::GaDrvScreenCreate(const WDDMGalliumDriverEnv *pEnv)
{
    return mDrv.pfnGaDrvScreenCreate(pEnv);
}

STDMETHODIMP_(void) VBoxGalliumStack::GaDrvScreenDestroy(struct pipe_screen *s)
{
    mDrv.pfnGaDrvScreenDestroy(s);
}

STDMETHODIMP_(WDDMGalliumDriverEnv const *) VBoxGalliumStack::GaDrvGetWDDMEnv(struct pipe_screen *pScreen)
{
    return mDrv.pfnGaDrvGetWDDMEnv(pScreen);
}

STDMETHODIMP_(uint32_t) VBoxGalliumStack::GaDrvGetContextId(struct pipe_context *pPipeContext)
{
    return mDrv.pfnGaDrvGetContextId(pPipeContext);
}

STDMETHODIMP_(uint32_t) VBoxGalliumStack::GaDrvGetSurfaceId(struct pipe_screen *pScreen,
                                                            struct pipe_resource *pResource)
{
    return mDrv.pfnGaDrvGetSurfaceId(pScreen, pResource);
}

STDMETHODIMP_(void) VBoxGalliumStack::GaDrvContextFlush(struct pipe_context *pPipeContext)
{
    mDrv.pfnGaDrvContextFlush(pPipeContext);
}
#endif

STDMETHODIMP VBoxGalliumStack::CreateDirect3DEx(HANDLE hAdapter,
                                                HANDLE hDevice,
                                                const D3DDDI_DEVICECALLBACKS *pDeviceCallbacks,
                                                const VBOXGAHWINFO *pHWInfo,
                                                IDirect3D9Ex **ppOut)
{
    GaDirect3D9Ex *p = new GaDirect3D9Ex(this);
    if (!p)
        return E_OUTOFMEMORY;

    HRESULT hr = p->Init(hAdapter, hDevice, pDeviceCallbacks, pHWInfo);
    if (SUCCEEDED(hr))
    {
        p->AddRef();
        *ppOut = p;
    }
    else
    {
        delete p;
    }

    return hr;
}

STDMETHODIMP VBoxGalliumStack::GaCreateDeviceEx(D3DDEVTYPE DeviceType,
                                                HWND hFocusWindow,
                                                DWORD BehaviorFlags,
                                                D3DPRESENT_PARAMETERS* pPresentationParameters,
                                                D3DDISPLAYMODEEX* pFullscreenDisplayMode,
                                                HANDLE hAdapter,
                                                HANDLE hDevice,
                                                const D3DDDI_DEVICECALLBACKS *pDeviceCallbacks,
                                                const VBOXGAHWINFO *pHWInfo,
                                                IDirect3DDevice9Ex **ppReturnedDeviceInterface)
{
    /* Create the per-WDDM-device gallium adapter. */
    IGaDirect3D9Ex *pD3D9 = NULL;
    HRESULT hr = CreateDirect3DEx(hAdapter,
                                  hDevice,
                                  pDeviceCallbacks,
                                  pHWInfo,
                                  (IDirect3D9Ex **)&pD3D9);
    if (SUCCEEDED(hr))
    {
        /* Create wrapper object for IDirect3DDevice9Ex */
        GaDirect3DDevice9Ex *p = new GaDirect3DDevice9Ex(pD3D9, hAdapter, hDevice, pDeviceCallbacks);
        if (p)
        {
            hr = p->Init(DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, pFullscreenDisplayMode);
            if (SUCCEEDED(hr))
            {
                p->AddRef();
                *ppReturnedDeviceInterface = p;
            }
            else
            {
                delete p;
            }
        }
        else
        {
            hr = E_OUTOFMEMORY;
        }

        pD3D9->Release();
    }

    return hr;
}

HRESULT GalliumStackCreate(IGalliumStack **ppOut)
{
    VBoxGalliumStack *p = new VBoxGalliumStack();
    if (!p)
        return E_OUTOFMEMORY;

    HRESULT hr = p->Load();
    if (SUCCEEDED(hr))
    {
        p->AddRef();
        *ppOut = p;
    }
    else
    {
        delete p;
    }

    return hr;
}


/*
 * GaDirect3D9Ex
 *
 * IDirect3D9Ex implementation based on Gallium D3D9 state tracker "nine".
 */

GaDirect3D9Ex::GaDirect3D9Ex(VBoxGalliumStack *pStack)
    :
    mcRefs(0),
    mpStack(pStack),
#if VBOX_MESA_V_MAJOR < 24
    mpPipeScreen(0),
#endif
    mpD3DAdapter9(0)
{
    mpStack->AddRef();
}

GaDirect3D9Ex::~GaDirect3D9Ex()
{
    cleanup();
}

STDMETHODIMP_(ULONG) GaDirect3D9Ex::AddRef()
{
    ULONG refs = InterlockedIncrement(&mcRefs);
    return refs;
}

STDMETHODIMP_(ULONG) GaDirect3D9Ex::Release()
{
    ULONG refs = InterlockedDecrement(&mcRefs);
    if (refs == 0)
    {
        delete this;
    }
    return refs;
}

STDMETHODIMP GaDirect3D9Ex::QueryInterface(REFIID riid,
                                           void **ppvObject)
{
    if (!ppvObject)
    {
        return E_POINTER;
    }

    if (   IsEqualGUID(IID_IGaDirect3D9Ex, riid)
        || IsEqualGUID(IID_IDirect3D9Ex, riid)
        || IsEqualGUID(IID_IDirect3D9, riid)
        || IsEqualGUID(IID_IUnknown, riid))
    {
        AddRef();
        *ppvObject = this;
        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

HRESULT GaDirect3D9Ex::Init(HANDLE hAdapter,
                            HANDLE hDevice,
                            const D3DDDI_DEVICECALLBACKS *pDeviceCallbacks,
                            const VBOXGAHWINFO *pHWInfo)
{
    LogFunc(("%p %p %p\n", hAdapter, hDevice, pDeviceCallbacks));

    HRESULT hr;

    mEnv.Init(hAdapter, hDevice, pDeviceCallbacks, pHWInfo);
    const WDDMGalliumDriverEnv *pEnv = mEnv.Env();

#if VBOX_MESA_V_MAJOR < 24
    mpPipeScreen = mpStack->GaDrvScreenCreate(pEnv);
    if (mpPipeScreen)
    {
        hr = mpStack->GaNineD3DAdapter9Create(mpPipeScreen, &mpD3DAdapter9);
        Assert(SUCCEEDED(hr));
    }
    else
    {
        hr = E_FAIL;
        AssertFailed();
    }
#else
    hr = mpStack->GaNineD3DAdapter9Create(pEnv, &mpD3DAdapter9);
    Assert(SUCCEEDED(hr));
#endif

    return hr;
}

void GaDirect3D9Ex::cleanup()
{
    if (mpD3DAdapter9)
    {
        D3DAdapter9_Release(mpD3DAdapter9);
        mpD3DAdapter9 = NULL;
    }

#if VBOX_MESA_V_MAJOR < 24
    if (mpPipeScreen)
    {
        mpStack->GaDrvScreenDestroy(mpPipeScreen);
        mpPipeScreen = NULL;
    }
#endif

    if (mpStack)
    {
        mpStack->Release();
        mpStack = 0;
    }
}

#define TRAPNOTIMPL do { \
    ASMBreakpoint(); \
} while (0)


STDMETHODIMP GaDirect3D9Ex::RegisterSoftwareDevice(void *pInitializeFunction)
{
    RT_NOREF(pInitializeFunction);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP_(UINT) GaDirect3D9Ex::GetAdapterCount()
{
    TRAPNOTIMPL;
    return 1;
}

STDMETHODIMP GaDirect3D9Ex::GetAdapterIdentifier(UINT Adapter,
                                                 DWORD Flags,
                                                 D3DADAPTER_IDENTIFIER9 *pIdentifier)
{
    RT_NOREF3(Adapter, Flags, pIdentifier);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP_(UINT) GaDirect3D9Ex::GetAdapterModeCount(UINT Adapter,
                                                       D3DFORMAT Format)
{
    RT_NOREF2(Adapter, Format);
    TRAPNOTIMPL;
    return 1;
}

STDMETHODIMP GaDirect3D9Ex::EnumAdapterModes(UINT Adapter,
                                             D3DFORMAT Format,
                                             UINT Mode,
                                             D3DDISPLAYMODE *pMode)
{
    RT_NOREF4(Adapter, Format, Mode, pMode);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP GaDirect3D9Ex::GetAdapterDisplayMode(UINT Adapter,
                                                  D3DDISPLAYMODE *pMode)
{
    RT_NOREF2(Adapter, pMode);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP GaDirect3D9Ex::CheckDeviceType(UINT iAdapter,
                                            D3DDEVTYPE DevType,
                                            D3DFORMAT DisplayFormat,
                                            D3DFORMAT BackBufferFormat,
                                            BOOL bWindowed)
{
    RT_NOREF5(iAdapter, DevType, DisplayFormat, BackBufferFormat, bWindowed);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP GaDirect3D9Ex::CheckDeviceFormat(UINT Adapter,
                                              D3DDEVTYPE DeviceType,
                                              D3DFORMAT AdapterFormat,
                                              DWORD Usage,
                                              D3DRESOURCETYPE RType,
                                              D3DFORMAT CheckFormat)
{
    RT_NOREF6(Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP GaDirect3D9Ex::CheckDeviceMultiSampleType(UINT Adapter,
                                                       D3DDEVTYPE DeviceType,
                                                       D3DFORMAT SurfaceFormat,
                                                       BOOL Windowed,
                                                       D3DMULTISAMPLE_TYPE MultiSampleType,
                                                       DWORD *pQualityLevels)
{
    RT_NOREF6(Adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType, pQualityLevels);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP GaDirect3D9Ex::CheckDepthStencilMatch(UINT Adapter,
                                                   D3DDEVTYPE DeviceType,
                                                   D3DFORMAT AdapterFormat,
                                                   D3DFORMAT RenderTargetFormat,
                                                   D3DFORMAT DepthStencilFormat)
{
    RT_NOREF5(Adapter, DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP GaDirect3D9Ex::CheckDeviceFormatConversion(UINT Adapter,
                                                        D3DDEVTYPE DeviceType,
                                                        D3DFORMAT SourceFormat,
                                                        D3DFORMAT TargetFormat)
{
    RT_NOREF4(Adapter, DeviceType, SourceFormat, TargetFormat);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP GaDirect3D9Ex::GetDeviceCaps(UINT Adapter,
                                          D3DDEVTYPE DeviceType,
                                          D3DCAPS9 *pCaps)
{
    RT_NOREF(Adapter);
    HRESULT hr = D3DAdapter9_GetDeviceCaps(mpD3DAdapter9, DeviceType, pCaps);
    return hr;
}

STDMETHODIMP_(HMONITOR) GaDirect3D9Ex::GetAdapterMonitor(UINT Adapter)
{
    RT_NOREF(Adapter);
    TRAPNOTIMPL;
    return NULL;
}

STDMETHODIMP GaDirect3D9Ex::CreateDevice(UINT Adapter,
                                         D3DDEVTYPE DeviceType,
                                         HWND hFocusWindow,
                                         DWORD BehaviorFlags,
                                         D3DPRESENT_PARAMETERS *pPresentationParameters,
                                         IDirect3DDevice9 **ppReturnedDeviceInterface)
{
    return CreateDeviceEx(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters,
                          NULL, (IDirect3DDevice9Ex **)ppReturnedDeviceInterface);
}

STDMETHODIMP_(UINT) GaDirect3D9Ex::GetAdapterModeCountEx(UINT Adapter,
                                                         CONST D3DDISPLAYMODEFILTER *pFilter)
{
    RT_NOREF2(Adapter, pFilter);
    TRAPNOTIMPL;
    return 1;
}

STDMETHODIMP GaDirect3D9Ex::EnumAdapterModesEx(UINT Adapter,
                                               CONST D3DDISPLAYMODEFILTER *pFilter,
                                               UINT Mode,
                                               D3DDISPLAYMODEEX *pMode)
{
    RT_NOREF4(Adapter, pFilter, Mode, pMode);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP GaDirect3D9Ex::GetAdapterDisplayModeEx(UINT Adapter,
                                                    D3DDISPLAYMODEEX *pMode,
                                                    D3DDISPLAYROTATION *pRotation)
{
    RT_NOREF3(Adapter, pMode, pRotation);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP GaDirect3D9Ex::CreateDeviceEx(UINT Adapter,
                                           D3DDEVTYPE DeviceType,
                                           HWND hFocusWindow,
                                           DWORD BehaviorFlags,
                                           D3DPRESENT_PARAMETERS *pPresentationParameters,
                                           D3DDISPLAYMODEEX *pFullscreenDisplayMode,
                                           IDirect3DDevice9Ex **ppReturnedDeviceInterface)
{
    RT_NOREF7(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters,
              pFullscreenDisplayMode, ppReturnedDeviceInterface);
    /* This method should never be called. GaCreateDeviceEx is the right one. */
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP GaDirect3D9Ex::GetAdapterLUID(UINT Adapter,
                                           LUID *pLUID)
{
    RT_NOREF2(Adapter, pLUID);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP_(IGalliumStack *) GaDirect3D9Ex::GetGalliumStack(void)
{
    return mpStack;
}

STDMETHODIMP_(ID3DAdapter9 *) GaDirect3D9Ex::GetAdapter9(void)
{
    return mpD3DAdapter9;
}

#if VBOX_MESA_V_MAJOR < 24
STDMETHODIMP_(struct pipe_screen *) GaDirect3D9Ex::GetScreen(void)
{
    return mpPipeScreen;
}
#else
STDMETHODIMP_(const WDDMGalliumDriverEnv *) GaDirect3D9Ex::GetWDDMEnv(void)
{
    return mEnv.Env();
}
#endif
