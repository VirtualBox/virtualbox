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

/**
 * @file
 *
 * Wrappers for DRM ioctl functionlaity used by the rest of the vmw
 * drm winsys.
 *
 * Based on svgaicd_escape.c
 */


#include "svga_cmd.h"
#include "util/u_memory.h"
#include "util/u_math.h"
#include "svgadump/svga_dump.h"
#include "frontend/drm_driver.h"
#include "vmw_screen.h"
#include "vmw_context.h"
#include "vmw_fence.h"
#include "vmwgfx_drm.h"
#include "svga3d_caps.h"
#include "svga3d_reg.h"
#include "svga3d_surfacedefs.h"

#include "../wddm_screen.h"
#include <iprt/asm.h>

#define VMW_MAX_DEFAULT_TEXTURE_SIZE   (128 * 1024 * 1024)
#define VMW_FENCE_TIMEOUT_SECONDS 3600UL

#define SVGA3D_FLAGS_64(upper32, lower32) (((uint64_t)upper32 << 32) | lower32)
#define SVGA3D_FLAGS_UPPER_32(svga3d_flags) (svga3d_flags >> 32)
#define SVGA3D_FLAGS_LOWER_32(svga3d_flags) \
   (svga3d_flags & ((uint64_t)UINT32_MAX))

struct vmw_region
{
   uint32_t handle;
   uint64_t map_handle;
   void *data;
   uint32_t map_count;
   struct vmw_winsys_screen_wddm *vws_wddm;
   uint32_t size;
};

uint32_t
vmw_region_size(struct vmw_region *region)
{
   return region->size;
}

#if defined(__DragonFly__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__OpenBSD__)
#define ERESTART EINTR
#endif

uint32
vmw_ioctl_context_create(struct vmw_winsys_screen *vws)
{
   struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;
   return vws_wddm->pEnv->pfnContextCreate(vws_wddm->pEnv->pvEnv, false, false);
}

uint32
vmw_ioctl_extended_context_create(struct vmw_winsys_screen *vws,
                                  bool vgpu10)
{
   struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;
   return vws_wddm->pEnv->pfnContextCreate(vws_wddm->pEnv->pvEnv, true, vgpu10);
}

void
vmw_ioctl_context_destroy(struct vmw_winsys_screen *vws, uint32 cid)
{
    struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;
    vws_wddm->pEnv->pfnContextDestroy(vws_wddm->pEnv->pvEnv, cid);
}

uint32
vmw_ioctl_surface_create(struct vmw_winsys_screen *vws,
                         SVGA3dSurface1Flags flags,
                         SVGA3dSurfaceFormat format,
                         unsigned usage,
                         SVGA3dSize size,
                         uint32_t numFaces, uint32_t numMipLevels,
                         unsigned sampleCount)
{
   struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;

   GASURFCREATE createParms;
   GASURFSIZE sizes[DRM_VMW_MAX_SURFACE_FACES*
                    DRM_VMW_MAX_MIP_LEVELS];
   GASURFSIZE *cur_size;
   uint32_t iFace;
   uint32_t iMipLevel;
   uint32_t u32Sid;
   int ret;

   RT_NOREF(sampleCount);

   memset(&createParms, 0, sizeof(createParms));
   createParms.flags = (uint32_t) flags;
   createParms.format = (uint32_t) format;
   createParms.usage = (uint32_t) usage;

   if (numFaces * numMipLevels >= GA_MAX_SURFACE_FACES*GA_MAX_MIP_LEVELS) {
      return (uint32_t)-1;
   }
   cur_size = sizes;
   for (iFace = 0; iFace < numFaces; ++iFace) {
      SVGA3dSize mipSize = size;

      createParms.mip_levels[iFace] = numMipLevels;
      for (iMipLevel = 0; iMipLevel < numMipLevels; ++iMipLevel) {
         cur_size->cWidth = mipSize.width;
         cur_size->cHeight = mipSize.height;
         cur_size->cDepth = mipSize.depth;
         cur_size->u32Reserved = 0;
         mipSize.width = MAX2(mipSize.width >> 1, 1);
         mipSize.height = MAX2(mipSize.height >> 1, 1);
         mipSize.depth = MAX2(mipSize.depth >> 1, 1);
         cur_size++;
      }
   }
   for (iFace = numFaces; iFace < SVGA3D_MAX_SURFACE_FACES; ++iFace) {
      createParms.mip_levels[iFace] = 0;
   }

   ret = vws_wddm->pEnv->pfnSurfaceDefine(vws_wddm->pEnv->pvEnv, &createParms, &sizes[0], numFaces * numMipLevels, &u32Sid);
   if (ret) {
      return (uint32_t)-1;
   }

   return u32Sid;
}


