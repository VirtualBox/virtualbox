/* $Id: VBoxNine.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - Direct3D9 state tracker.
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

#include <iprt/win/windows.h>
#include <iprt/win/d3d9.h>
//#include <d3dumddi.h>

#if VBOX_MESA_V_MAJOR >= 24
#include <VBoxGaDriver.h>
#endif
#include <VBoxWddmUmHlp.h>

//#include <windef.h>
//#include <winbase.h>
//#include <winsvc.h>
//#include <winnetwk.h>
//#include <npapi.h>
//#include <devioctl.h>
//#include <stdio.h>

//#include <iprt/alloc.h>
#include <iprt/initterm.h>
//#include <iprt/string.h>
//#include <iprt/log.h>
//#include <VBox/version.h>
//#include <VBox/VMMDev.h>
//#include <VBox/Log.h>

#include "adapter9.h"
#include "surface9.h"
#include "pipe/p_screen.h"

#include <iprt/types.h>
// #include <iprt/asm.h>

// #include "VBoxNine.h"

#if VBOX_MESA_V_MAJOR >= 24
struct pipe_screen * WINAPI GaDrvScreenCreate(const WDDMGalliumDriverEnv *pEnv);
void WINAPI GaDrvScreenDestroy(struct pipe_screen *s);
uint32_t WINAPI GaDrvGetSurfaceId(struct pipe_screen *pScreen, struct pipe_resource *pResource);
const WDDMGalliumDriverEnv *WINAPI GaDrvGetWDDMEnv(struct pipe_screen *pScreen);
uint32_t WINAPI GaDrvGetContextId(struct pipe_context *pPipeContext);
void WINAPI GaDrvContextFlush(struct pipe_context *pPipeContext);
#endif

struct d3dadapter9_context_wddm
{
    struct d3dadapter9_context base;
    void *reserved;
};

static void
wddm_destroy(struct d3dadapter9_context *ctx)
{
    /* struct d3dadapter9_context_wddm *ctx_wddm = (struct d3dadapter9_context_wddm *)ctx; */

/// @todo screen (hal) is deleted by the upper level. Do not delete here.
//    if (ctx->ref)
//        ctx->ref->destroy(ctx->ref);
//    /* because ref is a wrapper around hal, freeing ref frees hal too. */
//    else if (ctx->hal)
//        ctx->hal->destroy(ctx->hal);

    FREE(ctx);
}

static HRESULT
d3dadapter9_context_wddm_create(struct d3dadapter9_context_wddm **ppCtx, struct pipe_screen *s)
{
    struct d3dadapter9_context_wddm *ctx = CALLOC_STRUCT(d3dadapter9_context_wddm);

    if (!ctx) { return E_OUTOFMEMORY; }

    ctx->base.hal                        = s;
    /** @todo Need software device here. Currently assigned to hw device to prevent NineDevice9_ctor crash. */
    ctx->base.ref                        = ctx->base.hal;
    // D3DADAPTER_IDENTIFIER9 identifier;
    ctx->base.linear_framebuffer         = TRUE;
    ctx->base.throttling                 = FALSE;
    ctx->base.throttling_value           = 0;
    ctx->base.vblank_mode                = 1;
    ctx->base.thread_submit              = FALSE;
    ctx->base.discard_delayed_release    = FALSE;
    ctx->base.tearfree_discard           = FALSE;
    ctx->base.csmt_force                 = FALSE;
    ctx->base.dynamic_texture_workaround = FALSE;
    ctx->base.shader_inline_constants    = FALSE;
    ctx->base.memfd_virtualsizelimit     = -1;
    ctx->base.override_vram_size         = -1;
    ctx->base.destroy                    = wddm_destroy;


    /* read out PCI info */
    /// @todo read_descriptor(&ctx->base, fd);

    *ppCtx = ctx;
    return D3D_OK;
}

