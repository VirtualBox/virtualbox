/* $Id: VBoxGL.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - OpenGL driver.
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

#include "GaDrvEnvKMT.h"

#include "stw_winsys.h"
#include "stw_device.h"
#include "stw_context.h"

#include "pipe/p_state.h"
#if VBOX_MESA_V_MAJOR >= 24
#include "state_tracker/st_context.h"
#endif
#include "svga3d_reg.h"

#include <iprt/asm.h>
#include <iprt/initterm.h>

#include <common/wddm/VBoxMPIf.h>

#include <Psapi.h>

#if VBOX_MESA_V_MAJOR < 24
static const char *g_pszSvgaDll =
#ifdef VBOX_WOW64
    "VBoxSVGA-x86.dll"
#else
    "VBoxSVGA.dll"
#endif
;
#endif

static struct GaDrvFunctions
{
    PFNGaDrvScreenCreate  pfnGaDrvScreenCreate;
    PFNGaDrvScreenDestroy pfnGaDrvScreenDestroy;
    PFNGaDrvGetWDDMEnv    pfnGaDrvGetWDDMEnv;
    PFNGaDrvGetContextId  pfnGaDrvGetContextId;
    PFNGaDrvGetSurfaceId  pfnGaDrvGetSurfaceId;
    PFNGaDrvContextFlush  pfnGaDrvContextFlush;
} g_drvfuncs;


#if VBOX_MESA_V_MAJOR < 24
static HMODULE gaDrvLoadSVGA(struct GaDrvFunctions *pDrvFuncs)
{
    struct VBOXWDDMDLLPROC aDrvProcs[] =
    {
        { "GaDrvScreenCreate",  (FARPROC *)&pDrvFuncs->pfnGaDrvScreenCreate },
        { "GaDrvScreenDestroy", (FARPROC *)&pDrvFuncs->pfnGaDrvScreenDestroy },
        { "GaDrvGetWDDMEnv",    (FARPROC *)&pDrvFuncs->pfnGaDrvGetWDDMEnv },
        { "GaDrvGetContextId",  (FARPROC *)&pDrvFuncs->pfnGaDrvGetContextId },
        { "GaDrvGetSurfaceId",  (FARPROC *)&pDrvFuncs->pfnGaDrvGetSurfaceId },
        { "GaDrvContextFlush",  (FARPROC *)&pDrvFuncs->pfnGaDrvContextFlush },
        { NULL, NULL }
    };

    HMODULE hmod = VBoxWddmLoadSystemDll(g_pszSvgaDll);
    if (hmod)
    {
        VBoxWddmLoadAdresses(hmod, aDrvProcs);
    }
    return hmod;
}
#else
struct pipe_screen * WINAPI GaDrvScreenCreate(const WDDMGalliumDriverEnv *pEnv);
void WINAPI GaDrvScreenDestroy(struct pipe_screen *s);
uint32_t WINAPI GaDrvGetSurfaceId(struct pipe_screen *pScreen, struct pipe_resource *pResource);
const WDDMGalliumDriverEnv *WINAPI GaDrvGetWDDMEnv(struct pipe_screen *pScreen);
uint32_t WINAPI GaDrvGetContextId(struct pipe_context *pPipeContext);
void WINAPI GaDrvContextFlush(struct pipe_context *pPipeContext);

static void initDrvFuncs(void)
{
    g_drvfuncs.pfnGaDrvScreenCreate = GaDrvScreenCreate;
    g_drvfuncs.pfnGaDrvScreenDestroy = GaDrvScreenDestroy;
    g_drvfuncs.pfnGaDrvGetWDDMEnv = GaDrvGetWDDMEnv;
    g_drvfuncs.pfnGaDrvGetContextId = GaDrvGetContextId;
    g_drvfuncs.pfnGaDrvGetSurfaceId = GaDrvGetSurfaceId;
    g_drvfuncs.pfnGaDrvContextFlush = GaDrvContextFlush;
}
#endif

struct stw_shared_surface
{
    D3DKMT_HANDLE hResource;
    D3DKMT_HANDLE hSurface;
    uint32_t u32Sid;
};

static NTSTATUS vboxKmtPresent(D3DKMT_HANDLE hContext, HWND hwnd, D3DKMT_HANDLE hSource, LONG lWidth, LONG lHeight)
{
    RECT r;
    r.left  = 0;
    r.top    = 0;
    r.right  = lWidth;
    r.bottom = lHeight;

    D3DKMT_PRESENT PresentData;
    memset(&PresentData, 0, sizeof(PresentData));
    PresentData.hContext           = hContext;
    PresentData.hWindow            = hwnd;
    PresentData.hSource            = hSource;
    PresentData.hDestination       = 0;
    PresentData.Flags.Blt          = 1;
    PresentData.Flags.SrcRectValid = 1;
    PresentData.Flags.DstRectValid = 1;
    PresentData.SrcRect            = r;
    PresentData.SubRectCnt         = 1;
    PresentData.pSrcSubRects       = &r;
    PresentData.DstRect            = r;

    D3DKMTFUNCTIONS const *d3dkmt = D3DKMTFunctions();
    NTSTATUS Status = d3dkmt->pfnD3DKMTPresent(&PresentData);
    return Status;
}

NTSTATUS vboxKmtOpenSharedSurface(D3DKMT_HANDLE hAdapter, D3DKMT_HANDLE hDevice, D3DKMT_HANDLE hSharedSurface, struct stw_shared_surface *pSurf)
{
    D3DKMTFUNCTIONS const *d3dkmt = D3DKMTFunctions();

    D3DKMT_QUERYRESOURCEINFO QueryResourceInfoData;
    memset(&QueryResourceInfoData, 0, sizeof(QueryResourceInfoData));
    QueryResourceInfoData.hDevice = hDevice;
    QueryResourceInfoData.hGlobalShare = hSharedSurface;

    NTSTATUS Status = d3dkmt->pfnD3DKMTQueryResourceInfo(&QueryResourceInfoData);
    if (Status == STATUS_SUCCESS)
    {
        D3DDDI_OPENALLOCATIONINFO OpenAllocationInfoData;
        memset(&OpenAllocationInfoData, 0, sizeof(OpenAllocationInfoData));

        D3DKMT_OPENRESOURCE OpenResourceData;
        memset(&OpenResourceData, 0, sizeof(OpenResourceData));
        OpenResourceData.hDevice = hDevice;
        OpenResourceData.hGlobalShare = hSharedSurface;
        OpenResourceData.NumAllocations = 1;
        OpenResourceData.pOpenAllocationInfo = &OpenAllocationInfoData;
        if (QueryResourceInfoData.PrivateRuntimeDataSize)
        {
            OpenResourceData.pPrivateRuntimeData = malloc(QueryResourceInfoData.PrivateRuntimeDataSize);
            if (OpenResourceData.pPrivateRuntimeData == NULL)
            {
                Status = STATUS_NOT_SUPPORTED;
            }
            OpenResourceData.PrivateRuntimeDataSize = QueryResourceInfoData.PrivateRuntimeDataSize;
        }
        if (QueryResourceInfoData.ResourcePrivateDriverDataSize)
        {
            OpenResourceData.pResourcePrivateDriverData = malloc(QueryResourceInfoData.ResourcePrivateDriverDataSize);
            if (OpenResourceData.pResourcePrivateDriverData == NULL)
            {
                Status = STATUS_NOT_SUPPORTED;
            }
            OpenResourceData.ResourcePrivateDriverDataSize = QueryResourceInfoData.ResourcePrivateDriverDataSize;
        }
        if (QueryResourceInfoData.TotalPrivateDriverDataSize)
        {
            OpenResourceData.pTotalPrivateDriverDataBuffer = malloc(QueryResourceInfoData.TotalPrivateDriverDataSize);
            if (OpenResourceData.pTotalPrivateDriverDataBuffer == NULL)
            {
                Status = STATUS_NOT_SUPPORTED;
            }
            OpenResourceData.TotalPrivateDriverDataBufferSize = QueryResourceInfoData.TotalPrivateDriverDataSize;
        }

        if (Status == STATUS_SUCCESS)
        {
            Status = d3dkmt->pfnD3DKMTOpenResource(&OpenResourceData);
            if (Status == STATUS_SUCCESS)
            {
                if (OpenAllocationInfoData.PrivateDriverDataSize == sizeof(VBOXWDDM_ALLOCINFO))
                {
                    VBOXWDDM_ALLOCINFO *pVBoxAllocInfo = (VBOXWDDM_ALLOCINFO *)OpenAllocationInfoData.pPrivateDriverData;
                    pSurf->hResource = OpenResourceData.hResource;
                    pSurf->hSurface = OpenAllocationInfoData.hAllocation;
                    pSurf->u32Sid = pVBoxAllocInfo->hostID;
                }
                else if (OpenAllocationInfoData.PrivateDriverDataSize == sizeof(VBOXDXALLOCATIONDESC))
                {
                    //VBOXDXALLOCATIONDESC *pAllocDesc = (VBOXDXALLOCATIONDESC *)OpenAllocationInfoData.PrivateDriverDataSize;
                    pSurf->hResource = OpenResourceData.hResource;
                    pSurf->hSurface = OpenAllocationInfoData.hAllocation;

                    VBOXDISPIFESCAPE_SVGAGETSID data;
                    memset(&data, 0, sizeof(data));
                    data.EscapeHdr.escapeCode = VBOXESC_SVGAGETSID;
                    data.hAllocation = OpenAllocationInfoData.hAllocation;
                    // data.u32Sid = 0;

                    D3DKMT_ESCAPE EscapeData;
                    memset(&EscapeData, 0, sizeof(EscapeData));
                    EscapeData.hAdapter              = hAdapter;
                    EscapeData.hDevice               = hDevice;
                    EscapeData.Type                  = D3DKMT_ESCAPE_DRIVERPRIVATE;
                    // EscapeData.Flags.HardwareAccess  = 0;
                    EscapeData.pPrivateDriverData    = &data;
                    EscapeData.PrivateDriverDataSize = sizeof(data);
                    // EscapeData.hContext              = 0;
                    Status = d3dkmt->pfnD3DKMTEscape(&EscapeData);
                    if (Status == STATUS_SUCCESS)
                        pSurf->u32Sid = data.u32Sid;
                    else
                        Assert(0);
                }
                else
                    Assert(0);
            }
        }

        if (OpenResourceData.pPrivateRuntimeData != NULL)
        {
            free(OpenResourceData.pPrivateRuntimeData);
        }
        if (OpenResourceData.pResourcePrivateDriverData != NULL)
        {
            free(OpenResourceData.pResourcePrivateDriverData);
        }
        if (OpenResourceData.pTotalPrivateDriverDataBuffer != NULL)
        {
            free(OpenResourceData.pTotalPrivateDriverDataBuffer);
        }
    }

    return Status;
}

NTSTATUS vboxKmtCloseSharedSurface(D3DKMT_HANDLE hDevice, struct stw_shared_surface *pSurf)
{
    D3DKMTFUNCTIONS const *d3dkmt = D3DKMTFunctions();

    D3DKMT_DESTROYALLOCATION DestroyAllocationData;
    memset(&DestroyAllocationData, 0, sizeof(DestroyAllocationData));
    DestroyAllocationData.hDevice   = hDevice;
    DestroyAllocationData.hResource = pSurf->hResource;
    /* "If the OpenGL ICD sets the handle in the hResource member to a non-NULL value,
     * the ICD must set phAllocationList to NULL." and
     * "the AllocationCount member is ignored by the OpenGL runtime."
     */
    // DestroyAllocationData.phAllocationList = NULL;
    // DestroyAllocationData.AllocationCount  = 0;

    NTSTATUS Status = d3dkmt->pfnD3DKMTDestroyAllocation(&DestroyAllocationData);
    Assert(Status == STATUS_SUCCESS);
    return Status;
}


