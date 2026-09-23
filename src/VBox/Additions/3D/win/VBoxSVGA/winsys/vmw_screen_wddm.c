/*
 * Copyright (C) 2016-2026 Oracle and/or its affiliates.
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

/**********************************************************
 * Copyright 2009-2015 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/


#include "util/compiler.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"

#include "vmw_context.h"
#include "vmw_screen.h"
#include "vmw_surface.h"
#include "vmw_buffer.h"
#include "svga_drm_public.h"
#include "svga3d_surfacedefs.h"

#include "frontend/drm_driver.h"

#include "vmwgfx_drm.h"

#include "../wddm_screen.h"

#include <iprt/asm.h>

static struct svga_winsys_surface *
vmw_drm_surface_from_handle(struct svga_winsys_screen *sws,
			    struct winsys_handle *whandle,
			    SVGA3dSurfaceFormat *format);

static struct svga_winsys_surface *
vmw_drm_gb_surface_from_handle(struct svga_winsys_screen *sws,
                               struct winsys_handle *whandle,
                               SVGA3dSurfaceFormat *format);
static bool
vmw_drm_surface_get_handle(struct svga_winsys_screen *sws,
			   struct svga_winsys_surface *surface,
			   unsigned stride,
			   struct winsys_handle *whandle);

struct vmw_winsys_screen_wddm *
vmw_winsys_create_wddm(const WDDMGalliumDriverEnv *pEnv);

/* This is actually the entrypoint to the entire driver,
 * called by the target bootstrap code.
 */
struct svga_winsys_screen *
svga_wddm_winsys_screen_create(const WDDMGalliumDriverEnv *pEnv)
{
   struct vmw_winsys_screen_wddm *vws;

   if (pEnv->cb < sizeof(WDDMGalliumDriverEnv))
      goto out_no_vws;

   vws = vmw_winsys_create_wddm(pEnv);
   if (!vws)
      goto out_no_vws;

   /* XXX do this properly */
   vws->base.base.surface_from_handle = vws->base.base.have_gb_objects ?
      vmw_drm_gb_surface_from_handle : vmw_drm_surface_from_handle;
   vws->base.base.surface_get_handle = vmw_drm_surface_get_handle;

   return &vws->base.base;

out_no_vws:
   return NULL;
}

static struct svga_winsys_surface *
vmw_drm_gb_surface_from_handle(struct svga_winsys_screen *sws,
                               struct winsys_handle *whandle,
                               SVGA3dSurfaceFormat *format)
{
    RT_NOREF3(sws, whandle, format);
    AssertFailed();
    return 0;
}

static struct svga_winsys_surface *
vmw_drm_surface_from_handle(struct svga_winsys_screen *sws,
			    struct winsys_handle *whandle,
			    SVGA3dSurfaceFormat *format)
{
    RT_NOREF3(sws, whandle, format);
    AssertFailed();
    return 0;
}

/*
 * The user mode driver asks the kernel driver to create a resource
 * (vmw_ioctl_surface_create) and gets a sid (surface id).
 * This function is supposed to convert the sid to a handle (file descriptor)
 * which can be used to access the surface.
 */
static bool
vmw_drm_surface_get_handle(struct svga_winsys_screen *sws,
			   struct svga_winsys_surface *surface,
			   unsigned stride,
			   struct winsys_handle *whandle)
{
    struct vmw_winsys_screen *vws = vmw_winsys_screen(sws);
    struct vmw_svga_winsys_surface *vsrf;

    RT_NOREF(vws);

    if (!surface)
	return false;

    vsrf = vmw_svga_winsys_surface(surface);
    whandle->handle = (HANDLE)(uintptr_t)vsrf->sid;
    whandle->stride = stride;
    whandle->offset = 0;

    switch (whandle->type) {
    case WINSYS_HANDLE_TYPE_SHARED:
    case WINSYS_HANDLE_TYPE_KMS:
       break;
    case WINSYS_HANDLE_TYPE_FD:
       break;
    default:
       vmw_error("Attempt to export unsupported handle type %d.\n",
		 whandle->type);
       return false;
    }

    return true;
}

void
vmw_svga_winsys_host_log(struct svga_winsys_screen *sws, const char *log)
{
   (void)sws;
   _debug_printf("%s\n", log);
}