#if VBOX_MESA_V_MAJOR < 24
HRESULT WINAPI
GaNineD3DAdapter9Create(struct pipe_screen *s, ID3DAdapter9 **ppOut)
{
    HRESULT hr;
    struct d3dadapter9_context_wddm *pCtx = NULL;
    hr = d3dadapter9_context_wddm_create(&pCtx, s);
    if (SUCCEEDED(hr))
    {
        hr = NineAdapter9_new(&pCtx->base, (struct NineAdapter9 **)ppOut);
        if (FAILED(hr))
        {
            /// @todo NineAdapter9_new calls this as ctx->base.destroy,
            //       and will not call if memory allocation fails.
            // wddm_destroy(&pCtx->base);
        }
    }
    return hr;
}

struct pipe_resource * WINAPI
GaNinePipeResourceFromSurface(IUnknown *pSurface)
{
    /// @todo QueryInterface?
    struct NineResource9 *pResource = (struct NineResource9 *)pSurface;
    return pResource->resource;
}

extern struct pipe_context *
NineDevice9_GetPipe( struct NineDevice9 *This );

struct pipe_context * WINAPI
GaNinePipeContextFromDevice(IDirect3DDevice9 *pDevice)
{
    /// @todo Verify that this is a NineDevice?
    struct pipe_context *pPipeContext = NineDevice9_GetPipe((struct NineDevice9 *)pDevice);
    return pPipeContext;
}

#else /* VBOX_MESA_V_MAJOR >= 24 */

HRESULT WINAPI
GaNineD3DAdapter9Create(const WDDMGalliumDriverEnv *pEnv, ID3DAdapter9 **ppOut)
{
    HRESULT hr;
    struct pipe_screen *s = GaDrvScreenCreate(pEnv);
    if (s)
    {
        struct d3dadapter9_context_wddm *ctx = NULL;
        hr = d3dadapter9_context_wddm_create(&ctx, s);
        if (SUCCEEDED(hr))
        {
            hr = NineAdapter9_new(&ctx->base, (struct NineAdapter9 **)ppOut);
            if (FAILED(hr))
            {
                /// @todo NineAdapter9_new calls this as ctx->base.destroy,
                //       and will not call if memory allocation fails.
                // wddm_destroy(&pCtx->base);
            }
        }
        Assert(SUCCEEDED(hr));
    }
    else
    {
        hr = E_FAIL;
        AssertFailed();
    }
    return hr;
}

uint32_t WINAPI
GaNineGetSurfaceId(IUnknown *pSurface)
{
    uint32_t sid = SVGA_ID_INVALID;

    /// @todo QueryInterface?
    struct NineResource9 *pNineResource = (struct NineResource9 *)pSurface;
    struct pipe_resource *r = pNineResource->resource;
    if (r)
    {
        struct pipe_screen *s = r->screen;
        sid = GaDrvGetSurfaceId(s, r);
    }

    return sid;
}

extern struct pipe_context *
NineDevice9_GetPipe( struct NineDevice9 *This );

uint32_t WINAPI
GaNineGetContextId(IDirect3DDevice9 *pDevice)
{
    uint32_t cid = SVGA_ID_INVALID;

    /// @todo Verify that this is a NineDevice?
    struct pipe_context *pipe = NineDevice9_GetPipe((struct NineDevice9 *)pDevice);
    if (pipe)
        cid = GaDrvGetContextId(pipe);

    return cid;
}

void WINAPI
GaNineFlush(IDirect3DDevice9 *pDevice)
{
    struct pipe_context *pipe = NineDevice9_GetPipe((struct NineDevice9 *)pDevice);
    if (pipe)
        GaDrvContextFlush(pipe);
}
#endif /* VBOX_MESA_V_MAJOR >= 24 */

BOOL WINAPI DllMain(HINSTANCE hDLLInst,
                    DWORD fdwReason,
                    LPVOID lpvReserved)
{
    BOOL fReturn = TRUE;

    RT_NOREF2(hDLLInst, lpvReserved);

    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            RTR3InitDll(RTR3INIT_FLAGS_UNOBTRUSIVE);
            D3DKMTLoad();
            break;

        case DLL_PROCESS_DETACH:
            /// @todo RTR3Term();
            break;

        case DLL_THREAD_ATTACH:
            break;

        case DLL_THREAD_DETACH:
            break;

        default:
            break;
    }

    return fReturn;
}