uint32
vmw_ioctl_gb_surface_create(struct vmw_winsys_screen *vws,
                            SVGA3dSurfaceAllFlags flags,
                            SVGA3dSurfaceFormat format,
                            unsigned usage,
                            SVGA3dSize size,
                            uint32_t numFaces,
                            uint32_t numMipLevels,
                            unsigned sampleCount,
                            uint32_t buffer_handle,
                            SVGA3dMSPattern multisamplePattern,
                            SVGA3dMSQualityLevel qualityLevel,
                            struct vmw_region **p_region)
{
    struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;

    struct vmw_region *region = NULL;
    if (p_region)
    {
       region = CALLOC_STRUCT(vmw_region);
       if (!region)
          return SVGA3D_INVALID_ID;
    }

    SVGAGBSURFCREATE createParms;
    createParms.s.flags = flags;
    createParms.s.format = format;
    createParms.s.usage = usage;
    createParms.s.size = size;
    createParms.s.numFaces = numFaces;
    createParms.s.numMipLevels = numMipLevels;
    createParms.s.sampleCount = sampleCount;
    createParms.s.multisamplePattern = multisamplePattern;
    createParms.s.qualityLevel = qualityLevel;
    if (buffer_handle)
        createParms.gmrid = buffer_handle;
    else
        createParms.gmrid = SVGA3D_INVALID_ID;
    createParms.u64UserAddress = 0; /* out */
    createParms.u32Sid = 0; /* out */

    createParms.cbGB = svga3dsurface_get_serialized_size(format,
                                                         size,
                                                         numMipLevels,
                                                         numFaces);

    int ret = vws_wddm->pEnv->pfnGBSurfaceDefine(vws_wddm->pEnv->pvEnv, &createParms);
    if (ret)
    {
        FREE(region);
        return SVGA3D_INVALID_ID;
    }

    if (p_region)
    {
        region->handle      = createParms.gmrid;
        region->map_handle  = 0;
        region->data        = (void *)(uintptr_t)createParms.u64UserAddress;
        region->map_count   = 0;
        region->size        = createParms.cbGB;
        region->vws_wddm    = vws_wddm;
        *p_region = region;
    }
    return createParms.u32Sid;
}

/**
 * vmw_ioctl_surface_req - Fill in a struct surface_req
 *
 * @vws: Winsys screen
 * @whandle: Surface handle
 * @req: The struct surface req to fill in
 * @needs_unref: This call takes a kernel surface reference that needs to
 * be unreferenced.
 *
 * Returns 0 on success, negative error type otherwise.
 * Fills in the surface_req structure according to handle type and kernel
 * capabilities.
 */
static int
vmw_ioctl_surface_req(const struct vmw_winsys_screen *vws,
                      const struct winsys_handle *whandle,
                      struct drm_vmw_surface_arg *req,
                      bool *needs_unref)
{
   ASMBreakpoint();
   RT_NOREF4(vws, whandle, req, needs_unref);
   return -1;
}

/**
 * vmw_ioctl_gb_surface_ref - Put a reference on a guest-backed surface and
 * get surface information
 *
 * @vws: Screen to register the reference on
 * @handle: Kernel handle of the guest-backed surface
 * @flags: flags used when the surface was created
 * @format: Format used when the surface was created
 * @numMipLevels: Number of mipmap levels of the surface
 * @p_region: On successful return points to a newly allocated
 * struct vmw_region holding a reference to the surface backup buffer.
 *
 * Returns 0 on success, a system error on failure.
 */
int
vmw_ioctl_gb_surface_ref(struct vmw_winsys_screen *vws,
                         const struct winsys_handle *whandle,
                         SVGA3dSurfaceAllFlags *flags,
                         SVGA3dSurfaceFormat *format,
                         uint32_t *numMipLevels,
                         uint32_t *handle,
                         struct vmw_region **p_region)
{
   ASMBreakpoint();
   RT_NOREF7(vws, whandle, flags, format, numMipLevels, handle, p_region);
   return -1;
}