static struct pipe_screen *
wddm_screen_create(HDC hDC)
{
    RT_NOREF(hDC); /** @todo Use it? */
    struct pipe_screen *screen = NULL;

#if VBOX_MESA_V_MAJOR < 24
    if (gaDrvLoadSVGA(&g_drvfuncs))
#else
    initDrvFuncs();
#endif
    {
        WDDMGalliumDriverEnv const *pEnv = GaDrvEnvKmtCreate();
        if (pEnv)
        {
            /// @todo pEnv to include destructor callback, to be called from winsys screen destructor?
            screen = g_drvfuncs.pfnGaDrvScreenCreate(pEnv);
        }
    }

    return screen;
}

static void
wddm_present(struct pipe_screen *screen,
             struct pipe_context *context,
             struct pipe_resource *res,
             HDC hDC)
{
    RT_NOREF(context);
    struct stw_context *ctx = stw_current_context();
    struct pipe_context *pipe = ctx->st->pipe;

    const WDDMGalliumDriverEnv *pEnv = g_drvfuncs.pfnGaDrvGetWDDMEnv(screen);
    if (pEnv)
    {
        /* Get context and kernel-mode handle of the resource. */
        uint32_t u32Cid = g_drvfuncs.pfnGaDrvGetContextId(pipe);
        D3DKMT_HANDLE hContext = GaDrvEnvKmtContextHandle(pEnv, u32Cid);

        uint32_t u32SourceSid = g_drvfuncs.pfnGaDrvGetSurfaceId(screen, res);
        D3DKMT_HANDLE hSource = GaDrvEnvKmtSurfaceHandle(pEnv, u32SourceSid);

        HWND hwnd = WindowFromDC(hDC);

        vboxKmtPresent(hContext, hwnd, hSource, res->width0, res->height0);
    }
}

