/* $Id: VBoxSVGA.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - VMSVGA hardware driver.
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

#include <VBoxWddmUmHlp.h>

#include <iprt/initterm.h>

#include "svga_public.h"
#include "svga_screen.h"
#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "frontend/drm_driver.h"

#include "wddm_screen.h"

struct svga_winsys_screen *
svga_wddm_winsys_screen_create(const WDDMGalliumDriverEnv *pEnv);

struct pipe_screen * WINAPI
GaDrvScreenCreate(const WDDMGalliumDriverEnv *pEnv)
{
    struct svga_winsys_screen *sws = svga_wddm_winsys_screen_create(pEnv);
    if (sws)
        return svga_screen_create(sws);
    return NULL;
}

void WINAPI
GaDrvScreenDestroy(struct pipe_screen *s)
{
    if (s)
        s->destroy(s);
}

uint32_t WINAPI
GaDrvGetSurfaceId(struct pipe_screen *pScreen, struct pipe_resource *pResource)
{
    uint32_t u32Sid = 0;

    if (   pScreen
        && pResource)
    {
        /* Get the sid (surface id). */
        struct winsys_handle whandle;
        memset(&whandle, 0, sizeof(whandle));
        whandle.type = WINSYS_HANDLE_TYPE_SHARED;

        if (pScreen->resource_get_handle(pScreen, NULL, pResource, &whandle, 0))
        {
            u32Sid = (uint32_t)(uintptr_t)whandle.handle;
        }
    }

    return u32Sid;
}

const WDDMGalliumDriverEnv *WINAPI
GaDrvGetWDDMEnv(struct pipe_screen *pScreen)
{
    HANDLE hAdapter = NULL;

    if (pScreen)
    {
        struct svga_screen *pSvgaScreen = svga_screen(pScreen);
        struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)pSvgaScreen->sws;

        return vws_wddm->pEnv;
    }

    return hAdapter;
}

uint32_t WINAPI
GaDrvGetContextId(struct pipe_context *pPipeContext)
{
    uint32 u32Cid = ~0;

    if (pPipeContext)
    {
        struct svga_winsys_context *pSWC = svga_winsys_context(pPipeContext);
        u32Cid = pSWC->cid;
    }

    return u32Cid;
}

void WINAPI
GaDrvContextFlush(struct pipe_context *pPipeContext)
{
    if (pPipeContext)
        pPipeContext->flush(pPipeContext, NULL, PIPE_FLUSH_END_OF_FRAME);
}

#ifndef VBOX_MESA_STATIC_DRIVER
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
#endif