void
vmw_ioctl_surface_destroy(struct vmw_winsys_screen *vws, uint32 sid)
{
   struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;
   vws_wddm->pEnv->pfnSurfaceDestroy(vws_wddm->pEnv->pvEnv, sid);
}

void
vmw_ioctl_command(struct vmw_winsys_screen *vws, int32_t cid,
                  uint32_t throttle_us, void *commands, uint32_t size,
                  struct pipe_fence_handle **pfence, int32_t imported_fence_fd,
                  uint32_t flags)
{
    struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;
    GAFENCEQUERY FenceQuery;
    RT_NOREF3(throttle_us, imported_fence_fd, flags);
#ifdef DEBUG
   // svga_dump_commands(commands, size);
#endif
    memset(&FenceQuery, 0, sizeof(FenceQuery));
    FenceQuery.u32FenceStatus = GA_FENCE_STATUS_NULL;

    vws_wddm->pEnv->pfnRender(vws_wddm->pEnv->pvEnv, cid, commands, size, pfence? &FenceQuery: NULL);
    if (FenceQuery.u32FenceStatus == GA_FENCE_STATUS_NULL)
    {
       /*
        * Kernel has already synced, or caller requested no fence.
        */
       if (pfence)
          *pfence = NULL;
    }
    else
    {
       if (pfence)
       {
          vmw_fences_signal(vws->fence_ops, FenceQuery.u32ProcessedSeqNo, FenceQuery.u32SubmittedSeqNo, TRUE);

          *pfence = vmw_fence_create(vws->fence_ops, FenceQuery.u32FenceHandle,
                                     FenceQuery.u32SubmittedSeqNo, /* mask */ 0, -1);
          if (*pfence == NULL)
          {
             /*
              * Fence creation failed. Need to sync.
              */
             (void) vmw_ioctl_fence_finish(vws, FenceQuery.u32FenceHandle, /* mask */ 0);
             vmw_ioctl_fence_unref(vws, FenceQuery.u32FenceHandle);
          }
       }
    }
}


struct vmw_region *
vmw_ioctl_region_create(struct vmw_winsys_screen *vws, uint32_t size)
{
   /* 'region' is a buffer visible both for host and guest */
   struct vmw_region *region;
   struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;
   uint32_t u32GmrId = 0;
   void *pvMap = NULL;
   int ret;

   region = CALLOC_STRUCT(vmw_region);
   if (!region)
      goto out_err1;

   ret = vws_wddm->pEnv->pfnRegionCreate(vws_wddm->pEnv->pvEnv, size, &u32GmrId, &pvMap);

   if (ret) {
      vmw_error("IOCTL failed %d: %s\n", ret, strerror(-ret));
      goto out_err1;
   }

   region->handle      = u32GmrId;
   region->map_handle  = 0;
   region->data        = pvMap;
   region->map_count   = 0;
   region->size        = size;
   region->vws_wddm    = vws_wddm;

   return region;

out_err1:
   FREE(region);
   return NULL;
}

void
vmw_ioctl_region_destroy(struct vmw_region *region)
{
   struct vmw_winsys_screen_wddm *vws_wddm = region->vws_wddm;

   vws_wddm->pEnv->pfnRegionDestroy(vws_wddm->pEnv->pvEnv, region->handle, region->data);

   FREE(region);
}

SVGAGuestPtr
vmw_ioctl_region_ptr(struct vmw_region *region)
{
   SVGAGuestPtr ptr = {region->handle, 0};
   return ptr;
}

void *
vmw_ioctl_region_map(struct vmw_region *region)
{
   debug_printf("%s: gmrId = %u\n", __FUNCTION__,
                region->handle);

   if (region->data == NULL)
   {
      /* Should not get here. */
      return NULL;
   }

   ++region->map_count;

   return region->data;
}

void
vmw_ioctl_region_unmap(struct vmw_region *region)
{
   --region->map_count;
}

/**
 * vmw_ioctl_syncforcpu - Synchronize a buffer object for CPU usage
 *
 * @region: Pointer to a struct vmw_region representing the buffer object.
 * @dont_block: Dont wait for GPU idle, but rather return -EBUSY if the
 * GPU is busy with the buffer object.
 * @readonly: Hint that the CPU access is read-only.
 * @allow_cs: Allow concurrent command submission while the buffer is
 * synchronized for CPU. If FALSE command submissions referencing the
 * buffer will block until a corresponding call to vmw_ioctl_releasefromcpu.
 *
 * This function idles any GPU activities touching the buffer and blocks
 * command submission of commands referencing the buffer, even from
 * other processes.
 */