#if VBOX_MESA_V_MAJOR < 24
static boolean
#else
static bool
#endif
wddm_get_adapter_luid(struct pipe_screen *screen,
                      HDC hDC,
                      LUID *pAdapterLuid)
{
    RT_NOREF(hDC); /** @todo Use it? */
    const WDDMGalliumDriverEnv *pEnv = g_drvfuncs.pfnGaDrvGetWDDMEnv(screen);
    if (pEnv)
    {
        GaDrvEnvKmtAdapterLUID(pEnv, pAdapterLuid);
        return true;
    }

    return false;
}

static struct stw_shared_surface *
wddm_shared_surface_open(struct pipe_screen *screen,
                         HANDLE hSharedSurface)
{
    struct stw_shared_surface *surface = NULL;

    const WDDMGalliumDriverEnv *pEnv = g_drvfuncs.pfnGaDrvGetWDDMEnv(screen);
    if (pEnv)
    {
        surface = (struct stw_shared_surface *)malloc(sizeof(struct stw_shared_surface));
        if (surface)
        {
            D3DKMT_HANDLE hAdapter = GaDrvEnvKmtAdapterHandle(pEnv);
            D3DKMT_HANDLE hDevice = GaDrvEnvKmtDeviceHandle(pEnv);
            NTSTATUS Status = vboxKmtOpenSharedSurface(hAdapter, hDevice, (D3DKMT_HANDLE)(uintptr_t)hSharedSurface, surface);
            if (Status != STATUS_SUCCESS)
            {
                free(surface);
                surface = NULL;
            }
        }
    }
    return surface;
}