int
vmw_ioctl_syncforcpu(struct vmw_region *region,
                     bool dont_block,
                     bool readonly,
                     bool allow_cs)
{
    ASMBreakpoint();
    RT_NOREF4(region, dont_block, readonly, allow_cs);
    return -1;
}

/**
 * vmw_ioctl_releasefromcpu - Undo a previous syncforcpu.
 *
 * @region: Pointer to a struct vmw_region representing the buffer object.
 * @readonly: Should hold the same value as the matching syncforcpu call.
 * @allow_cs: Should hold the same value as the matching syncforcpu call.
 */
void
vmw_ioctl_releasefromcpu(struct vmw_region *region,
                         bool readonly,
                         bool allow_cs)
{
   ASMBreakpoint();
   RT_NOREF3(region, readonly, allow_cs);
   return;
}

void
vmw_ioctl_fence_unref(struct vmw_winsys_screen *vws,
		      uint32_t handle)
{
   struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;
   vws_wddm->pEnv->pfnFenceUnref(vws_wddm->pEnv->pvEnv, handle);
}

static inline uint32_t
vmw_drm_fence_flags(uint32_t flags)
{
    uint32_t dflags = 0;

    if (flags & SVGA_FENCE_FLAG_EXEC)
	dflags |= DRM_VMW_FENCE_FLAG_EXEC;
    if (flags & SVGA_FENCE_FLAG_QUERY)
	dflags |= DRM_VMW_FENCE_FLAG_QUERY;

    return dflags;
}


int
vmw_ioctl_fence_signalled(struct vmw_winsys_screen *vws,
			  uint32_t handle,
			  uint32_t flags)
{
   RT_NOREF(flags);
   struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;
   GAFENCEQUERY FenceQuery;
   int ret;

   memset(&FenceQuery, 0, sizeof(FenceQuery));
   FenceQuery.u32FenceStatus = GA_FENCE_STATUS_NULL;

   ret = vws_wddm->pEnv->pfnFenceQuery(vws_wddm->pEnv->pvEnv, handle, &FenceQuery);

   if (ret != 0)
      return ret;

   if (FenceQuery.u32FenceStatus == GA_FENCE_STATUS_NULL)
      return 0; /* Treat as signalled. */

   /** @todo last 3 params in mesa: arg.passed_seqno, 0, false */
   vmw_fences_signal(vws->fence_ops, FenceQuery.u32ProcessedSeqNo, FenceQuery.u32SubmittedSeqNo, true);

   return FenceQuery.u32FenceStatus == GA_FENCE_STATUS_SIGNALED ? 0 : -1;
}



int
vmw_ioctl_fence_finish(struct vmw_winsys_screen *vws,
                       uint32_t handle,
		       uint32_t flags)
{
   RT_NOREF(flags);
   struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;

   vws_wddm->pEnv->pfnFenceWait(vws_wddm->pEnv->pvEnv, handle, VMW_FENCE_TIMEOUT_SECONDS*1000000);

   return 0; /* Regardless. */
}

uint32
vmw_ioctl_shader_create(struct vmw_winsys_screen *vws,
			SVGA3dShaderType type,
			uint32 code_len)
{
   ASMBreakpoint();
   RT_NOREF3(vws, type, code_len);
   return 0;
}

void
vmw_ioctl_shader_destroy(struct vmw_winsys_screen *vws, uint32 shid)
{
   ASMBreakpoint();
   RT_NOREF2(vws, shid);
   return;
}