static void
wddm_shared_surface_close(struct pipe_screen *screen,
                         struct stw_shared_surface *surface)
{
    const WDDMGalliumDriverEnv *pEnv = g_drvfuncs.pfnGaDrvGetWDDMEnv(screen);
    if (pEnv)
    {
        D3DKMT_HANDLE hDevice = GaDrvEnvKmtDeviceHandle(pEnv);
        vboxKmtCloseSharedSurface(hDevice, surface);
    }
    free(surface);
}

static void
wddm_compose(struct pipe_screen *screen,
             struct pipe_resource *res,
             struct stw_shared_surface *dest,
             LPCRECT pRect,
             ULONGLONG PresentHistoryToken)
{
    struct stw_context *ctx = stw_current_context();
    struct pipe_context *pipe = ctx->st->pipe;

    /* The ICD asked to present something, make sure that any outstanding commends are submitted. */
    g_drvfuncs.pfnGaDrvContextFlush(pipe);

    uint32_t u32SourceSid = g_drvfuncs.pfnGaDrvGetSurfaceId(screen, res);

    /* Generate SVGA_3D_CMD_SURFACE_COPY command for these resources. */
    struct
    {
        SVGA3dCmdHeader header;
        SVGA3dCmdSurfaceCopy surfaceCopy;
        SVGA3dCopyBox box;
    } command;

    command.header.id   = SVGA_3D_CMD_SURFACE_COPY;
    command.header.size = sizeof(command) - sizeof(SVGA3dCmdHeader);

    command.surfaceCopy.src.sid     = u32SourceSid;
    command.surfaceCopy.src.face    = 0;
    command.surfaceCopy.src.mipmap  = 0;
    command.surfaceCopy.dest.sid    = dest->u32Sid;
    command.surfaceCopy.dest.face   = 0;
    command.surfaceCopy.dest.mipmap = 0;

    command.box.x    = pRect->left;
    command.box.y    = pRect->top;
    command.box.z    = 0;
    command.box.w    = pRect->right - pRect->left;
    command.box.h    = pRect->bottom - pRect->top;
    command.box.d    = 1;
    command.box.srcx = 0;
    command.box.srcy = 0;
    command.box.srcz = 0;

    const WDDMGalliumDriverEnv *pEnv = g_drvfuncs.pfnGaDrvGetWDDMEnv(screen);
    if (pEnv)
    {
        uint32_t u32Cid = g_drvfuncs.pfnGaDrvGetContextId(pipe);
        GaDrvEnvKmtRenderCompose(pEnv, u32Cid, &command, sizeof(command), PresentHistoryToken);
    }
}

static unsigned
wddm_get_pfd_flags(struct pipe_screen *screen)
{
    (void)screen;
    return stw_pfd_gdi_support | stw_pfd_double_buffer;
}

static const char *
wddm_get_name(void)
{
   return "VBoxGL";
}

static const struct stw_winsys stw_winsys = {
   wddm_screen_create,
   wddm_present,
   wddm_get_adapter_luid,
   wddm_shared_surface_open,
   wddm_shared_surface_close,
   wddm_compose,
   wddm_get_pfd_flags,
   NULL, /* create_framebuffer */
   wddm_get_name,
};

#ifdef DEBUG
typedef BOOL WINAPI FNGetModuleInformation(HANDLE hProcess, HMODULE hModule, LPMODULEINFO lpmodinfo, DWORD cb);
typedef FNGetModuleInformation *PFNGetModuleInformation;

static PFNGetModuleInformation g_pfnGetModuleInformation = NULL;
static HMODULE g_hModPsapi = NULL;
static PVOID g_VBoxWDbgVEHandler = NULL;

static bool vboxVDbgIsAddressInModule(PVOID pv, const char *pszModuleName)
{
    HMODULE hMod = GetModuleHandleA(pszModuleName);
    if (!hMod)
        return false;

    if (!g_pfnGetModuleInformation)
        return false;

    HANDLE hProcess = GetCurrentProcess();
    MODULEINFO ModuleInfo = {0};
    if (!g_pfnGetModuleInformation(hProcess, hMod, &ModuleInfo, sizeof(ModuleInfo)))
        return false;

    return    (uintptr_t)ModuleInfo.lpBaseOfDll <= (uintptr_t)pv
           && (uintptr_t)pv < (uintptr_t)ModuleInfo.lpBaseOfDll + ModuleInfo.SizeOfImage;
}