static int
vmw_ioctl_parse_caps(struct vmw_winsys_screen *vws,
		     const uint32_t *cap_buffer)
{
   unsigned i;

   if (vws->base.have_gb_objects) {
      for (i = 0; i < vws->ioctl.num_cap_3d; ++i) {
	 vws->ioctl.cap_3d[i].has_cap = true;
	 vws->ioctl.cap_3d[i].result.u = cap_buffer[i];
      }
      return 0;
   } else {
      const uint32 *capsBlock;
      const SVGA3dCapsRecord *capsRecord = NULL;
      uint32 offset;
      const SVGA3dCapPair *capArray;
      unsigned numCaps, index;

      /*
       * Search linearly through the caps block records for the specified type.
       */
      capsBlock = cap_buffer;
      for (offset = 0; capsBlock[offset] != 0; offset += capsBlock[offset]) {
	 const SVGA3dCapsRecord *record;
	 if (offset >= SVGA_FIFO_3D_CAPS_SIZE)
            break;
	 record = (const SVGA3dCapsRecord *) (capsBlock + offset);
	 if ((record->header.type >= SVGA3DCAPS_RECORD_DEVCAPS_MIN) &&
	     (record->header.type <= SVGA3DCAPS_RECORD_DEVCAPS_MAX) &&
	     (!capsRecord || (record->header.type > capsRecord->header.type))) {
	    capsRecord = record;
	 }
      }

      if(!capsRecord)
	 return -1;

      /*
       * Calculate the number of caps from the size of the record.
       */
      capArray = (const SVGA3dCapPair *) capsRecord->data;
      numCaps = (int) ((capsRecord->header.length * sizeof(uint32) -
			sizeof capsRecord->header) / (2 * sizeof(uint32)));

      for (i = 0; i < numCaps; i++) {
	 index = capArray[i][0];
	 if (index < vws->ioctl.num_cap_3d) {
	    vws->ioctl.cap_3d[index].has_cap = true;
	    vws->ioctl.cap_3d[index].result.u = capArray[i][1];
	 } else {
	    debug_printf("Unknown devcaps seen: %d\n", index);
	 }
      }
   }
   return 0;
}

#define SVGA_CAP2_DX2 0x00000004
#define SVGA_CAP2_DX3 0x00000400

enum SVGASHADERMODEL
{
   SVGA_SM_LEGACY = 0,
   SVGA_SM_4,
   SVGA_SM_4_1,
   SVGA_SM_5,
   SVGA_SM_MAX
};

static enum SVGASHADERMODEL vboxGetShaderModel(struct vmw_winsys_screen_wddm *vws_wddm)
{
#if 0
    (void)vws_wddm;
    return SVGA_SM_5;
#else
   enum SVGASHADERMODEL enmResult = SVGA_SM_LEGACY;

   if (   (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAPABILITIES] & SVGA_CAP_GBOBJECTS)
       && (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAPABILITIES] & SVGA_CAP_CMD_BUFFERS_3 /*=SVGA_CAP_DX*/)
       && vws_wddm->HwInfo.au32Caps[SVGA3D_DEVCAP_DXCONTEXT])
   {
      enmResult = SVGA_SM_4;

      if (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAPABILITIES] & SVGA_CAP_CAP2_REGISTER)
      {
         if (   (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAP2] & SVGA_CAP2_DX2)
             && vws_wddm->HwInfo.au32Caps[SVGA3D_DEVCAP_SM41])
         {
            enmResult = SVGA_SM_4_1;

            if (   (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAP2] & SVGA_CAP2_DX3)
                && vws_wddm->HwInfo.au32Caps[SVGA3D_DEVCAP_SM5])
            {
               enmResult = SVGA_SM_5;
            }
         }
      }
   }

   return enmResult;
#endif
}