static bool vboxVDbgIsExceptionIgnored(PEXCEPTION_RECORD pExceptionRecord)
{
    /* Module (dll) names for GetModuleHandle.
     * Exceptions originated from these modules will be ignored.
     */
    static const char *apszIgnoredModuleNames[] =
    {
        NULL
    };

    int i = 0;
    while (apszIgnoredModuleNames[i])
    {
        if (vboxVDbgIsAddressInModule(pExceptionRecord->ExceptionAddress, apszIgnoredModuleNames[i]))
            return true;

        ++i;
    }

    return false;
}

static LONG WINAPI vboxVDbgVectoredHandler(struct _EXCEPTION_POINTERS *pExceptionInfo) RT_NOTHROW_DEF
{
    static volatile bool g_fAllowIgnore = true; /* Might be changed in kernel debugger. */

    PEXCEPTION_RECORD pExceptionRecord = pExceptionInfo->ExceptionRecord;
    /* PCONTEXT pContextRecord = pExceptionInfo->ContextRecord; */

    switch (pExceptionRecord->ExceptionCode)
    {
        default:
            break;
        case EXCEPTION_BREAKPOINT:
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_INVALID_OPERATION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            if (g_fAllowIgnore && vboxVDbgIsExceptionIgnored(pExceptionRecord))
                break;
            ASMBreakpoint();
            break;
        case 0x40010006: /* OutputDebugStringA? */
        case 0x4001000a: /* OutputDebugStringW? */
            break;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void vboxVDbgVEHandlerRegister(void)
{
    Assert(!g_VBoxWDbgVEHandler);
    g_VBoxWDbgVEHandler = AddVectoredExceptionHandler(1, vboxVDbgVectoredHandler);
    Assert(g_VBoxWDbgVEHandler);

    g_hModPsapi = GetModuleHandleA("Psapi.dll"); /* Usually already loaded. */
    if (g_hModPsapi)
        g_pfnGetModuleInformation = (PFNGetModuleInformation)GetProcAddress(g_hModPsapi, "GetModuleInformation");
}

static void vboxVDbgVEHandlerUnregister(void)
{
    Assert(g_VBoxWDbgVEHandler);
    ULONG uResult = RemoveVectoredExceptionHandler(g_VBoxWDbgVEHandler);
    Assert(uResult); RT_NOREF(uResult);
    g_VBoxWDbgVEHandler = NULL;

    g_hModPsapi = NULL;
    g_pfnGetModuleInformation = NULL;
}
#endif /* DEBUG */

BOOL WINAPI DllMain(HINSTANCE hDLLInst,
                    DWORD fdwReason,
                    LPVOID lpvReserved)
{
    RT_NOREF2(hDLLInst, lpvReserved);

    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
#ifdef DEBUG
            vboxVDbgVEHandlerRegister();
#endif
            RTR3InitDll(RTR3INIT_FLAGS_UNOBTRUSIVE);
            D3DKMTLoad();
            stw_init(&stw_winsys);
            stw_init_thread();
            break;

        case DLL_PROCESS_DETACH:
#ifdef DEBUG
            vboxVDbgVEHandlerUnregister();
#endif
            break;

        case DLL_THREAD_ATTACH:
            stw_init_thread();
            break;

        case DLL_THREAD_DETACH:
            stw_cleanup_thread();
            break;

        default:
            if (lpvReserved == NULL)
            {
               // We're being unloaded from the process.
               stw_cleanup_thread();
               stw_cleanup();
            }
            else
            {
               // Process itself is terminating, and all threads and modules are
               // being detached.
               //
               // The order threads (including llvmpipe rasterizer threads) are
               // destroyed can not be relied up, so it's not safe to cleanup.
               //
               // However global destructors (e.g., LLVM's) will still be called, and
               // if Microsoft OPENGL32.DLL's DllMain is called after us, it will
               // still try to invoke DrvDeleteContext to destroys all outstanding,
               // so set stw_dev to NULL to return immediately if that happens.
               stw_dev = NULL;
            }
            break;
    }

    return TRUE;
}