static int
vboxGetParam(struct vmw_winsys_screen_wddm *vws_wddm, struct drm_vmw_getparam_arg *gp_arg)
{
    /* DRM_VMW_GET_PARAM */
    switch (gp_arg->param)
    {
        case DRM_VMW_PARAM_NUM_STREAMS:
            gp_arg->value = 1; /* not used */
            break;
        case DRM_VMW_PARAM_NUM_FREE_STREAMS:
            gp_arg->value = 1; /* not used */
            break;
        case DRM_VMW_PARAM_3D:
            gp_arg->value = (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAPABILITIES] & SVGA_CAP_3D) != 0;
            break;
        case DRM_VMW_PARAM_HW_CAPS:
            gp_arg->value = vws_wddm->HwInfo.au32Regs[SVGA_REG_CAPABILITIES];
            break;
        case DRM_VMW_PARAM_FIFO_CAPS:
            gp_arg->value = vws_wddm->HwInfo.au32Fifo[SVGA_FIFO_CAPABILITIES];
            break;
        case DRM_VMW_PARAM_MAX_FB_SIZE:
            gp_arg->value = vws_wddm->HwInfo.au32Regs[SVGA_REG_MAX_PRIMARY_BOUNDING_BOX_MEM];
            break;
        case DRM_VMW_PARAM_FIFO_HW_VERSION:
            if (vws_wddm->HwInfo.au32Fifo[SVGA_FIFO_CAPABILITIES] & SVGA_FIFO_CAP_3D_HWVERSION_REVISED)
            {
                gp_arg->value =
                    vws_wddm->HwInfo.au32Fifo[SVGA_FIFO_3D_HWVERSION_REVISED];
            }
            else
            {
                gp_arg->value =
                    vws_wddm->HwInfo.au32Fifo[SVGA_FIFO_3D_HWVERSION];
            }
            break;
        case DRM_VMW_PARAM_MAX_SURF_MEMORY:
            if (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAPABILITIES] & SVGA_CAP_GBOBJECTS)
                gp_arg->value = vws_wddm->HwInfo.au32Regs[SVGA_REG_SUGGESTED_GBOBJECT_MEM_SIZE_KB] * 1024 / 2;
            else
                gp_arg->value = vws_wddm->HwInfo.au32Regs[SVGA_REG_MEMORY_SIZE];
            break;
        case DRM_VMW_PARAM_3D_CAPS_SIZE:
            if (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAPABILITIES] & SVGA_CAP_GBOBJECTS)
                gp_arg->value = SVGA3D_DEVCAP_MAX * sizeof(uint32_t);
            else
                gp_arg->value = (SVGA_FIFO_3D_CAPS_LAST - SVGA_FIFO_3D_CAPS + 1) * sizeof(uint32_t);
            break;
        case DRM_VMW_PARAM_MAX_MOB_MEMORY:
            gp_arg->value = vws_wddm->HwInfo.au32Regs[SVGA_REG_SUGGESTED_GBOBJECT_MEM_SIZE_KB] * 1024;
            break;
        case DRM_VMW_PARAM_MAX_MOB_SIZE:
            gp_arg->value = vws_wddm->HwInfo.au32Regs[SVGA_REG_MOB_MAX_SIZE];
            break;
        case DRM_VMW_PARAM_SCREEN_TARGET:
            gp_arg->value = 1; /* not used */
            break;
        case DRM_VMW_PARAM_DX:
            gp_arg->value = (vboxGetShaderModel(vws_wddm) >= SVGA_SM_4);
            break;
        case DRM_VMW_PARAM_HW_CAPS2:
            if (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAPABILITIES] & SVGA_CAP_CAP2_REGISTER)
                gp_arg->value = vws_wddm->HwInfo.au32Regs[SVGA_REG_CAP2];
            else
                gp_arg->value = 0;
            break;
        case DRM_VMW_PARAM_SM4_1:
            gp_arg->value = (vboxGetShaderModel(vws_wddm) >= SVGA_SM_4_1);
            break;
        case DRM_VMW_PARAM_SM5:
            gp_arg->value = (vboxGetShaderModel(vws_wddm) >= SVGA_SM_5);
            break;
        case DRM_VMW_PARAM_GL43:
            gp_arg->value = false; /** @todo */
            break;
        default: return -1;
    }
    return 0;
}

static int
vboxGet3DCap(struct vmw_winsys_screen_wddm *vws_wddm, void *pvCap, size_t cbCap)
{
    /* DRM_VMW_GET_3D_CAP */
    if (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAPABILITIES] & SVGA_CAP_GBOBJECTS)
        memcpy(pvCap, vws_wddm->HwInfo.au32Caps, cbCap);
    else
        memcpy(pvCap, &vws_wddm->HwInfo.au32Fifo[SVGA_FIFO_3D_CAPS], cbCap);
    return 0;
}

bool
vmw_ioctl_init(struct vmw_winsys_screen *vws)
{
   struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;

   struct drm_vmw_getparam_arg gp_arg;
   unsigned int size;
   int ret;
   uint32_t *cap_buffer;
   bool drm_gb_capable;
   bool have_drm_2_5;

   VMW_FUNC;

   have_drm_2_5 = 1;
   vws->ioctl.have_drm_2_6 = 1;
   vws->ioctl.have_drm_2_9 = 1;
   vws->ioctl.have_drm_2_15 = 1;
   vws->ioctl.have_drm_2_16 = 1;
   vws->ioctl.have_drm_2_17 = 1;
   vws->ioctl.have_drm_2_18 = 1;
   vws->ioctl.have_drm_2_19 = 1;
   vws->ioctl.have_drm_2_20 = 1;

   vws->ioctl.drm_execbuf_version = vws->ioctl.have_drm_2_9 ? 2 : 1;

   drm_gb_capable = have_drm_2_5;

   memset(&gp_arg, 0, sizeof(gp_arg));
   gp_arg.param = DRM_VMW_PARAM_3D;
   ret = vboxGetParam(vws_wddm, &gp_arg);
   if (ret || gp_arg.value == 0) {
      vmw_error("No 3D enabled (%i, %s).\n", ret, strerror(-ret));
      goto out_no_3d;
   }

   memset(&gp_arg, 0, sizeof(gp_arg));
   gp_arg.param = DRM_VMW_PARAM_FIFO_HW_VERSION;
   ret = vboxGetParam(vws_wddm, &gp_arg);
   if (ret) {
      vmw_error("Failed to get fifo hw version (%i, %s).\n",
                ret, strerror(-ret));
      goto out_no_3d;
   }
   vws->ioctl.hwversion = gp_arg.value;

   memset(&gp_arg, 0, sizeof(gp_arg));
   gp_arg.param = DRM_VMW_PARAM_HW_CAPS;
   ret = vboxGetParam(vws_wddm, &gp_arg);
   if (ret)
      vws->base.have_gb_objects = false;
   else
      vws->base.have_gb_objects =
         !!(gp_arg.value & (uint64_t) SVGA_CAP_GBOBJECTS);

   if (vws->base.have_gb_objects && !drm_gb_capable)
      goto out_no_3d;

   vws->base.have_vgpu10 = false;
   vws->base.have_sm4_1 = false;
   vws->base.have_intra_surface_copy = false;
   vws->base.device_id = 0x0405; /* assume SVGA II */

   if (vws->base.have_gb_objects) {
      memset(&gp_arg, 0, sizeof(gp_arg));
      gp_arg.param = DRM_VMW_PARAM_MAX_MOB_MEMORY;
      ret = vboxGetParam(vws_wddm, &gp_arg);
      if (ret) {
         /* Just guess a large enough value. */
         vws->ioctl.max_mob_memory = 256*1024*1024;
      } else {
         vws->ioctl.max_mob_memory = gp_arg.value;
      }

      memset(&gp_arg, 0, sizeof(gp_arg));
      gp_arg.param = DRM_VMW_PARAM_MAX_MOB_SIZE;
      ret = vboxGetParam(vws_wddm, &gp_arg);

      if (ret || gp_arg.value == 0) {
           vws->ioctl.max_texture_size = VMW_MAX_DEFAULT_TEXTURE_SIZE;
      } else {
           vws->ioctl.max_texture_size = gp_arg.value;
      }

      /* Never early flush surfaces, mobs do accounting. */
      vws->ioctl.max_surface_memory = -1;

      if (vws->ioctl.have_drm_2_9) {
         memset(&gp_arg, 0, sizeof(gp_arg));
         gp_arg.param = DRM_VMW_PARAM_DX;
         ret = vboxGetParam(vws_wddm, &gp_arg);
         if (ret == 0 && gp_arg.value != 0) {
            const char *vgpu10_val;

            debug_printf("Have VGPU10 interface and hardware.\n");
            vws->base.have_vgpu10 = true;
            vgpu10_val = getenv("SVGA_VGPU10");
            if (vgpu10_val && strcmp(vgpu10_val, "0") == 0) {
               debug_printf("Disabling VGPU10 interface.\n");
               vws->base.have_vgpu10 = false;
            } else {
               debug_printf("Enabling VGPU10 interface.\n");
            }
         }
      }

      if (vws->ioctl.have_drm_2_15 && vws->base.have_vgpu10) {
         memset(&gp_arg, 0, sizeof(gp_arg));
         gp_arg.param = DRM_VMW_PARAM_HW_CAPS2;
         ret = vboxGetParam(vws_wddm, &gp_arg);
         if (ret == 0 && gp_arg.value != 0) {
            vws->base.have_intra_surface_copy = true;
         }

         memset(&gp_arg, 0, sizeof(gp_arg));
         gp_arg.param = DRM_VMW_PARAM_SM4_1;
         ret = vboxGetParam(vws_wddm, &gp_arg);
         if (ret == 0 && gp_arg.value != 0) {
            vws->base.have_sm4_1 = true;
         }
      }

      if (vws->ioctl.have_drm_2_18 && vws->base.have_sm4_1) {
         memset(&gp_arg, 0, sizeof(gp_arg));
         gp_arg.param = DRM_VMW_PARAM_SM5;
         ret = vboxGetParam(vws_wddm, &gp_arg);
         if (ret == 0 && gp_arg.value != 0) {
            vws->base.have_sm5 = true;
         }
      }

      if (vws->ioctl.have_drm_2_20 && vws->base.have_sm5) {
         memset(&gp_arg, 0, sizeof(gp_arg));
         gp_arg.param = DRM_VMW_PARAM_GL43;
         ret = vboxGetParam(vws_wddm, &gp_arg);
         if (ret == 0 && gp_arg.value != 0) {
            vws->base.have_gl43 = true;
         }
      }

      memset(&gp_arg, 0, sizeof(gp_arg));
      gp_arg.param = DRM_VMW_PARAM_3D_CAPS_SIZE;
      ret = vboxGetParam(vws_wddm, &gp_arg);
      if (ret)
         size = SVGA_FIFO_3D_CAPS_SIZE * sizeof(uint32_t);
      else
         size = gp_arg.value;

      if (vws->base.have_gb_objects)
         vws->ioctl.num_cap_3d = size / sizeof(uint32_t);
      else
         vws->ioctl.num_cap_3d = SVGA3D_DEVCAP_MAX;

      if (vws->ioctl.have_drm_2_16) {
         vws->base.have_coherent = true;
      }
   } else {
      vws->ioctl.num_cap_3d = SVGA3D_DEVCAP_MAX;

      memset(&gp_arg, 0, sizeof(gp_arg));
      gp_arg.param = DRM_VMW_PARAM_MAX_SURF_MEMORY;
      if (have_drm_2_5)
         ret = vboxGetParam(vws_wddm, &gp_arg);
      if (!have_drm_2_5 || ret) {
         /* Just guess a large enough value, around 800mb. */
         vws->ioctl.max_surface_memory = 0x30000000;
      } else {
         vws->ioctl.max_surface_memory = gp_arg.value;
      }

      vws->ioctl.max_texture_size = VMW_MAX_DEFAULT_TEXTURE_SIZE;

      size = SVGA_FIFO_3D_CAPS_SIZE * sizeof(uint32_t);
   }

   debug_printf("VGPU10 interface is %s.\n",
                vws->base.have_vgpu10 ? "on" : "off");

   cap_buffer = calloc(1, size);
   if (!cap_buffer) {
      debug_printf("Failed alloc fifo 3D caps buffer.\n");
      goto out_no_3d;
   }

   vws->ioctl.cap_3d = calloc(vws->ioctl.num_cap_3d, 
			      sizeof(*vws->ioctl.cap_3d));
   if (!vws->ioctl.cap_3d) {
      debug_printf("Failed alloc fifo 3D caps buffer.\n");
      goto out_no_caparray;
   }

   /*
    * This call must always be after DRM_VMW_PARAM_MAX_MOB_MEMORY and
    * DRM_VMW_PARAM_SM4_1. This is because, based on these calls, kernel
    * driver sends the supported cap.
    */
   ret = vboxGet3DCap(vws_wddm, cap_buffer, size);

   if (ret) {
      debug_printf("Failed to get 3D capabilities"
		   " (%i, %s).\n", ret, strerror(-ret));
      goto out_no_caps;
   }

   ret = vmw_ioctl_parse_caps(vws, cap_buffer);
   if (ret) {
      debug_printf("Failed to parse 3D capabilities"
		   " (%i, %s).\n", ret, strerror(-ret));
      goto out_no_caps;
   }

   if (vws->ioctl.have_drm_2_15 && vws->base.have_vgpu10) {

     /* support for these commands didn't make it into vmwgfx kernel
      * modules before 2.10.
      */
      vws->base.have_generate_mipmap_cmd = true;
      vws->base.have_set_predication_cmd = true;
   }

   if (vws->ioctl.have_drm_2_15) {
      vws->base.have_fence_fd = true;
   }

   free(cap_buffer);
   vmw_printf("%s OK\n", __FUNCTION__);
   return true;
  out_no_caps:
   free(vws->ioctl.cap_3d);
  out_no_caparray:
   free(cap_buffer);
  out_no_3d:
   vws->ioctl.num_cap_3d = 0;
   debug_printf("%s Failed\n", __FUNCTION__);
   return false;
}



void
vmw_ioctl_cleanup(struct vmw_winsys_screen *vws)
{
   VMW_FUNC;

   free(vws->ioctl.cap_3d);
}
