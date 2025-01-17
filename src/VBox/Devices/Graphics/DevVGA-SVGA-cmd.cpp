/* $Id: DevVGA-SVGA-cmd.cpp 107929 2025-01-17 08:39:07Z dmitrii.grigorev@oracle.com $ */
/** @file
 * VMware SVGA device - implementation of VMSVGA commands.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef IN_RING3
# error "DevVGA-SVGA-cmd.cpp is only for ring-3 code"
#endif


#define LOG_GROUP LOG_GROUP_DEV_VMSVGA
#include <iprt/mem.h>
#include <iprt/path.h>
#include <VBox/AssertGuest.h>
#include <VBox/log.h>
#include <VBox/vmm/pdmdev.h>
#include <VBoxVideo.h>

/* should go BEFORE any other DevVGA include to make all DevVGA.h config defines be visible */
#include "DevVGA.h"

/* Should be included after DevVGA.h/DevVGA-SVGA.h to pick all defines. */
#ifdef VBOX_WITH_VMSVGA3D
# include "DevVGA-SVGA3d.h"
#endif
#include "DevVGA-SVGA-internal.h"

#include <iprt/formats/bmp.h>
#include <stdio.h>

#if defined(LOG_ENABLED) || defined(VBOX_STRICT)
# define SVGA_CASE_ID2STR(idx) case idx: return #idx

static const char *vmsvgaFifo3dCmdToString(SVGAFifo3dCmdId enmCmdId)
{
    switch (enmCmdId)
    {
        SVGA_CASE_ID2STR(SVGA_3D_CMD_LEGACY_BASE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SURFACE_DEFINE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SURFACE_DESTROY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SURFACE_COPY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SURFACE_STRETCHBLT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SURFACE_DMA);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_CONTEXT_DEFINE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_CONTEXT_DESTROY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SETTRANSFORM);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SETZRANGE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SETRENDERSTATE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SETRENDERTARGET);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SETTEXTURESTATE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SETMATERIAL);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SETLIGHTDATA);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SETLIGHTENABLED);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SETVIEWPORT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SETCLIPPLANE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_CLEAR);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_PRESENT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SHADER_DEFINE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SHADER_DESTROY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SET_SHADER);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SET_SHADER_CONST);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DRAW_PRIMITIVES);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SETSCISSORRECT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_BEGIN_QUERY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_END_QUERY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_WAIT_FOR_QUERY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_PRESENT_READBACK);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SURFACE_DEFINE_V2);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_GENERATE_MIPMAPS);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEAD4); /* SVGA_3D_CMD_VIDEO_CREATE_DECODER */
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEAD5); /* SVGA_3D_CMD_VIDEO_DESTROY_DECODER */
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEAD6); /* SVGA_3D_CMD_VIDEO_CREATE_PROCESSOR */
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEAD7); /* SVGA_3D_CMD_VIDEO_DESTROY_PROCESSOR */
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEAD8); /* SVGA_3D_CMD_VIDEO_DECODE_START_FRAME */
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEAD9); /* SVGA_3D_CMD_VIDEO_DECODE_RENDER */
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEAD10); /* SVGA_3D_CMD_VIDEO_DECODE_END_FRAME */
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEAD11); /* SVGA_3D_CMD_VIDEO_PROCESS_FRAME */
        SVGA_CASE_ID2STR(SVGA_3D_CMD_ACTIVATE_SURFACE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEACTIVATE_SURFACE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SCREEN_DMA);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_VB_DX_CLEAR_RENDERTARGET_VIEW_REGION); /* SVGA_3D_CMD_DEAD1 */
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEAD2);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEAD12); /* Old SVGA_3D_CMD_LOGICOPS_BITBLT */
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEAD13); /* Old SVGA_3D_CMD_LOGICOPS_TRANSBLT */
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEAD14); /* Old SVGA_3D_CMD_LOGICOPS_STRETCHBLT */
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEAD15); /* Old SVGA_3D_CMD_LOGICOPS_COLORFILL */
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEAD16); /* Old SVGA_3D_CMD_LOGICOPS_ALPHABLEND */
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEAD17); /* Old SVGA_3D_CMD_LOGICOPS_CLEARTYPEBLEND */
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SET_OTABLE_BASE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_READBACK_OTABLE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEFINE_GB_MOB);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DESTROY_GB_MOB);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEAD3);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_UPDATE_GB_MOB_MAPPING);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEFINE_GB_SURFACE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DESTROY_GB_SURFACE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_BIND_GB_SURFACE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_COND_BIND_GB_SURFACE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_UPDATE_GB_IMAGE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_UPDATE_GB_SURFACE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_READBACK_GB_IMAGE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_READBACK_GB_SURFACE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_INVALIDATE_GB_IMAGE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_INVALIDATE_GB_SURFACE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEFINE_GB_CONTEXT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DESTROY_GB_CONTEXT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_BIND_GB_CONTEXT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_READBACK_GB_CONTEXT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_INVALIDATE_GB_CONTEXT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEFINE_GB_SHADER);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DESTROY_GB_SHADER);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_BIND_GB_SHADER);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SET_OTABLE_BASE64);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_BEGIN_GB_QUERY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_END_GB_QUERY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_WAIT_FOR_GB_QUERY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_NOP);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_ENABLE_GART);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DISABLE_GART);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_MAP_MOB_INTO_GART);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_UNMAP_GART_RANGE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEFINE_GB_SCREENTARGET);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DESTROY_GB_SCREENTARGET);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_BIND_GB_SCREENTARGET);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_UPDATE_GB_SCREENTARGET);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_READBACK_GB_IMAGE_PARTIAL);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_INVALIDATE_GB_IMAGE_PARTIAL);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SET_GB_SHADERCONSTS_INLINE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_GB_SCREEN_DMA);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_BIND_GB_SURFACE_WITH_PITCH);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_GB_MOB_FENCE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEFINE_GB_SURFACE_V2);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEFINE_GB_MOB64);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_REDEFINE_GB_MOB64);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_NOP_ERROR);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SET_VERTEX_STREAMS);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SET_VERTEX_DECLS);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SET_VERTEX_DIVISORS);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DRAW);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DRAW_INDEXED);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DEFINE_CONTEXT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DESTROY_CONTEXT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_BIND_CONTEXT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_READBACK_CONTEXT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_INVALIDATE_CONTEXT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_SINGLE_CONSTANT_BUFFER);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_SHADER_RESOURCES);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_SHADER);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_SAMPLERS);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DRAW);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DRAW_INDEXED);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DRAW_INSTANCED);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DRAW_INDEXED_INSTANCED);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DRAW_AUTO);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_INPUT_LAYOUT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_VERTEX_BUFFERS);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_INDEX_BUFFER);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_TOPOLOGY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_RENDERTARGETS);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_BLEND_STATE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_DEPTHSTENCIL_STATE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_RASTERIZER_STATE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DEFINE_QUERY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DESTROY_QUERY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_BIND_QUERY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_QUERY_OFFSET);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_BEGIN_QUERY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_END_QUERY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_READBACK_QUERY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_PREDICATION);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_SOTARGETS);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_VIEWPORTS);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_SCISSORRECTS);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_CLEAR_RENDERTARGET_VIEW);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_CLEAR_DEPTHSTENCIL_VIEW);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_PRED_COPY_REGION);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_PRED_COPY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_PRESENTBLT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_GENMIPS);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_UPDATE_SUBRESOURCE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_READBACK_SUBRESOURCE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_INVALIDATE_SUBRESOURCE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DEFINE_SHADERRESOURCE_VIEW);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DESTROY_SHADERRESOURCE_VIEW);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DEFINE_RENDERTARGET_VIEW);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DESTROY_RENDERTARGET_VIEW);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_VIEW);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_VIEW);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DEFINE_ELEMENTLAYOUT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DESTROY_ELEMENTLAYOUT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DEFINE_BLEND_STATE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DESTROY_BLEND_STATE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_STATE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_STATE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DEFINE_RASTERIZER_STATE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DESTROY_RASTERIZER_STATE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DEFINE_SAMPLER_STATE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DESTROY_SAMPLER_STATE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DEFINE_SHADER);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DESTROY_SHADER);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_BIND_SHADER);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DEFINE_STREAMOUTPUT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DESTROY_STREAMOUTPUT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_STREAMOUTPUT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_COTABLE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_READBACK_COTABLE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_BUFFER_COPY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_TRANSFER_FROM_BUFFER);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SURFACE_COPY_AND_READBACK);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_MOVE_QUERY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_BIND_ALL_QUERY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_READBACK_ALL_QUERY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_PRED_TRANSFER_FROM_BUFFER);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_MOB_FENCE_64);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_BIND_ALL_SHADER);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_HINT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_BUFFER_UPDATE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_VS_CONSTANT_BUFFER_OFFSET);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_PS_CONSTANT_BUFFER_OFFSET);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_GS_CONSTANT_BUFFER_OFFSET);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_HS_CONSTANT_BUFFER_OFFSET);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_DS_CONSTANT_BUFFER_OFFSET);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_CS_CONSTANT_BUFFER_OFFSET);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_COND_BIND_ALL_SHADER);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SCREEN_COPY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_RESERVED1);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_RESERVED2);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_RESERVED3);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_RESERVED4);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_RESERVED5);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_RESERVED6);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_RESERVED7);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_RESERVED8);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_GROW_OTABLE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_GROW_COTABLE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_INTRA_SURFACE_COPY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEFINE_GB_SURFACE_V3);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_RESOLVE_COPY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_PRED_RESOLVE_COPY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_PRED_CONVERT_REGION);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_PRED_CONVERT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_WHOLE_SURFACE_COPY);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DEFINE_UA_VIEW);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DESTROY_UA_VIEW);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_CLEAR_UA_VIEW_UINT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_CLEAR_UA_VIEW_FLOAT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_COPY_STRUCTURE_COUNT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_UA_VIEWS);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DRAW_INDEXED_INSTANCED_INDIRECT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DRAW_INSTANCED_INDIRECT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DISPATCH);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DISPATCH_INDIRECT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_WRITE_ZERO_SURFACE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_HINT_ZERO_SURFACE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_TRANSFER_TO_BUFFER);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_STRUCTURE_COUNT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_LOGICOPS_BITBLT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_LOGICOPS_TRANSBLT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_LOGICOPS_STRETCHBLT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_LOGICOPS_COLORFILL);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_LOGICOPS_ALPHABLEND);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_LOGICOPS_CLEARTYPEBLEND);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_RESERVED2_1);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_RESERVED2_2);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DEFINE_GB_SURFACE_V4);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_CS_UA_VIEWS);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_MIN_LOD);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_RESERVED2_3);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_RESERVED2_4);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_VIEW_V2);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DEFINE_STREAMOUTPUT_WITH_MOB);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_SET_SHADER_IFACE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_BIND_STREAMOUTPUT);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_SURFACE_STRETCHBLT_NON_MS_TO_MS);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_BIND_SHADER_IFACE);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_DX_DEFINE_RASTERIZER_STATE_V2);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_MAX);
        SVGA_CASE_ID2STR(SVGA_3D_CMD_FUTURE_MAX);

        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_DEFINE_VIDEO_PROCESSOR);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_DEFINE_VIDEO_DECODER_OUTPUT_VIEW);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_DEFINE_VIDEO_DECODER);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_DECODER_BEGIN_FRAME);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_DECODER_SUBMIT_BUFFERS);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_DECODER_END_FRAME);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_DEFINE_VIDEO_PROCESSOR_INPUT_VIEW);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_DEFINE_VIDEO_PROCESSOR_OUTPUT_VIEW);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_BLT);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_DESTROY_VIDEO_DECODER);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_DESTROY_VIDEO_DECODER_OUTPUT_VIEW);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_DESTROY_VIDEO_PROCESSOR);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_DESTROY_VIDEO_PROCESSOR_INPUT_VIEW);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_DESTROY_VIDEO_PROCESSOR_OUTPUT_VIEW);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_TARGET_RECT);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_BACKGROUND_COLOR);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_COLOR_SPACE);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_ALPHA_FILL_MODE);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_CONSTRICTION);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_STEREO_MODE);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_FRAME_FORMAT);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_COLOR_SPACE);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_OUTPUT_RATE);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_SOURCE_RECT);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_DEST_RECT);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_ALPHA);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_PALETTE);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_PIXEL_ASPECT_RATIO);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_LUMA_KEY);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_STEREO_FORMAT);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_AUTO_PROCESSING_MODE);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_FILTER);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_ROTATION);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_GET_VIDEO_CAPABILITY);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_CLEAR_RTV);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_CLEAR_UAV);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_CLEAR_VDOV);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_CLEAR_VPIV);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_DX_CLEAR_VPOV);
        SVGA_CASE_ID2STR(VBSVGA_3D_CMD_MAX);
#ifndef DEBUG_sunlover
        default: break; /* Compiler warning. */
#endif
    }
    return "UNKNOWN_3D";
}

/**
 * FIFO command name lookup
 *
 * @returns FIFO command string or "UNKNOWN"
 * @param   u32Cmd      FIFO command
 */
const char *vmsvgaR3FifoCmdToString(uint32_t u32Cmd)
{
    switch (u32Cmd)
    {
        SVGA_CASE_ID2STR(SVGA_CMD_INVALID_CMD);
        SVGA_CASE_ID2STR(SVGA_CMD_UPDATE);
        SVGA_CASE_ID2STR(SVGA_CMD_RECT_FILL);
        SVGA_CASE_ID2STR(SVGA_CMD_RECT_COPY);
        SVGA_CASE_ID2STR(SVGA_CMD_RECT_ROP_COPY);
        SVGA_CASE_ID2STR(SVGA_CMD_DEFINE_CURSOR);
        SVGA_CASE_ID2STR(SVGA_CMD_DISPLAY_CURSOR);
        SVGA_CASE_ID2STR(SVGA_CMD_MOVE_CURSOR);
        SVGA_CASE_ID2STR(SVGA_CMD_DEFINE_ALPHA_CURSOR);
        SVGA_CASE_ID2STR(SVGA_CMD_UPDATE_VERBOSE);
        SVGA_CASE_ID2STR(SVGA_CMD_FRONT_ROP_FILL);
        SVGA_CASE_ID2STR(SVGA_CMD_FENCE);
        SVGA_CASE_ID2STR(SVGA_CMD_ESCAPE);
        SVGA_CASE_ID2STR(SVGA_CMD_DEFINE_SCREEN);
        SVGA_CASE_ID2STR(SVGA_CMD_DESTROY_SCREEN);
        SVGA_CASE_ID2STR(SVGA_CMD_DEFINE_GMRFB);
        SVGA_CASE_ID2STR(SVGA_CMD_BLIT_GMRFB_TO_SCREEN);
        SVGA_CASE_ID2STR(SVGA_CMD_BLIT_SCREEN_TO_GMRFB);
        SVGA_CASE_ID2STR(SVGA_CMD_ANNOTATION_FILL);
        SVGA_CASE_ID2STR(SVGA_CMD_ANNOTATION_COPY);
        SVGA_CASE_ID2STR(SVGA_CMD_DEFINE_GMR2);
        SVGA_CASE_ID2STR(SVGA_CMD_REMAP_GMR2);
        SVGA_CASE_ID2STR(SVGA_CMD_DEAD);
        SVGA_CASE_ID2STR(SVGA_CMD_DEAD_2);
        SVGA_CASE_ID2STR(SVGA_CMD_NOP);
        SVGA_CASE_ID2STR(SVGA_CMD_NOP_ERROR);
        SVGA_CASE_ID2STR(SVGA_CMD_MAX);
        default:
            if (   (u32Cmd >= SVGA_3D_CMD_BASE && u32Cmd < SVGA_3D_CMD_MAX)
                || (u32Cmd >= VBSVGA_3D_CMD_BASE && u32Cmd < VBSVGA_3D_CMD_MAX))
                return vmsvgaFifo3dCmdToString((SVGAFifo3dCmdId)u32Cmd);
    }
    return "UNKNOWN";
}
# undef SVGA_CASE_ID2STR
#endif /* LOG_ENABLED || VBOX_STRICT */


/*
 *
 * Guest-Backed Objects (GBO).
 *
 */

#ifdef VBOX_WITH_VMSVGA3D

static int vmsvgaR3GboCreate(PVMSVGAR3STATE pSvgaR3State, SVGAMobFormat ptDepth, PPN64 baseAddress, uint32_t sizeInBytes, PVMSVGAGBO pGbo)
{
    ASSERT_GUEST_RETURN(sizeInBytes <= _128M, VERR_INVALID_PARAMETER); /** @todo Less than SVGA_REG_MOB_MAX_SIZE */

    /*
     * The 'baseAddress' is a page number and points to the 'root page' of the GBO.
     * Content of the root page depends on the ptDepth value:
     * SVGA3D_MOBFMT_PTDEPTH[64]_0 - the only data page;
     * SVGA3D_MOBFMT_PTDEPTH[64]_1 - array of page numbers for data pages;
     * SVGA3D_MOBFMT_PTDEPTH[64]_2 - array of page numbers for SVGA3D_MOBFMT_PTDEPTH[64]_1 pages.
     * The code below extracts the page addresses of the GBO.
     */

    /* Verify and normalize the ptDepth value. */
    bool fGCPhys64; /* Whether the page table contains 64 bit page numbers. */
    if (RT_LIKELY(   ptDepth == SVGA3D_MOBFMT_PTDEPTH64_0
                  || ptDepth == SVGA3D_MOBFMT_PTDEPTH64_1
                  || ptDepth == SVGA3D_MOBFMT_PTDEPTH64_2))
        fGCPhys64 = true;
    else if (   ptDepth == SVGA3D_MOBFMT_PTDEPTH_0
             || ptDepth == SVGA3D_MOBFMT_PTDEPTH_1
             || ptDepth == SVGA3D_MOBFMT_PTDEPTH_2)
    {
        fGCPhys64 = false;
        /* Shift ptDepth to the SVGA3D_MOBFMT_PTDEPTH64_x range. */
        ptDepth = (SVGAMobFormat)(ptDepth + SVGA3D_MOBFMT_PTDEPTH64_0 - SVGA3D_MOBFMT_PTDEPTH_0);
    }
    else if (ptDepth == SVGA3D_MOBFMT_RANGE)
        fGCPhys64 = false; /* Does not matter, there is no page table. */
    else
        ASSERT_GUEST_FAILED_RETURN(VERR_INVALID_PARAMETER);

    uint32_t const cPPNsPerPage = X86_PAGE_SIZE / (fGCPhys64 ? sizeof(PPN64) : sizeof(PPN));

    pGbo->cbTotal = sizeInBytes;
    pGbo->cTotalPages = (sizeInBytes + X86_PAGE_SIZE - 1) >> X86_PAGE_SHIFT;

    /* Allocate the maximum amount possible (everything non-continuous) */
    PVMSVGAGBODESCRIPTOR paDescriptors = (PVMSVGAGBODESCRIPTOR)RTMemAlloc(pGbo->cTotalPages * sizeof(VMSVGAGBODESCRIPTOR));
    AssertReturn(paDescriptors, VERR_NO_MEMORY);

    int rc = VINF_SUCCESS;
    if (ptDepth == SVGA3D_MOBFMT_PTDEPTH64_0)
    {
        ASSERT_GUEST_STMT_RETURN(pGbo->cTotalPages == 1,
                                 RTMemFree(paDescriptors),
                                 VERR_INVALID_PARAMETER);

        RTGCPHYS GCPhys = (RTGCPHYS)baseAddress << X86_PAGE_SHIFT;
        GCPhys &= UINT64_C(0x00000FFFFFFFFFFF); /* Seeing rubbish in the top bits with certain linux guests. */
        paDescriptors[0].GCPhys   = GCPhys;
        paDescriptors[0].cPages = 1;
    }
    else if (ptDepth == SVGA3D_MOBFMT_PTDEPTH64_1)
    {
        ASSERT_GUEST_STMT_RETURN(pGbo->cTotalPages <= cPPNsPerPage,
                                 RTMemFree(paDescriptors),
                                 VERR_INVALID_PARAMETER);

        /* Read the root page. */
        uint8_t au8RootPage[X86_PAGE_SIZE];
        RTGCPHYS GCPhys = (RTGCPHYS)baseAddress << X86_PAGE_SHIFT;
        rc = PDMDevHlpPCIPhysRead(pSvgaR3State->pDevIns, GCPhys, &au8RootPage, sizeof(au8RootPage));
        if (RT_SUCCESS(rc))
        {
            PPN64 *paPPN64 = (PPN64 *)&au8RootPage[0];
            PPN *paPPN32 = (PPN *)&au8RootPage[0];
            for (uint32_t iPPN = 0; iPPN < pGbo->cTotalPages; ++iPPN)
            {
                GCPhys = (RTGCPHYS)(fGCPhys64 ? paPPN64[iPPN] : paPPN32[iPPN]) << X86_PAGE_SHIFT;
                GCPhys &= UINT64_C(0x00000FFFFFFFFFFF); /* Seeing rubbish in the top bits with certain linux guests. */
                paDescriptors[iPPN].GCPhys   = GCPhys;
                paDescriptors[iPPN].cPages = 1;
            }
        }
    }
    else if (ptDepth == SVGA3D_MOBFMT_PTDEPTH64_2)
    {
        ASSERT_GUEST_STMT_RETURN(pGbo->cTotalPages <= cPPNsPerPage * cPPNsPerPage,
                                 RTMemFree(paDescriptors),
                                 VERR_INVALID_PARAMETER);

        /* Read the Level2 root page. */
        uint8_t au8RootPageLevel2[X86_PAGE_SIZE];
        RTGCPHYS GCPhys = (RTGCPHYS)baseAddress << X86_PAGE_SHIFT;
        rc = PDMDevHlpPCIPhysRead(pSvgaR3State->pDevIns, GCPhys, &au8RootPageLevel2, sizeof(au8RootPageLevel2));
        if (RT_SUCCESS(rc))
        {
            uint32_t cPagesLeft = pGbo->cTotalPages;

            PPN64 *paPPN64Level2 = (PPN64 *)&au8RootPageLevel2[0];
            PPN *paPPN32Level2 = (PPN *)&au8RootPageLevel2[0];

            uint32_t const cPPNsLevel2 = (pGbo->cTotalPages + cPPNsPerPage - 1) / cPPNsPerPage;
            for (uint32_t iPPNLevel2 = 0; iPPNLevel2 < cPPNsLevel2; ++iPPNLevel2)
            {
                /* Read the Level1 root page. */
                uint8_t au8RootPage[X86_PAGE_SIZE];
                RTGCPHYS GCPhysLevel1 = (RTGCPHYS)(fGCPhys64 ? paPPN64Level2[iPPNLevel2] : paPPN32Level2[iPPNLevel2]) << X86_PAGE_SHIFT;
                GCPhys &= UINT64_C(0x00000FFFFFFFFFFF); /* Seeing rubbish in the top bits with certain linux guests. */
                rc = PDMDevHlpPCIPhysRead(pSvgaR3State->pDevIns, GCPhysLevel1, &au8RootPage, sizeof(au8RootPage));
                if (RT_SUCCESS(rc))
                {
                    PPN64 *paPPN64 = (PPN64 *)&au8RootPage[0];
                    PPN *paPPN32 = (PPN *)&au8RootPage[0];

                    uint32_t const cPPNs = RT_MIN(cPagesLeft, cPPNsPerPage);
                    for (uint32_t iPPN = 0; iPPN < cPPNs; ++iPPN)
                    {
                        GCPhys = (RTGCPHYS)(fGCPhys64 ? paPPN64[iPPN] : paPPN32[iPPN]) << X86_PAGE_SHIFT;
                        GCPhys &= UINT64_C(0x00000FFFFFFFFFFF); /* Seeing rubbish in the top bits with certain linux guests. */
                        paDescriptors[iPPN + iPPNLevel2 * cPPNsPerPage].GCPhys   = GCPhys;
                        paDescriptors[iPPN + iPPNLevel2 * cPPNsPerPage].cPages = 1;
                    }
                    cPagesLeft -= cPPNs;
                }
            }
        }
    }
    else if (ptDepth == SVGA3D_MOBFMT_RANGE)
    {
        RTGCPHYS GCPhys = (RTGCPHYS)baseAddress << X86_PAGE_SHIFT;
        GCPhys &= UINT64_C(0x00000FFFFFFFFFFF); /* Seeing rubbish in the top bits with certain linux guests. */
        paDescriptors[0].GCPhys = GCPhys;
        paDescriptors[0].cPages = pGbo->cTotalPages;
    }
    else
    {
        AssertFailed();
        return VERR_INTERNAL_ERROR; /* ptDepth should be already verified. */
    }

    /* Compress the descriptors. */
    if (ptDepth != SVGA3D_MOBFMT_RANGE)
    {
        uint32_t iDescriptor = 0;
        for (uint32_t i = 1; i < pGbo->cTotalPages; ++i)
        {
            /* Continuous physical memory? */
            if (paDescriptors[i].GCPhys == paDescriptors[iDescriptor].GCPhys + paDescriptors[iDescriptor].cPages * X86_PAGE_SIZE)
            {
                Assert(paDescriptors[iDescriptor].cPages);
                paDescriptors[iDescriptor].cPages++;
                Log5Func(("Page %x GCPhys=%RGp successor\n", i, paDescriptors[i].GCPhys));
            }
            else
            {
                iDescriptor++;
                paDescriptors[iDescriptor].GCPhys   = paDescriptors[i].GCPhys;
                paDescriptors[iDescriptor].cPages = 1;
                Log5Func(("Page %x GCPhys=%RGp\n", i, paDescriptors[iDescriptor].GCPhys));
            }
        }

        pGbo->cDescriptors = iDescriptor + 1;
        Log5Func(("Nr of descriptors %d\n", pGbo->cDescriptors));
    }
    else
        pGbo->cDescriptors = 1;

    if (RT_LIKELY(pGbo->cDescriptors < pGbo->cTotalPages))
    {
        pGbo->paDescriptors = (PVMSVGAGBODESCRIPTOR)RTMemRealloc(paDescriptors, pGbo->cDescriptors * sizeof(VMSVGAGBODESCRIPTOR));
        AssertReturn(pGbo->paDescriptors, VERR_NO_MEMORY);
    }
    else
        pGbo->paDescriptors = paDescriptors;

    pGbo->fGboFlags = 0;
    pGbo->pvHost = NULL;

    return VINF_SUCCESS;
}


static void vmsvgaR3GboDestroy(PVMSVGAR3STATE pSvgaR3State, PVMSVGAGBO pGbo)
{
    RT_NOREF(pSvgaR3State);

    if (RT_LIKELY(VMSVGA_IS_GBO_CREATED(pGbo)))
    {
        RTMemFree(pGbo->pvHost);
        RTMemFree(pGbo->paDescriptors);
        RT_ZERO(*pGbo);
    }
}

/** @todo static void vmsvgaR3GboWriteProtect(PVMSVGAR3STATE pSvgaR3State, PVMSVGAGBO pGbo, bool fWriteProtect) */

typedef enum VMSVGAGboTransferDirection
{
    VMSVGAGboTransferDirection_Read,
    VMSVGAGboTransferDirection_Write,
} VMSVGAGboTransferDirection;

static int vmsvgaR3GboTransfer(PVMSVGAR3STATE pSvgaR3State, PVMSVGAGBO pGbo,
                               uint32_t off, void *pvData, uint32_t cbData,
                               VMSVGAGboTransferDirection enmDirection)
{
    //DEBUG_BREAKPOINT_TEST();
    int rc = VINF_SUCCESS;
    uint8_t *pu8CurrentHost  = (uint8_t *)pvData;

    /* Find the right descriptor */
    PCVMSVGAGBODESCRIPTOR const paDescriptors = pGbo->paDescriptors;
    uint32_t iDescriptor = 0;                               /* Index in the descriptor array. */
    uint32_t offDescriptor = 0;                             /* GMR offset of the current descriptor. */
    while (offDescriptor + paDescriptors[iDescriptor].cPages * X86_PAGE_SIZE <= off)
    {
        offDescriptor += paDescriptors[iDescriptor].cPages * X86_PAGE_SIZE;
        AssertReturn(offDescriptor < pGbo->cbTotal, VERR_INTERNAL_ERROR); /* overflow protection */
        ++iDescriptor;
        AssertReturn(iDescriptor < pGbo->cDescriptors, VERR_INTERNAL_ERROR);
    }

    while (cbData)
    {
        uint32_t cbToCopy;
        if (off + cbData <= offDescriptor + paDescriptors[iDescriptor].cPages * X86_PAGE_SIZE)
            cbToCopy = cbData;
        else
        {
            cbToCopy = (offDescriptor + paDescriptors[iDescriptor].cPages * X86_PAGE_SIZE - off);
            AssertReturn(cbToCopy <= cbData, VERR_INVALID_PARAMETER);
        }

        RTGCPHYS const GCPhys = paDescriptors[iDescriptor].GCPhys + off - offDescriptor;
        Log5Func(("%s phys=%RGp\n", (enmDirection == VMSVGAGboTransferDirection_Read) ? "READ" : "WRITE", GCPhys));

        /*
         * We are deliberately using the non-PCI version of PDMDevHlpPCIPhys[Read|Write] as the
         * guest-side VMSVGA driver seems to allocate non-DMA (regular physical) addresses,
         * see @bugref{9654#c75}.
         */
        if (enmDirection == VMSVGAGboTransferDirection_Read)
            rc = PDMDevHlpPhysRead(pSvgaR3State->pDevIns, GCPhys, pu8CurrentHost, cbToCopy);
        else
            rc = PDMDevHlpPhysWrite(pSvgaR3State->pDevIns, GCPhys, pu8CurrentHost, cbToCopy);
        AssertRCBreak(rc);

        cbData         -= cbToCopy;
        off            += cbToCopy;
        pu8CurrentHost += cbToCopy;

        /* Go to the next descriptor if there's anything left. */
        if (cbData)
        {
            offDescriptor += paDescriptors[iDescriptor].cPages * X86_PAGE_SIZE;
            AssertReturn(offDescriptor < pGbo->cbTotal, VERR_INTERNAL_ERROR);
            ++iDescriptor;
            AssertReturn(iDescriptor < pGbo->cDescriptors, VERR_INTERNAL_ERROR);
        }
    }
    return rc;
}


static int vmsvgaR3GboWrite(PVMSVGAR3STATE pSvgaR3State, PVMSVGAGBO pGbo,
                            uint32_t off, void const *pvData, uint32_t cbData)
{
    return vmsvgaR3GboTransfer(pSvgaR3State, pGbo,
                               off, (void *)pvData, cbData,
                               VMSVGAGboTransferDirection_Write);
}


static int vmsvgaR3GboRead(PVMSVGAR3STATE pSvgaR3State, PVMSVGAGBO pGbo,
                           uint32_t off, void *pvData, uint32_t cbData)
{
    return vmsvgaR3GboTransfer(pSvgaR3State, pGbo,
                               off, pvData, cbData,
                               VMSVGAGboTransferDirection_Read);
}


static int vmsvgaR3GboBackingStoreCreate(PVMSVGAR3STATE pSvgaR3State, PVMSVGAGBO pGbo, uint32_t cbValid)
{
    int rc;

    /* Just reread the data if pvHost has been allocated already. */
    if (!(pGbo->fGboFlags & VMSVGAGBO_F_HOST_BACKED))
        pGbo->pvHost = RTMemAllocZ(pGbo->cbTotal);

    if (pGbo->pvHost)
    {
        cbValid = RT_MIN(cbValid, pGbo->cbTotal);
        rc = vmsvgaR3GboRead(pSvgaR3State, pGbo, 0, pGbo->pvHost, cbValid);
    }
    else
        rc = VERR_NO_MEMORY;

    if (RT_SUCCESS(rc))
        pGbo->fGboFlags |= VMSVGAGBO_F_HOST_BACKED;
    else
    {
        RTMemFree(pGbo->pvHost);
        pGbo->pvHost = NULL;
    }
    return rc;
}


static void vmsvgaR3GboBackingStoreDelete(PVMSVGAR3STATE pSvgaR3State, PVMSVGAGBO pGbo)
{
    RT_NOREF(pSvgaR3State);
    AssertReturnVoid(pGbo->fGboFlags & VMSVGAGBO_F_HOST_BACKED);
    RTMemFree(pGbo->pvHost);
    pGbo->pvHost = NULL;
    pGbo->fGboFlags &= ~VMSVGAGBO_F_HOST_BACKED;
}


static int vmsvgaR3GboBackingStoreWriteToGuest(PVMSVGAR3STATE pSvgaR3State, PVMSVGAGBO pGbo)
{
    AssertReturn(pGbo->fGboFlags & VMSVGAGBO_F_HOST_BACKED, VERR_INVALID_STATE);
    return vmsvgaR3GboWrite(pSvgaR3State, pGbo, 0, pGbo->pvHost, pGbo->cbTotal);
}


static int vmsvgaR3GboBackingStoreReadFromGuest(PVMSVGAR3STATE pSvgaR3State, PVMSVGAGBO pGbo)
{
    AssertReturn(pGbo->fGboFlags & VMSVGAGBO_F_HOST_BACKED, VERR_INVALID_STATE);
    return vmsvgaR3GboRead(pSvgaR3State, pGbo, 0, pGbo->pvHost, pGbo->cbTotal);
}

static int vmsvgaR3GboCopy(PVMSVGAR3STATE pSvgaR3State, PVMSVGAGBO pGboDst, uint32_t offDst,
                           PVMSVGAGBO pGboSrc, uint32_t offSrc, uint32_t cbCopy)
{
    uint32_t const cbTmpBuf = GUEST_PAGE_SIZE;
    void *pvTmpBuf = RTMemTmpAlloc(cbTmpBuf);
    AssertPtrReturn(pvTmpBuf, VERR_NO_MEMORY);

    int  rc = VINF_SUCCESS;
    while (cbCopy > 0)
    {
        uint32_t const cbToCopy = RT_MIN(cbTmpBuf, cbCopy);

        rc = vmsvgaR3GboRead(pSvgaR3State, pGboSrc, offSrc, pvTmpBuf, cbToCopy);
        AssertRCBreak(rc);

        rc = vmsvgaR3GboWrite(pSvgaR3State, pGboDst, offDst, pvTmpBuf, cbToCopy);
        AssertRCBreak(rc);

        offSrc += cbToCopy;
        offDst += cbToCopy;
        cbCopy -= cbToCopy;
    }

    RTMemTmpFree(pvTmpBuf);
    return rc;
}


/*
 *
 * Object Tables.
 *
 */

static int vmsvgaR3OTableSetOrGrow(PVMSVGAR3STATE pSvgaR3State, SVGAOTableType type, PPN64 baseAddress,
                                   uint32_t sizeInBytes, uint32 validSizeInBytes, SVGAMobFormat ptDepth, bool fGrow)
{
    ASSERT_GUEST_RETURN(type < RT_ELEMENTS(pSvgaR3State->aGboOTables), VERR_INVALID_PARAMETER);
    ASSERT_GUEST_RETURN(sizeInBytes >= validSizeInBytes, VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();

    ASSERT_GUEST_RETURN(pSvgaR3State->aGboOTables[type].cbTotal >= validSizeInBytes, VERR_INVALID_PARAMETER);

    if (sizeInBytes > 0)
    {
        /* Create a new guest backed object for the object table. */
        VMSVGAGBO gbo;
        int rc = vmsvgaR3GboCreate(pSvgaR3State, ptDepth, baseAddress, sizeInBytes, &gbo);
        AssertRCReturn(rc, rc);

        /* If the guest sets a new OTable (fGrow == false), then it has already copied the valid data to the new GBO. */
        if (fGrow && validSizeInBytes)
        {
            /* Copy data from old gbo to the new one. */
            rc = vmsvgaR3GboCopy(pSvgaR3State, &gbo, 0, &pSvgaR3State->aGboOTables[type], 0, validSizeInBytes);
            AssertRCReturnStmt(rc, vmsvgaR3GboDestroy(pSvgaR3State, &gbo), rc);
        }

        vmsvgaR3GboDestroy(pSvgaR3State, &pSvgaR3State->aGboOTables[type]);
        pSvgaR3State->aGboOTables[type] = gbo;

    }
    else
        vmsvgaR3GboDestroy(pSvgaR3State, &pSvgaR3State->aGboOTables[type]);

    return VINF_SUCCESS;
}


static int vmsvgaR3OTableVerifyIndex(PVMSVGAR3STATE pSvgaR3State, PVMSVGAGBO pGboOTable,
                                     uint32_t idx, uint32_t cbEntry)
{
    RT_NOREF(pSvgaR3State);

    /* The table must exist and the index must be within the table. */
    ASSERT_GUEST_RETURN(VMSVGA_IS_GBO_CREATED(pGboOTable), VERR_INVALID_PARAMETER);
    ASSERT_GUEST_RETURN(idx < pGboOTable->cbTotal / cbEntry, VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();
    return VINF_SUCCESS;
}


static int vmsvgaR3OTableRead(PVMSVGAR3STATE pSvgaR3State, PVMSVGAGBO pGboOTable,
                              uint32_t idx, uint32_t cbEntry,
                              void *pvData, uint32_t cbData)
{
    AssertReturn(cbData <= cbEntry, VERR_INVALID_PARAMETER);

    int rc = vmsvgaR3OTableVerifyIndex(pSvgaR3State, pGboOTable, idx, cbEntry);
    if (RT_SUCCESS(rc))
    {
        uint32_t const off = idx * cbEntry;
        rc = vmsvgaR3GboRead(pSvgaR3State, pGboOTable, off, pvData, cbData);
    }
    return rc;
}

static int vmsvgaR3OTableWrite(PVMSVGAR3STATE pSvgaR3State, PVMSVGAGBO pGboOTable,
                               uint32_t idx, uint32_t cbEntry,
                               void const *pvData, uint32_t cbData)
{
    AssertReturn(cbData <= cbEntry, VERR_INVALID_PARAMETER);

    int rc = vmsvgaR3OTableVerifyIndex(pSvgaR3State, pGboOTable, idx, cbEntry);
    if (RT_SUCCESS(rc))
    {
        uint32_t const off = idx * cbEntry;
        rc = vmsvgaR3GboWrite(pSvgaR3State, pGboOTable, off, pvData, cbData);
    }
    return rc;
}


int vmsvgaR3OTableReadSurface(PVMSVGAR3STATE pSvgaR3State, uint32_t sid, SVGAOTableSurfaceEntry *pEntrySurface)
{
    return vmsvgaR3OTableRead(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SURFACE],
                              sid, SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, pEntrySurface, sizeof(SVGAOTableSurfaceEntry));
}


/*
 *
 * The guest's Memory OBjects (MOB).
 *
 */

static int vmsvgaR3MobCreate(PVMSVGAR3STATE pSvgaR3State,
                             SVGAMobFormat ptDepth, PPN64 baseAddress, uint32_t sizeInBytes, SVGAMobId mobid,
                             PVMSVGAMOB pMob)
{
    RT_ZERO(*pMob);

    /* Update the entry in the pSvgaR3State->pGboOTableMob. */
    SVGAOTableMobEntry entry;
    entry.ptDepth     = ptDepth;
    entry.sizeInBytes = sizeInBytes;
    entry.base        = baseAddress;
    int rc = vmsvgaR3OTableWrite(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_MOB],
                                 mobid, SVGA3D_OTABLE_MOB_ENTRY_SIZE, &entry, sizeof(entry));
    if (RT_SUCCESS(rc))
    {
        /* Create the corresponding GBO. */
        rc = vmsvgaR3GboCreate(pSvgaR3State, ptDepth, baseAddress, sizeInBytes, &pMob->Gbo);
        if (RT_SUCCESS(rc))
        {
            /* If a mob with this id already exists, then delete it. */
            PVMSVGAMOB pOldMob = (PVMSVGAMOB)RTAvlU32Remove(&pSvgaR3State->MOBTree, mobid);
            if (pOldMob)
            {
                /* This should not happen. */
                ASSERT_GUEST_FAILED();
                RTListNodeRemove(&pOldMob->nodeLRU);
                vmsvgaR3GboDestroy(pSvgaR3State, &pOldMob->Gbo);
                RTMemFree(pOldMob);
            }

            /* Add to the tree of known MOBs and the LRU list. */
            pMob->Core.Key = mobid;
            if (RTAvlU32Insert(&pSvgaR3State->MOBTree, &pMob->Core))
            {
                RTListPrepend(&pSvgaR3State->MOBLRUList, &pMob->nodeLRU);
                return VINF_SUCCESS;
            }

            AssertFailedStmt(rc = VERR_INVALID_STATE);
            vmsvgaR3GboDestroy(pSvgaR3State, &pMob->Gbo);
        }
    }

    return rc;
}


static void vmsvgaR3MobFree(PVMSVGAR3STATE pSvgaR3State, PVMSVGAMOB pMob)
{
    vmsvgaR3GboDestroy(pSvgaR3State, &pMob->Gbo);
    RTMemFree(pMob);
}


static int vmsvgaR3MobDestroy(PVMSVGAR3STATE pSvgaR3State, SVGAMobId mobid)
{
    /* Update the entry in the pSvgaR3State->pGboOTableMob. */
    SVGAOTableMobEntry entry;
    RT_ZERO(entry);
    vmsvgaR3OTableWrite(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_MOB],
                        mobid, SVGA3D_OTABLE_MOB_ENTRY_SIZE, &entry, sizeof(entry));

    PVMSVGAMOB pMob = (PVMSVGAMOB)RTAvlU32Remove(&pSvgaR3State->MOBTree, mobid);
    if (pMob)
    {
        RTListNodeRemove(&pMob->nodeLRU);
        vmsvgaR3MobFree(pSvgaR3State, pMob);
        return VINF_SUCCESS;
    }

    return VERR_INVALID_PARAMETER;
}


PVMSVGAMOB vmsvgaR3MobGet(PVMSVGAR3STATE pSvgaR3State, SVGAMobId RT_UNTRUSTED_GUEST mobid)
{
    if (mobid == SVGA_ID_INVALID)
        return NULL;

    PVMSVGAMOB pMob = (PVMSVGAMOB)RTAvlU32Get(&pSvgaR3State->MOBTree, mobid);
    if (pMob)
    {
        /* Move to the head of the LRU list. */
        RTListNodeRemove(&pMob->nodeLRU);
        RTListPrepend(&pSvgaR3State->MOBLRUList, &pMob->nodeLRU);
    }
    else
        ASSERT_GUEST_FAILED();

    return pMob;
}


int vmsvgaR3MobWrite(PVMSVGAR3STATE pSvgaR3State, PVMSVGAMOB pMob,
                     uint32_t off, void const *pvData, uint32_t cbData)
{
    return vmsvgaR3GboWrite(pSvgaR3State, &pMob->Gbo, off, pvData, cbData);
}


int vmsvgaR3MobRead(PVMSVGAR3STATE pSvgaR3State, PVMSVGAMOB pMob,
                    uint32_t off, void *pvData, uint32_t cbData)
{
    return vmsvgaR3GboWrite(pSvgaR3State, &pMob->Gbo, off, pvData, cbData);
}


/** Create a host ring-3 pointer to the MOB data.
 * Current approach is to allocate a host memory buffer and copy the guest MOB data if necessary.
 * @param pSvgaR3State R3 device state.
 * @param pMob     The MOB.
 * @param cbValid  How many bytes of the guest backing memory contain valid data.
 * @return VBox status.
 */
/** @todo uint32_t cbValid -> uint32_t offStart, uint32_t cbData */
int vmsvgaR3MobBackingStoreCreate(PVMSVGAR3STATE pSvgaR3State, PVMSVGAMOB pMob, uint32_t cbValid)
{
    AssertReturn(pMob, VERR_INVALID_PARAMETER);
    return vmsvgaR3GboBackingStoreCreate(pSvgaR3State, &pMob->Gbo, cbValid);
}


void vmsvgaR3MobBackingStoreDelete(PVMSVGAR3STATE pSvgaR3State, PVMSVGAMOB pMob)
{
    if (pMob)
        vmsvgaR3GboBackingStoreDelete(pSvgaR3State, &pMob->Gbo);
}


int vmsvgaR3MobBackingStoreWriteToGuest(PVMSVGAR3STATE pSvgaR3State, PVMSVGAMOB pMob)
{
    if (pMob)
        return vmsvgaR3GboBackingStoreWriteToGuest(pSvgaR3State, &pMob->Gbo);
    return VERR_INVALID_PARAMETER;
}


int vmsvgaR3MobBackingStoreReadFromGuest(PVMSVGAR3STATE pSvgaR3State, PVMSVGAMOB pMob)
{
    if (pMob)
        return vmsvgaR3GboBackingStoreReadFromGuest(pSvgaR3State, &pMob->Gbo);
    return VERR_INVALID_PARAMETER;
}


void *vmsvgaR3MobBackingStorePtr(PVMSVGAMOB pMob, uint32_t off)
{
    if (pMob && (pMob->Gbo.fGboFlags & VMSVGAGBO_F_HOST_BACKED))
    {
        if (off <= pMob->Gbo.cbTotal)
            return (uint8_t *)pMob->Gbo.pvHost + off;
    }
    return NULL;
}


static DECLCALLBACK(int) vmsvgaR3MobFreeCb(PAVLU32NODECORE pNode, void *pvUser)
{
    PVMSVGAMOB pMob = (PVMSVGAMOB)pNode;
    PVMSVGAR3STATE pSvgaR3State = (PVMSVGAR3STATE)pvUser;
    vmsvgaR3MobFree(pSvgaR3State, pMob);
    return 0;
}


#endif /* VBOX_WITH_VMSVGA3D */



void vmsvgaR3ResetSvgaState(PVGASTATE pThis, PVGASTATECC pThisCC)
{
#ifdef VBOX_WITH_VMSVGA3D
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pThis);

    RTAvlU32Destroy(&pSvgaR3State->MOBTree, vmsvgaR3MobFreeCb, pSvgaR3State);
    RTListInit(&pSvgaR3State->MOBLRUList);

    for (unsigned i = 0; i < RT_ELEMENTS(pSvgaR3State->aGboOTables); ++i)
        vmsvgaR3GboDestroy(pSvgaR3State, &pSvgaR3State->aGboOTables[i]);
#else
    RT_NOREF(pThis, pThisCC);
#endif
}


void vmsvgaR3TerminateSvgaState(PVGASTATE pThis, PVGASTATECC pThisCC)
{
    vmsvgaR3ResetSvgaState(pThis, pThisCC);
}


/*
 * Screen objects.
 */
VMSVGASCREENOBJECT *vmsvgaR3GetScreenObject(PVGASTATECC pThisCC, uint32_t idScreen)
{
    PVMSVGAR3STATE pSVGAState = pThisCC->svga.pSvgaR3State;
    if (   idScreen < (uint32_t)RT_ELEMENTS(pSVGAState->aScreens)
        && pSVGAState
        && pSVGAState->aScreens[idScreen].fDefined)
    {
        Assert(pSVGAState->aScreens[idScreen].idScreen == idScreen);
        return &pSVGAState->aScreens[idScreen];
    }
    return NULL;
}


int vmsvgaR3DestroyScreen(PVGASTATE pThis, PVGASTATECC pThisCC, VMSVGASCREENOBJECT *pScreen)
{
    pScreen->fModified = true;
    pScreen->fDefined  = false;

    /* Notify frontend that the screen is about to be deleted. */
    vmsvgaR3ChangeMode(pThis, pThisCC);

#ifdef VBOX_WITH_VMSVGA3D
    if (RT_LIKELY(pThis->svga.f3DEnabled))
        vmsvga3dDestroyScreen(pThisCC, pScreen);
#endif

    RTMemFree(pScreen->pvScreenBitmap);
    pScreen->pvScreenBitmap = NULL;

    return VINF_SUCCESS;
}


void vmsvgaR3ResetScreens(PVGASTATE pThis, PVGASTATECC pThisCC)
{
    for (uint32_t idScreen = 0; idScreen < (uint32_t)RT_ELEMENTS(pThisCC->svga.pSvgaR3State->aScreens); ++idScreen)
    {
        VMSVGASCREENOBJECT *pScreen = vmsvgaR3GetScreenObject(pThisCC, idScreen);
        if (pScreen)
            vmsvgaR3DestroyScreen(pThis, pThisCC, pScreen);
    }
}


/**
 * Copy a rectangle of pixels within guest VRAM.
 */
static void vmsvgaR3RectCopy(PVGASTATECC pThisCC, VMSVGASCREENOBJECT const *pScreen, uint32_t srcX, uint32_t srcY,
                             uint32_t dstX, uint32_t dstY, uint32_t width, uint32_t height, unsigned cbFrameBuffer)
{
    if (!width || !height)
        return; /* Nothing to do, don't even bother. */

    /*
     * The guest VRAM (aka GFB) is considered to be a bitmap in the format
     * corresponding to the current display mode.
     */
    uint32_t const  cbPixel = RT_ALIGN(pScreen->cBpp, 8) / 8;
    uint32_t const  cbScanline = pScreen->cbPitch ? pScreen->cbPitch : width * cbPixel;
    uint8_t const   *pSrc;
    uint8_t         *pDst;
    unsigned const  cbRectWidth = width * cbPixel;
    unsigned        uMaxOffset;

    uMaxOffset = (RT_MAX(srcY, dstY) + height) * cbScanline + (RT_MAX(srcX, dstX) + width) * cbPixel;
    if (uMaxOffset >= cbFrameBuffer)
    {
        Log(("Max offset (%u) too big for framebuffer (%u bytes), ignoring!\n", uMaxOffset, cbFrameBuffer));
        return; /* Just don't listen to a bad guest. */
    }

    pSrc = pDst = pThisCC->pbVRam;
    pSrc += srcY * cbScanline + srcX * cbPixel;
    pDst += dstY * cbScanline + dstX * cbPixel;

    if (srcY >= dstY)
    {
        /* Source below destination, copy top to bottom. */
        for (; height > 0; height--)
        {
            memmove(pDst, pSrc, cbRectWidth);
            pSrc += cbScanline;
            pDst += cbScanline;
        }
    }
    else
    {
        /* Source above destination, copy bottom to top. */
        pSrc += cbScanline * (height - 1);
        pDst += cbScanline * (height - 1);
        for (; height > 0; height--)
        {
            memmove(pDst, pSrc, cbRectWidth);
            pSrc -= cbScanline;
            pDst -= cbScanline;
        }
    }
}


/**
 * Common worker for changing the pointer shape.
 *
 * @param   pThisCC             The VGA/VMSVGA state for ring-3.
 * @param   pSVGAState          The VMSVGA ring-3 instance data.
 * @param   fAlpha              Whether there is alpha or not.
 * @param   xHot                Hotspot x coordinate.
 * @param   yHot                Hotspot y coordinate.
 * @param   cx                  Width.
 * @param   cy                  Height.
 * @param   pbData              Heap copy of the cursor data.  Consumed.
 * @param   cbData              The size of the data.
 */
static void vmsvgaR3InstallNewCursor(PVGASTATECC pThisCC, PVMSVGAR3STATE pSVGAState, bool fAlpha,
                                     uint32_t xHot, uint32_t yHot, uint32_t cx, uint32_t cy, uint8_t *pbData, uint32_t cbData)
{
    LogRel2(("vmsvgaR3InstallNewCursor: cx=%d cy=%d xHot=%d yHot=%d fAlpha=%d cbData=%#x\n", cx, cy, xHot, yHot, fAlpha, cbData));
#ifdef LOG_ENABLED
    if (LogIs2Enabled())
    {
        uint32_t cbAndLine = RT_ALIGN(cx, 8) / 8;
        if (!fAlpha)
        {
            Log2(("VMSVGA Cursor AND mask (%d,%d):\n", cx, cy));
            for (uint32_t y = 0; y < cy; y++)
            {
                Log2(("%3u:", y));
                uint8_t const *pbLine = &pbData[y * cbAndLine];
                for (uint32_t x = 0; x < cx; x += 8)
                {
                    uint8_t   b = pbLine[x / 8];
                    char      szByte[12];
                    szByte[0] = b & 0x80 ? '*' : ' '; /* most significant bit first */
                    szByte[1] = b & 0x40 ? '*' : ' ';
                    szByte[2] = b & 0x20 ? '*' : ' ';
                    szByte[3] = b & 0x10 ? '*' : ' ';
                    szByte[4] = b & 0x08 ? '*' : ' ';
                    szByte[5] = b & 0x04 ? '*' : ' ';
                    szByte[6] = b & 0x02 ? '*' : ' ';
                    szByte[7] = b & 0x01 ? '*' : ' ';
                    szByte[8] = '\0';
                    Log2(("%s", szByte));
                }
                Log2(("\n"));
            }
        }

        Log2(("VMSVGA Cursor XOR mask (%d,%d):\n", cx, cy));
        uint32_t const *pu32Xor = (uint32_t const *)&pbData[RT_ALIGN_32(cbAndLine * cy, 4)];
        for (uint32_t y = 0; y < cy; y++)
        {
            Log2(("%3u:", y));
            uint32_t const *pu32Line = &pu32Xor[y * cx];
            for (uint32_t x = 0; x < cx; x++)
                Log2((" %08x", pu32Line[x]));
            Log2(("\n"));
        }
    }
#endif

    int rc = pThisCC->pDrv->pfnVBVAMousePointerShape(pThisCC->pDrv, true /*fVisible*/, fAlpha, xHot, yHot, cx, cy, pbData);
    AssertRC(rc);

    if (pSVGAState->Cursor.fActive)
        RTMemFreeZ(pSVGAState->Cursor.pData, pSVGAState->Cursor.cbData);

    pSVGAState->Cursor.fActive  = true;
    pSVGAState->Cursor.xHotspot = xHot;
    pSVGAState->Cursor.yHotspot = yHot;
    pSVGAState->Cursor.width    = cx;
    pSVGAState->Cursor.height   = cy;
    pSVGAState->Cursor.cbData   = cbData;
    pSVGAState->Cursor.pData    = pbData;
}


#ifdef VBOX_WITH_VMSVGA3D

/*
 * SVGA_3D_CMD_* handlers.
 */


/** SVGA_3D_CMD_SURFACE_DEFINE 1040, SVGA_3D_CMD_SURFACE_DEFINE_V2 1070
 *
 * @param pThisCC         The VGA/VMSVGA state for the current context.
 * @param pCmd            The VMSVGA command.
 * @param cMipLevelSizes  Number of elements in the paMipLevelSizes array.
 * @param paMipLevelSizes Arrays of surface sizes for each face and miplevel.
 */
static void vmsvga3dCmdDefineSurface(PVGASTATECC pThisCC, SVGA3dCmdDefineSurface_v2 const *pCmd,
                                     uint32_t cMipLevelSizes, SVGA3dSize *paMipLevelSizes)
{
    ASSERT_GUEST_RETURN_VOID(pCmd->sid < SVGA3D_MAX_SURFACE_IDS);
    ASSERT_GUEST_RETURN_VOID(cMipLevelSizes >= 1);
    RT_UNTRUSTED_VALIDATED_FENCE();

    /* Number of faces (cFaces) is specified as the number of the first non-zero elements in the 'face' array.
     * Since only plain surfaces (cFaces == 1) and cubemaps (cFaces == 6) are supported
     * (see also SVGA3dCmdDefineSurface definition in svga3d_reg.h), we ignore anything else.
     */
    uint32_t cRemainingMipLevels = cMipLevelSizes;
    uint32_t cFaces = 0;
    for (uint32_t i = 0; i < SVGA3D_MAX_SURFACE_FACES; ++i)
    {
        if (pCmd->face[i].numMipLevels == 0)
            break;

        /* All SVGA3dSurfaceFace structures must have the same value of numMipLevels field */
        ASSERT_GUEST_RETURN_VOID(pCmd->face[i].numMipLevels == pCmd->face[0].numMipLevels);

        /* numMipLevels value can't be greater than the number of remaining elements in the paMipLevelSizes array. */
        ASSERT_GUEST_RETURN_VOID(pCmd->face[i].numMipLevels <= cRemainingMipLevels);
        cRemainingMipLevels -= pCmd->face[i].numMipLevels;

        ++cFaces;
    }
    for (uint32_t i = cFaces; i < SVGA3D_MAX_SURFACE_FACES; ++i)
        ASSERT_GUEST_RETURN_VOID(pCmd->face[i].numMipLevels == 0);

    /* cFaces must be 6 for a cubemap and 1 otherwise. */
    ASSERT_GUEST_RETURN_VOID(cFaces == (uint32_t)((pCmd->surfaceFlags & SVGA3D_SURFACE_CUBEMAP) ? 6 : 1));

    /* Sum of face[i].numMipLevels must be equal to cMipLevels. */
    ASSERT_GUEST_RETURN_VOID(cRemainingMipLevels == 0);
    RT_UNTRUSTED_VALIDATED_FENCE();

    /* Verify paMipLevelSizes */
    uint32_t cWidth  = paMipLevelSizes[0].width;
    uint32_t cHeight = paMipLevelSizes[0].height;
    uint32_t cDepth  = paMipLevelSizes[0].depth;
    for (uint32_t i = 1; i < pCmd->face[0].numMipLevels; ++i)
    {
        cWidth >>= 1;
        if (cWidth == 0) cWidth = 1;
        cHeight >>= 1;
        if (cHeight == 0) cHeight = 1;
        cDepth >>= 1;
        if (cDepth == 0) cDepth = 1;
        for (uint32_t iFace = 0; iFace < cFaces; ++iFace)
        {
            uint32_t const iMipLevelSize = iFace * pCmd->face[0].numMipLevels + i;
            ASSERT_GUEST_RETURN_VOID(   cWidth  == paMipLevelSizes[iMipLevelSize].width
                                     && cHeight == paMipLevelSizes[iMipLevelSize].height
                                     && cDepth  == paMipLevelSizes[iMipLevelSize].depth);
        }
    }
    RT_UNTRUSTED_VALIDATED_FENCE();

    /* Create the surface. */
    SVGA3dMSPattern const multisamplePattern = pCmd->multisampleCount > 1 ? SVGA3D_MS_PATTERN_STANDARD : SVGA3D_MS_PATTERN_NONE;
    SVGA3dMSQualityLevel const qualityLevel = pCmd->multisampleCount > 1 ? SVGA3D_MS_QUALITY_FULL : SVGA3D_MS_QUALITY_NONE;
    vmsvga3dSurfaceDefine(pThisCC, pCmd->sid, pCmd->surfaceFlags, pCmd->format,
                          pCmd->multisampleCount, multisamplePattern, qualityLevel, pCmd->autogenFilter,
                          pCmd->face[0].numMipLevels, &paMipLevelSizes[0], /* arraySize = */ 0, /* bufferByteStride = */ 0, /* fAllocMipLevels = */ true);
}


/* SVGA_3D_CMD_SET_OTABLE_BASE 1091 */
static void vmsvga3dCmdSetOTableBase(PVGASTATECC pThisCC, SVGA3dCmdSetOTableBase const *pCmd)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    vmsvgaR3OTableSetOrGrow(pSvgaR3State, pCmd->type, pCmd->baseAddress,
                            pCmd->sizeInBytes, pCmd->validSizeInBytes, pCmd->ptDepth, /*fGrow*/ false);
}


/* SVGA_3D_CMD_DEFINE_GB_MOB 1093 */
static void vmsvga3dCmdDefineGBMob(PVGASTATECC pThisCC, SVGA3dCmdDefineGBMob const *pCmd)
{
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    ASSERT_GUEST_RETURN_VOID(pCmd->mobid != SVGA_ID_INVALID); /* The guest should not use this id. */

    /* Maybe just update the OTable and create Gbo when the MOB is actually accessed? */
    /* Allocate a structure for the MOB. */
    PVMSVGAMOB pMob = (PVMSVGAMOB)RTMemAllocZ(sizeof(*pMob));
    AssertPtrReturnVoid(pMob);

    int rc = vmsvgaR3MobCreate(pSvgaR3State, pCmd->ptDepth, pCmd->base, pCmd->sizeInBytes, pCmd->mobid, pMob);
    if (RT_SUCCESS(rc))
    {
        return;
    }

    AssertFailed();

    RTMemFree(pMob);
}


/* SVGA_3D_CMD_DESTROY_GB_MOB 1094 */
static void vmsvga3dCmdDestroyGBMob(PVGASTATECC pThisCC, SVGA3dCmdDestroyGBMob const *pCmd)
{
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    ASSERT_GUEST_RETURN_VOID(pCmd->mobid != SVGA_ID_INVALID); /* The guest should not use this id. */

    int rc = vmsvgaR3MobDestroy(pSvgaR3State, pCmd->mobid);
    if (RT_SUCCESS(rc))
    {
        return;
    }

    AssertFailed();
}


/* SVGA_3D_CMD_DEFINE_GB_SURFACE 1097 */
static void vmsvga3dCmdDefineGBSurface(PVGASTATECC pThisCC, SVGA3dCmdDefineGBSurface const *pCmd)
{
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    /* Update the entry in the pSvgaR3State->pGboOTableSurface. */
    SVGAOTableSurfaceEntry entry;
    RT_ZERO(entry);
    entry.format = pCmd->format;
    entry.surface1Flags = pCmd->surfaceFlags;
    entry.numMipLevels = pCmd->numMipLevels;
    entry.multisampleCount = pCmd->multisampleCount;
    entry.autogenFilter = pCmd->autogenFilter;
    entry.size = pCmd->size;
    entry.mobid = SVGA_ID_INVALID;
    // entry.arraySize = 0;
    // entry.mobPitch = 0;
    // entry.surface2Flags = 0;
    // entry.multisamplePattern = 0;
    // entry.qualityLevel = 0;
    // entry.bufferByteStride = 0;
    // entry.minLOD = 0;
    int rc = vmsvgaR3OTableWrite(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SURFACE],
                                 pCmd->sid, SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, &entry, sizeof(entry));
    if (RT_SUCCESS(rc))
    {
        /* Create the host surface. */
        SVGA3dMSPattern const multisamplePattern = pCmd->multisampleCount > 1 ? SVGA3D_MS_PATTERN_STANDARD : SVGA3D_MS_PATTERN_NONE;
        SVGA3dMSQualityLevel const qualityLevel = pCmd->multisampleCount > 1 ? SVGA3D_MS_QUALITY_FULL : SVGA3D_MS_QUALITY_NONE;
        vmsvga3dSurfaceDefine(pThisCC, pCmd->sid, pCmd->surfaceFlags, pCmd->format,
                              pCmd->multisampleCount, multisamplePattern, qualityLevel, pCmd->autogenFilter,
                              pCmd->numMipLevels, &pCmd->size, /* arraySize = */ 0, /* bufferByteStride = */ 0, /* fAllocMipLevels = */ false);
    }
}


/* SVGA_3D_CMD_DESTROY_GB_SURFACE 1098 */
static void vmsvga3dCmdDestroyGBSurface(PVGASTATECC pThisCC, SVGA3dCmdDestroyGBSurface const *pCmd)
{
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    /* Update the entry in the pSvgaR3State->pGboOTableSurface. */
    SVGAOTableSurfaceEntry entry;
    RT_ZERO(entry);
    entry.mobid = SVGA_ID_INVALID;
    vmsvgaR3OTableWrite(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SURFACE],
                        pCmd->sid, SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, &entry, sizeof(entry));

    vmsvga3dSurfaceDestroy(pThisCC, pCmd->sid);
}


/* SVGA_3D_CMD_BIND_GB_SURFACE 1099 */
static void vmsvga3dCmdBindGBSurface(PVGASTATECC pThisCC, SVGA3dCmdBindGBSurface const *pCmd)
{
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    /* Assign the mobid to the surface. */
    int rc = VINF_SUCCESS;
    if (pCmd->mobid != SVGA_ID_INVALID)
        rc = vmsvgaR3OTableVerifyIndex(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_MOB],
                                       pCmd->mobid, SVGA3D_OTABLE_MOB_ENTRY_SIZE);
    if (RT_SUCCESS(rc))
    {
        SVGAOTableSurfaceEntry entry;
        rc = vmsvgaR3OTableRead(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SURFACE],
                                pCmd->sid, SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, &entry, sizeof(entry));
        if (RT_SUCCESS(rc))
        {
            entry.mobid = pCmd->mobid;
            rc = vmsvgaR3OTableWrite(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SURFACE],
                                     pCmd->sid, SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, &entry, sizeof(entry));
            if (RT_SUCCESS(rc))
            {
                /* */
            }
        }
    }
}


typedef union
{
    float f;
    uint32_t u;
} Unsigned2Float;

float float16ToFloat(uint16_t f16)
{
    /* Format specs from Wiki: [15] = sign, [14:10] = exponent, [9:0] = fraction */
    uint16_t const f = f16 & 0x3FF;
    uint16_t const e = (f16 >> 10) & 0x1F;
    uint16_t const s = (f16 >> 15) & 0x1;
    Unsigned2Float u2f;

    if (e == 0)
    {
        if (f == 0)
        {
            /* zero, -0 */
            u2f.u = (s << 31) | (0 << 23) | 0;
            return u2f.f;
        }

        /* subnormal numbers: (-1)^signbit * 2^-14 * 0.significantbits */
        float const k = 1.0f / 16384.0f; /* 2^-14 */
        return (s ? -1.0f : 1.0f) * k * (float)f / 1024.0f;
    }

    if (e == 31)
    {
        if (f == 0)
        {
            /* +-infinity */
            u2f.u = (s << 31) | (0xFF << 23) | 0;
            return u2f.f;
        }

        /* NaN */
        u2f.u = (s << 31) | (0xFF << 23) | 1;
        return u2f.f;
    }

    /* normalized value: (-1)^signbit * 2^(exponent - 15) * 1.significantbits */
    /* Build the float, adjusting for exponent bias (float32 bias is 127, float16 is 15)
     * and number of bits in the fraction (float32 has 23, float16 has 10). */
    u2f.u = (s << 31) | ((e + 127 - 15) << 23) | (f << (23 - 10));
    return u2f.f;
}


static int vmsvga3dBmpWrite(const char *pszFilename, VMSVGA3D_MAPPED_SURFACE const *pMap)
{
    if (   pMap->cbBlock != 4 && pMap->cbBlock != 2 && pMap->cbBlock != 1
        && pMap->format != SVGA3D_R16G16B16A16_FLOAT
        && pMap->format != SVGA3D_R32G32B32A32_FLOAT)
        return VERR_NOT_SUPPORTED;

    int const w = pMap->cbRow / pMap->cbBlock;
    int const h = pMap->cRows;

    int const cbBitmap = pMap->cbRow * pMap->cRows;
    int const cBits = (   pMap->format == SVGA3D_R16G16B16A16_FLOAT
                       || pMap->format == SVGA3D_R32G32B32A32_FLOAT)
                    ? 32
                    : pMap->cbBlock * 8;

    FILE *f = fopen(pszFilename, "wb");
    if (!f)
        return VERR_FILE_NOT_FOUND;

    /* Always write 32 bit bitmap which can be displayed. */
#ifdef RT_OS_WINDOWS
    if (cBits == 32)
    {
        BMPFILEHDR fileHdr;
        RT_ZERO(fileHdr);
        fileHdr.uType       = BMP_HDR_MAGIC;
        fileHdr.cbFileSize = sizeof(fileHdr) + sizeof(BITMAPV4HEADER) + cbBitmap;
        fileHdr.offBits    = sizeof(fileHdr) + sizeof(BITMAPV4HEADER);

        BITMAPV4HEADER hdrV4;
        RT_ZERO(hdrV4);
        hdrV4.bV4Size          = sizeof(hdrV4);
        hdrV4.bV4Width         = w;
        hdrV4.bV4Height        = -h;
        hdrV4.bV4Planes        = 1;
        hdrV4.bV4BitCount      = 32;
        hdrV4.bV4V4Compression = BI_BITFIELDS;
        hdrV4.bV4SizeImage     = cbBitmap;
        hdrV4.bV4XPelsPerMeter = 2835;
        hdrV4.bV4YPelsPerMeter = 2835;
        // hdrV4.bV4ClrUsed       = 0;
        // hdrV4.bV4ClrImportant  = 0;
        hdrV4.bV4RedMask       = 0x00ff0000;
        hdrV4.bV4GreenMask     = 0x0000ff00;
        hdrV4.bV4BlueMask      = 0x000000ff;
        hdrV4.bV4AlphaMask     = 0xff000000;
        hdrV4.bV4CSType        = LCS_WINDOWS_COLOR_SPACE;
        // hdrV4.bV4Endpoints     = {0};
        // hdrV4.bV4GammaRed      = 0;
        // hdrV4.bV4GammaGreen    = 0;
        // hdrV4.bV4GammaBlue     = 0;

        fwrite(&fileHdr, 1, sizeof(fileHdr), f);
        fwrite(&hdrV4, 1, sizeof(hdrV4), f);
    }
    else
#else
    RT_NOREF(cBits);
#endif
    {
        BMPFILEHDR fileHdr;
        RT_ZERO(fileHdr);
        fileHdr.uType      = BMP_HDR_MAGIC;
        fileHdr.cbFileSize = sizeof(BMPFILEHDR) + sizeof(BMPWIN3XINFOHDR) + cbBitmap;
        fileHdr.offBits    = sizeof(BMPFILEHDR) + sizeof(BMPWIN3XINFOHDR);

        BMPWIN3XINFOHDR coreHdr;
        RT_ZERO(coreHdr);
        coreHdr.cbSize      = sizeof(coreHdr);
        coreHdr.uWidth      = w;
        coreHdr.uHeight     = -h;
        coreHdr.cPlanes     = 1;
        coreHdr.cBits       = 32;
        coreHdr.cbSizeImage = cbBitmap;

        fwrite(&fileHdr, 1, sizeof(fileHdr), f);
        fwrite(&coreHdr, 1, sizeof(coreHdr), f);
    }

    if (pMap->format == SVGA3D_R16G16B16A16_FLOAT)
    {
        const uint8_t *s = (uint8_t *)pMap->pvData;
        for (int32_t y = 0; y < h; ++y)
        {
            for (int32_t x = 0; x < w; ++x)
            {
                uint16_t const *pu16Pixel = (uint16_t *)(s + x * 8);
                uint8_t r = (uint8_t)(255.0 * float16ToFloat(pu16Pixel[0]));
                uint8_t g = (uint8_t)(255.0 * float16ToFloat(pu16Pixel[1]));
                uint8_t b = (uint8_t)(255.0 * float16ToFloat(pu16Pixel[2]));
                uint8_t a = (uint8_t)(255.0 * float16ToFloat(pu16Pixel[3]));
                uint32_t u32Pixel = b + (g << 8) + (r << 16) + (a << 24);
                fwrite(&u32Pixel, 1, 4, f);
            }

            s += pMap->cbRowPitch;
        }
    }
    else if (pMap->format == SVGA3D_R32G32B32A32_FLOAT)
    {
        const uint8_t *s = (uint8_t *)pMap->pvData;
        for (int32_t y = 0; y < h; ++y)
        {
            for (int32_t x = 0; x < w; ++x)
            {
                float const *pPixel = (float *)(s + x * 8);
                uint8_t r = (uint8_t)(255.0 * pPixel[0]);
                uint8_t g = (uint8_t)(255.0 * pPixel[1]);
                uint8_t b = (uint8_t)(255.0 * pPixel[2]);
                uint8_t a = (uint8_t)(255.0 * pPixel[3]);
                uint32_t u32Pixel = b + (g << 8) + (r << 16) + (a << 24);
                fwrite(&u32Pixel, 1, 4, f);
            }

            s += pMap->cbRowPitch;
        }
    }
    else if (pMap->cbBlock == 4)
    {
        const uint8_t *s = (uint8_t *)pMap->pvData;
        for (uint32_t iRow = 0; iRow < pMap->cRows; ++iRow)
        {
            fwrite(s, 1, pMap->cbRow, f);

            s += pMap->cbRowPitch;
        }
    }
    else if (pMap->cbBlock == 2)
    {
        const uint8_t *s = (uint8_t *)pMap->pvData;
        for (uint32_t iRow = 0; iRow < pMap->cRows; ++iRow)
        {
            for (int32_t x = 0; x < w; ++x)
            {
                uint16_t const *pPixel = (uint16_t *)(s + x * sizeof(uint16_t));
                uint32_t u32Pixel = *pPixel;
                fwrite(&u32Pixel, 1, 4, f);
            }

            s += pMap->cbRowPitch;
        }
    }
    else if (pMap->cbBlock == 1)
    {
        const uint8_t *s = (uint8_t *)pMap->pvData;
        for (uint32_t iRow = 0; iRow < pMap->cRows; ++iRow)
        {
            for (int32_t x = 0; x < w; ++x)
            {
                uint32_t u32Pixel = s[x];
                fwrite(&u32Pixel, 1, 4, f);
            }

            s += pMap->cbRowPitch;
        }
    }

    fclose(f);

    return VINF_SUCCESS;
}


void vmsvga3dMapWriteBmpFile(VMSVGA3D_MAPPED_SURFACE const *pMap, char const *pszPrefix)
{
    static int idxBitmap = 0;
    char *pszFilename = RTStrAPrintf2("bmp" RTPATH_SLASH_STR "%s%d.bmp", pszPrefix, idxBitmap++);
    int rc = vmsvga3dBmpWrite(pszFilename, pMap);
    Log(("WriteBmpFile %s format %d %Rrc\n", pszFilename, pMap->format, rc)); RT_NOREF(rc);
    RTStrFree(pszFilename);
}


static int vmsvgaR3TransferSurfaceLevel(PVGASTATECC pThisCC,
                                        PVMSVGAMOB pMob,
                                        SVGA3dSurfaceImageId const *pImage,
                                        SVGA3dBox const *pBox,
                                        SVGA3dTransferType enmTransfer)
{
    if (vmsvga3dIsMultisampleSurface(pThisCC, pImage->sid))
    {
        /* Multisample surfaces can't be accessed. Skip. */
        return VINF_SUCCESS;
    }

    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    VMSVGA3D_SURFACE_MAP enmMapType;
    if (enmTransfer == SVGA3D_WRITE_HOST_VRAM)
        enmMapType = pBox
                   ? VMSVGA3D_SURFACE_MAP_WRITE
                   : VMSVGA3D_SURFACE_MAP_WRITE_DISCARD;
    else if (enmTransfer == SVGA3D_READ_HOST_VRAM)
        enmMapType = VMSVGA3D_SURFACE_MAP_READ;
    else
        AssertFailedReturn(VERR_INVALID_PARAMETER);

    VMSVGA3D_MAPPED_SURFACE map;
    int rc = vmsvga3dSurfaceMap(pThisCC, pImage, pBox, enmMapType, &map);
    if (RT_SUCCESS(rc))
    {
        /* Copy mapped surface <-> MOB. */
        VMSGA3D_BOX_DIMENSIONS dims;
        rc = vmsvga3dGetBoxDimensions(pThisCC, pImage, pBox, &dims);
        if (RT_SUCCESS(rc))
        {
            for (uint32_t z = 0; z < map.box.d; ++z)
            {
                uint8_t *pu8Map = (uint8_t *)map.pvData + z * map.cbDepthPitch;
                uint32_t offMob = dims.offSubresource + dims.offBox + z * dims.cbDepthPitch;

                for (uint32_t iRow = 0; iRow < map.cRows; ++iRow)
                {
                    if (enmTransfer == SVGA3D_READ_HOST_VRAM)
                        rc = vmsvgaR3GboWrite(pSvgaR3State, &pMob->Gbo, offMob, pu8Map, dims.cbRow);
                    else
                        rc = vmsvgaR3GboRead(pSvgaR3State, &pMob->Gbo, offMob, pu8Map, dims.cbRow);
                    AssertRCBreak(rc);

                    pu8Map += map.cbRowPitch;
                    offMob += dims.cbPitch;
                }
            }
        }

        // vmsvga3dMapWriteBmpFile(&map, "Dynamic");

        bool const fWritten = (enmTransfer == SVGA3D_WRITE_HOST_VRAM);
        vmsvga3dSurfaceUnmap(pThisCC, pImage, &map, fWritten);
    }

    return rc;
}


/* SVGA_3D_CMD_UPDATE_GB_IMAGE 1101 */
static void vmsvga3dCmdUpdateGBImage(PVGASTATECC pThisCC, SVGA3dCmdUpdateGBImage const *pCmd)
{
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    LogFlowFunc(("sid=%u @%u,%u,%u %ux%ux%u\n",
                 pCmd->image.sid, pCmd->box.x, pCmd->box.y, pCmd->box.z, pCmd->box.w, pCmd->box.h, pCmd->box.d));

/*
   SVGA3dSurfaceFormat format;
   SVGA3dSurface1Flags surface1Flags;
   uint32 numMipLevels;
   uint32 multisampleCount;
   SVGA3dTextureFilter autogenFilter;
   SVGA3dSize size;
   SVGAMobId mobid;
   uint32 arraySize;
   uint32 mobPitch;
   SVGA3dSurface2Flags surface2Flags;
   uint8 multisamplePattern;
   uint8 qualityLevel;
   uint16 bufferByteStride;
   float minLOD;
*/

    /* "update a surface from its backing MOB." */
    SVGAOTableSurfaceEntry entrySurface;
    int rc = vmsvgaR3OTableRead(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SURFACE],
                            pCmd->image.sid, SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, &entrySurface, sizeof(entrySurface));
    if (RT_SUCCESS(rc))
    {
        PVMSVGAMOB pMob = vmsvgaR3MobGet(pSvgaR3State, entrySurface.mobid);
        if (pMob)
        {
            rc = vmsvgaR3TransferSurfaceLevel(pThisCC, pMob, &pCmd->image, &pCmd->box, SVGA3D_WRITE_HOST_VRAM);
            AssertRC(rc);
        }
    }
}


/* SVGA_3D_CMD_UPDATE_GB_SURFACE 1102 */
static void vmsvga3dCmdUpdateGBSurface(PVGASTATECC pThisCC, SVGA3dCmdUpdateGBSurface const *pCmd)
{
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    LogFlowFunc(("sid=%u\n",
                 pCmd->sid));

    /* "update a surface from its backing MOB." */
    SVGAOTableSurfaceEntry entrySurface;
    int rc = vmsvgaR3OTableRead(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SURFACE],
                            pCmd->sid, SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, &entrySurface, sizeof(entrySurface));
    if (RT_SUCCESS(rc))
    {
        PVMSVGAMOB pMob = vmsvgaR3MobGet(pSvgaR3State, entrySurface.mobid);
        if (pMob)
        {
            uint32 const arraySize = vmsvga3dGetArrayElements(pThisCC, pCmd->sid);
            for (uint32_t iArray = 0; iArray < arraySize; ++iArray)
            {
                for (uint32_t iMipmap = 0; iMipmap < entrySurface.numMipLevels; ++iMipmap)
                {
                    SVGA3dSurfaceImageId image;
                    image.sid = pCmd->sid;
                    image.face = iArray;
                    image.mipmap = iMipmap;

                    rc = vmsvgaR3TransferSurfaceLevel(pThisCC, pMob, &image, /* all pBox = */ NULL, SVGA3D_WRITE_HOST_VRAM);
                    AssertRCBreak(rc);
                }
            }
        }
    }
}


/* SVGA_3D_CMD_READBACK_GB_IMAGE 1103 */
static void vmsvga3dCmdReadbackGBImage(PVGASTATECC pThisCC, SVGA3dCmdReadbackGBImage const *pCmd)
{
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    LogFlowFunc(("sid=%u, face=%u, mipmap=%u\n",
                 pCmd->image.sid, pCmd->image.face, pCmd->image.mipmap));

    /* Read a surface to its backing MOB. */
    SVGAOTableSurfaceEntry entrySurface;
    int rc = vmsvgaR3OTableRead(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SURFACE],
                            pCmd->image.sid, SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, &entrySurface, sizeof(entrySurface));
    if (RT_SUCCESS(rc))
    {
        PVMSVGAMOB pMob = vmsvgaR3MobGet(pSvgaR3State, entrySurface.mobid);
        if (pMob)
        {
            rc = vmsvgaR3TransferSurfaceLevel(pThisCC, pMob, &pCmd->image, /* all pBox = */ NULL, SVGA3D_READ_HOST_VRAM);
            AssertRC(rc);
        }
    }
}


/* SVGA_3D_CMD_READBACK_GB_SURFACE 1104 */
static void vmsvga3dCmdReadbackGBSurface(PVGASTATECC pThisCC, SVGA3dCmdReadbackGBSurface const *pCmd)
{
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    LogFlowFunc(("sid=%u\n",
                 pCmd->sid));

    /* Read a surface to its backing MOB. */
    SVGAOTableSurfaceEntry entrySurface;
    int rc = vmsvgaR3OTableRead(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SURFACE],
                            pCmd->sid, SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, &entrySurface, sizeof(entrySurface));
    if (RT_SUCCESS(rc))
    {
        PVMSVGAMOB pMob = vmsvgaR3MobGet(pSvgaR3State, entrySurface.mobid);
        if (pMob)
        {
            uint32 const arraySize = vmsvga3dGetArrayElements(pThisCC, pCmd->sid);
            for (uint32_t iArray = 0; iArray < arraySize; ++iArray)
            {
                for (uint32_t iMipmap = 0; iMipmap < entrySurface.numMipLevels; ++iMipmap)
                {
                    SVGA3dSurfaceImageId image;
                    image.sid = pCmd->sid;
                    image.face = iArray;
                    image.mipmap = iMipmap;

                    rc = vmsvgaR3TransferSurfaceLevel(pThisCC, pMob, &image, /* all pBox = */ NULL, SVGA3D_READ_HOST_VRAM);
                    AssertRCBreak(rc);
                }
            }
        }
    }
}


/* SVGA_3D_CMD_INVALIDATE_GB_IMAGE 1105 */
static void vmsvga3dCmdInvalidateGBImage(PVGASTATECC pThisCC, SVGA3dCmdInvalidateGBImage const *pCmd)
{
    //DEBUG_BREAKPOINT_TEST();
    vmsvga3dSurfaceInvalidate(pThisCC, pCmd->image.sid, pCmd->image.face, pCmd->image.mipmap);
}


/* SVGA_3D_CMD_INVALIDATE_GB_SURFACE 1106 */
static void vmsvga3dCmdInvalidateGBSurface(PVGASTATECC pThisCC, SVGA3dCmdInvalidateGBSurface const *pCmd)
{
    //DEBUG_BREAKPOINT_TEST();
    vmsvga3dSurfaceInvalidate(pThisCC, pCmd->sid, SVGA_ID_INVALID, SVGA_ID_INVALID);
}


/* SVGA_3D_CMD_SET_OTABLE_BASE64 1115 */
static void vmsvga3dCmdSetOTableBase64(PVGASTATECC pThisCC, SVGA3dCmdSetOTableBase64 const *pCmd)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    vmsvgaR3OTableSetOrGrow(pSvgaR3State, pCmd->type, pCmd->baseAddress,
                            pCmd->sizeInBytes, pCmd->validSizeInBytes, pCmd->ptDepth, /*fGrow*/ false);
}


/* SVGA_3D_CMD_DEFINE_GB_SCREENTARGET 1124 */
static void vmsvga3dCmdDefineGBScreenTarget(PVGASTATE pThis, PVGASTATECC pThisCC, SVGA3dCmdDefineGBScreenTarget const *pCmd)
{
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    ASSERT_GUEST_RETURN_VOID(pCmd->stid < RT_ELEMENTS(pSvgaR3State->aScreens));
    ASSERT_GUEST_RETURN_VOID(pCmd->width > 0 && pCmd->width <= pThis->svga.u32MaxWidth); /* SVGA_REG_SCREENTARGET_MAX_WIDTH */
    ASSERT_GUEST_RETURN_VOID(pCmd->height > 0 && pCmd->height <= pThis->svga.u32MaxHeight); /* SVGA_REG_SCREENTARGET_MAX_HEIGHT */
    RT_UNTRUSTED_VALIDATED_FENCE();

    /* Update the entry in the pSvgaR3State->pGboOTableScreenTarget. */
    SVGAOTableScreenTargetEntry entry;
    RT_ZERO(entry);
    entry.image.sid = SVGA_ID_INVALID;
    // entry.image.face = 0;
    // entry.image.mipmap = 0;
    entry.width     = pCmd->width;
    entry.height    = pCmd->height;
    entry.xRoot     = pCmd->xRoot;
    entry.yRoot     = pCmd->yRoot;
    entry.flags     = pCmd->flags;
    entry.dpi       = pCmd->dpi;

    int rc = vmsvgaR3OTableWrite(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SCREENTARGET],
                                 pCmd->stid, SVGA3D_OTABLE_SCREEN_TARGET_ENTRY_SIZE, &entry, sizeof(entry));
    if (RT_SUCCESS(rc))
    {
        /* Screen objects and screen targets are similar, therefore we will use the same for both. */
        /** @todo Generic screen object/target interface. */
        VMSVGASCREENOBJECT *pScreen = &pSvgaR3State->aScreens[pCmd->stid];
        Assert(pScreen->idScreen == pCmd->stid);
        pScreen->fDefined  = true;
        pScreen->fModified = true;
        pScreen->fuScreen  = SVGA_SCREEN_MUST_BE_SET
                           | (RT_BOOL(pCmd->flags & SVGA_STFLAG_PRIMARY) ? SVGA_SCREEN_IS_PRIMARY : 0);

        pScreen->xOrigin = pCmd->xRoot;
        pScreen->yOrigin = pCmd->yRoot;
        pScreen->cWidth  = pCmd->width;
        pScreen->cHeight = pCmd->height;
        pScreen->offVRAM = 0; /* Not applicable for screen targets, they use either a separate memory buffer or a host window. */
        pScreen->cbPitch = pCmd->width * 4;
        pScreen->cBpp    = 32;
        pScreen->cDpi    = pCmd->dpi;

        /* The screen bitmap must be deallocated after 'vmsvgaR3ChangeMode'. */
        void *pvOldScreenBitmap = pScreen->pvScreenBitmap;
        pScreen->pvScreenBitmap = 0;

        if (RT_LIKELY(pThis->svga.f3DEnabled))
            vmsvga3dDefineScreen(pThis, pThisCC, pScreen);

        if (!pScreen->pHwScreen)
        {
            /* System memory buffer. */
            pScreen->pvScreenBitmap = RTMemAllocZ(pScreen->cHeight * pScreen->cbPitch);
        }

        pThis->svga.fGFBRegisters = false;
        vmsvgaR3ChangeMode(pThis, pThisCC);

        RTMemFree(pvOldScreenBitmap);
    }
}


/* SVGA_3D_CMD_DESTROY_GB_SCREENTARGET 1125 */
static void vmsvga3dCmdDestroyGBScreenTarget(PVGASTATE pThis, PVGASTATECC pThisCC, SVGA3dCmdDestroyGBScreenTarget const *pCmd)
{
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    ASSERT_GUEST_RETURN_VOID(pCmd->stid < RT_ELEMENTS(pSvgaR3State->aScreens));
    RT_UNTRUSTED_VALIDATED_FENCE();

    /* Update the entry in the pSvgaR3State->pGboOTableScreenTarget. */
    SVGAOTableScreenTargetEntry entry;
    RT_ZERO(entry);
    int rc = vmsvgaR3OTableWrite(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SCREENTARGET],
                                 pCmd->stid, SVGA3D_OTABLE_SCREEN_TARGET_ENTRY_SIZE, &entry, sizeof(entry));
    if (RT_SUCCESS(rc))
    {
        /* Screen objects and screen targets are similar, therefore we will use the same for both. */
        /** @todo Generic screen object/target interface. */
        VMSVGASCREENOBJECT *pScreen = &pSvgaR3State->aScreens[pCmd->stid];
        vmsvgaR3DestroyScreen(pThis, pThisCC, pScreen);
    }
}


/* SVGA_3D_CMD_BIND_GB_SCREENTARGET 1126 */
static void vmsvga3dCmdBindGBScreenTarget(PVGASTATECC pThisCC, SVGA3dCmdBindGBScreenTarget const *pCmd)
{
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    /* "Binding a surface to a Screen Target the same as flipping" */

    ASSERT_GUEST_RETURN_VOID(pCmd->stid < RT_ELEMENTS(pSvgaR3State->aScreens));
    ASSERT_GUEST_RETURN_VOID(pCmd->image.face == 0 && pCmd->image.mipmap == 0);
    RT_UNTRUSTED_VALIDATED_FENCE();

    /* Assign the surface to the screen target. */
    int rc = VINF_SUCCESS;
    if (pCmd->image.sid != SVGA_ID_INVALID)
        rc = vmsvgaR3OTableVerifyIndex(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SURFACE],
                                       pCmd->image.sid, SVGA3D_OTABLE_SURFACE_ENTRY_SIZE);
    if (RT_SUCCESS(rc))
    {
        SVGAOTableScreenTargetEntry entry;
        rc = vmsvgaR3OTableRead(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SCREENTARGET],
                                pCmd->stid, SVGA3D_OTABLE_SCREEN_TARGET_ENTRY_SIZE, &entry, sizeof(entry));
        if (RT_SUCCESS(rc))
        {
            entry.image = pCmd->image;
            rc = vmsvgaR3OTableWrite(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SCREENTARGET],
                                     pCmd->stid, SVGA3D_OTABLE_SCREEN_TARGET_ENTRY_SIZE, &entry, sizeof(entry));
            if (RT_SUCCESS(rc))
            {
                VMSVGASCREENOBJECT *pScreen = &pSvgaR3State->aScreens[pCmd->stid];
                rc = pSvgaR3State->pFuncsGBO->pfnScreenTargetBind(pThisCC, pScreen, pCmd->image.sid);
                AssertRC(rc);
            }
        }
    }
}


/* SVGA_3D_CMD_UPDATE_GB_SCREENTARGET 1127 */
static void vmsvga3dCmdUpdateGBScreenTarget(PVGASTATECC pThisCC, SVGA3dCmdUpdateGBScreenTarget const *pCmd)
{
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    /* Update the screen target from its backing surface. */
    ASSERT_GUEST_RETURN_VOID(pCmd->stid < RT_ELEMENTS(pSvgaR3State->aScreens));
    RT_UNTRUSTED_VALIDATED_FENCE();

    /* Get the screen target info. */
    SVGAOTableScreenTargetEntry entryScreenTarget;
    int rc = vmsvgaR3OTableRead(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SCREENTARGET],
                                pCmd->stid, SVGA3D_OTABLE_SCREEN_TARGET_ENTRY_SIZE, &entryScreenTarget, sizeof(entryScreenTarget));
    if (RT_SUCCESS(rc))
    {
        ASSERT_GUEST_RETURN_VOID(entryScreenTarget.image.face == 0 && entryScreenTarget.image.mipmap == 0);
        RT_UNTRUSTED_VALIDATED_FENCE();

        if (entryScreenTarget.image.sid != SVGA_ID_INVALID)
        {
            SVGAOTableSurfaceEntry entrySurface;
            rc = vmsvgaR3OTableRead(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SURFACE],
                                    entryScreenTarget.image.sid, SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, &entrySurface, sizeof(entrySurface));
            if (RT_SUCCESS(rc))
            {
                /* Copy entrySurface.mobid content to the screen target. */
                if (entrySurface.mobid != SVGA_ID_INVALID)
                {
                    RT_UNTRUSTED_VALIDATED_FENCE();
                    SVGA3dRect targetRect = pCmd->rect;

                    VMSVGASCREENOBJECT *pScreen = &pSvgaR3State->aScreens[pCmd->stid];
                    if (pScreen->pHwScreen)
                    {
                        /* Copy the screen target surface to the backend's screen. */
                        pSvgaR3State->pFuncsGBO->pfnScreenTargetUpdate(pThisCC, pScreen, &targetRect);
                    }
                    else
                    {
                        SVGASignedRect r;
                        r.left   = pCmd->rect.x;
                        r.top    = pCmd->rect.y;
                        r.right  = pCmd->rect.x + pCmd->rect.w;
                        r.bottom = pCmd->rect.y + pCmd->rect.h;
                        vmsvga3dScreenUpdate(pThisCC, pCmd->stid, r, entryScreenTarget.image, r, 0, NULL);
                    }
                }
            }
        }
    }
}


/* SVGA_3D_CMD_DEFINE_GB_SURFACE_V2 1134 */
static void vmsvga3dCmdDefineGBSurface_v2(PVGASTATECC pThisCC, SVGA3dCmdDefineGBSurface_v2 const *pCmd)
{
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    /* Update the entry in the pSvgaR3State->pGboOTableSurface. */
    SVGAOTableSurfaceEntry entry;
    RT_ZERO(entry);
    entry.format = pCmd->format;
    entry.surface1Flags = pCmd->surfaceFlags;
    entry.numMipLevels = pCmd->numMipLevels;
    entry.multisampleCount = pCmd->multisampleCount;
    entry.autogenFilter = pCmd->autogenFilter;
    entry.size = pCmd->size;
    entry.mobid = SVGA_ID_INVALID;
    entry.arraySize = pCmd->arraySize;
    // entry.mobPitch = 0;
    // entry.surface2Flags = 0;
    // entry.multisamplePattern = 0;
    // entry.qualityLevel = 0;
    // entry.bufferByteStride = 0;
    // entry.minLOD = 0;

    int rc = vmsvgaR3OTableWrite(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SURFACE],
                                 pCmd->sid, SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, &entry, sizeof(entry));
    if (RT_SUCCESS(rc))
    {
        /* Create the host surface. */
        SVGA3dMSPattern const multisamplePattern = pCmd->multisampleCount > 1 ? SVGA3D_MS_PATTERN_STANDARD : SVGA3D_MS_PATTERN_NONE;
        SVGA3dMSQualityLevel const qualityLevel = pCmd->multisampleCount > 1 ? SVGA3D_MS_QUALITY_FULL : SVGA3D_MS_QUALITY_NONE;
        vmsvga3dSurfaceDefine(pThisCC, pCmd->sid, pCmd->surfaceFlags, pCmd->format,
                              pCmd->multisampleCount, multisamplePattern, qualityLevel, pCmd->autogenFilter,
                              pCmd->numMipLevels, &pCmd->size, pCmd->arraySize, /* bufferByteStride = */ 0, /* fAllocMipLevels = */ false);
    }
}


/* SVGA_3D_CMD_DEFINE_GB_MOB64 1135 */
static void vmsvga3dCmdDefineGBMob64(PVGASTATECC pThisCC, SVGA3dCmdDefineGBMob64 const *pCmd)
{
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    ASSERT_GUEST_RETURN_VOID(pCmd->mobid != SVGA_ID_INVALID); /* The guest should not use this id. */

    /* Maybe just update the OTable and create Gbo when the MOB is actually accessed? */
    /* Allocate a structure for the MOB. */
    PVMSVGAMOB pMob = (PVMSVGAMOB)RTMemAllocZ(sizeof(*pMob));
    AssertPtrReturnVoid(pMob);

    int rc = vmsvgaR3MobCreate(pSvgaR3State, pCmd->ptDepth, pCmd->base, pCmd->sizeInBytes, pCmd->mobid, pMob);
    if (RT_SUCCESS(rc))
    {
        return;
    }

    RTMemFree(pMob);
}


/* SVGA_3D_CMD_DX_DEFINE_CONTEXT 1143 */
static int vmsvga3dCmdDXDefineContext(PVGASTATECC pThisCC, SVGA3dCmdDXDefineContext const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);

    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    /* Update the entry in the pSvgaR3State->pGboOTable[SVGA_OTABLE_DXCONTEXT]. */
    SVGAOTableDXContextEntry entry;
    RT_ZERO(entry);
    entry.cid = pCmd->cid;
    entry.mobid = SVGA_ID_INVALID;
    int rc = vmsvgaR3OTableWrite(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_DXCONTEXT],
                                 pCmd->cid, sizeof(SVGAOTableDXContextEntry), &entry, sizeof(entry));
    if (RT_SUCCESS(rc))
    {
        /* Create the host context. */
        rc = vmsvga3dDXDefineContext(pThisCC, pCmd->cid);
    }

    return rc;
#else
    RT_NOREF(pThisCC, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DESTROY_CONTEXT 1144 */
static int vmsvga3dCmdDXDestroyContext(PVGASTATECC pThisCC, SVGA3dCmdDXDestroyContext const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);

    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    /* Update the entry in the pSvgaR3State->pGboOTable[SVGA_OTABLE_DXCONTEXT]. */
    SVGAOTableDXContextEntry entry;
    RT_ZERO(entry);
    vmsvgaR3OTableWrite(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_DXCONTEXT],
                        pCmd->cid, sizeof(SVGAOTableDXContextEntry), &entry, sizeof(entry));

    return vmsvga3dDXDestroyContext(pThisCC, pCmd->cid);
#else
    RT_NOREF(pThisCC, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_BIND_CONTEXT 1145 */
static int vmsvga3dCmdDXBindContext(PVGASTATECC pThisCC, SVGA3dCmdDXBindContext const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);

    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    /* Assign a mobid to a cid. */
    int rc = VINF_SUCCESS;
    if (pCmd->mobid != SVGA_ID_INVALID)
        rc = vmsvgaR3OTableVerifyIndex(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_MOB],
                                       pCmd->mobid, SVGA3D_OTABLE_MOB_ENTRY_SIZE);
    if (RT_SUCCESS(rc))
    {
        SVGAOTableDXContextEntry entry;
        rc = vmsvgaR3OTableRead(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_DXCONTEXT],
                                pCmd->cid, sizeof(SVGAOTableDXContextEntry), &entry, sizeof(entry));
        if (RT_SUCCESS(rc))
        {
            SVGADXContextMobFormat *pSvgaDXContext = NULL;
            if (pCmd->mobid != entry.mobid && entry.mobid != SVGA_ID_INVALID)
            {
                /* Unbind notification to the DX backend. Copy the context data to the guest backing memory. */
                pSvgaDXContext = (SVGADXContextMobFormat *)RTMemAlloc(sizeof(SVGADXContextMobFormat));
                if (pSvgaDXContext)
                {
                    rc = vmsvga3dDXUnbindContext(pThisCC, pCmd->cid, pSvgaDXContext);
                    if (RT_SUCCESS(rc))
                    {
                        PVMSVGAMOB pMob = vmsvgaR3MobGet(pSvgaR3State, entry.mobid);
                        if (pMob)
                        {
                            rc = vmsvgaR3GboWrite(pSvgaR3State, &pMob->Gbo, 0, pSvgaDXContext, sizeof(SVGADXContextMobFormat));
                        }
                    }

                    RTMemFree(pSvgaDXContext);
                    pSvgaDXContext = NULL;
                }
            }

            if (pCmd->mobid != SVGA_ID_INVALID)
            {
                /* Bind a new context. Copy existing data from the guest backing memory. */
                if (pCmd->validContents)
                {
                    PVMSVGAMOB pMob = vmsvgaR3MobGet(pSvgaR3State, pCmd->mobid);
                    if (pMob)
                    {
                        pSvgaDXContext = (SVGADXContextMobFormat *)RTMemAlloc(sizeof(SVGADXContextMobFormat));
                        if (pSvgaDXContext)
                        {
                            rc = vmsvgaR3GboRead(pSvgaR3State, &pMob->Gbo, 0, pSvgaDXContext, sizeof(SVGADXContextMobFormat));
                            if (RT_FAILURE(rc))
                            {
                                RTMemFree(pSvgaDXContext);
                                pSvgaDXContext = NULL;
                            }
                        }
                    }
                }

                rc = vmsvga3dDXBindContext(pThisCC, pCmd->cid, pSvgaDXContext);

                RTMemFree(pSvgaDXContext);
            }

            /* Update the object table. */
            entry.mobid = pCmd->mobid;
            rc = vmsvgaR3OTableWrite(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_DXCONTEXT],
                                     pCmd->cid, sizeof(SVGAOTableDXContextEntry), &entry, sizeof(entry));
        }
    }

    return rc;
#else
    RT_NOREF(pThisCC, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_READBACK_CONTEXT 1146 */
static int vmsvga3dCmdDXReadbackContext(PVGASTATECC pThisCC, SVGA3dCmdDXReadbackContext const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);

    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    /* "Request that the device flush the contents back into guest memory." */
    SVGAOTableDXContextEntry entry;
    int rc = vmsvgaR3OTableRead(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_DXCONTEXT],
                                pCmd->cid, sizeof(SVGAOTableDXContextEntry), &entry, sizeof(entry));
    if (RT_SUCCESS(rc))
    {
        if (entry.mobid != SVGA_ID_INVALID)
        {
            PVMSVGAMOB pMob = vmsvgaR3MobGet(pSvgaR3State, entry.mobid);
            if (pMob)
            {
                /* Get the content. */
                SVGADXContextMobFormat *pSvgaDXContext = (SVGADXContextMobFormat *)RTMemAlloc(sizeof(SVGADXContextMobFormat));
                if (pSvgaDXContext)
                {
                    rc = vmsvga3dDXReadbackContext(pThisCC, pCmd->cid, pSvgaDXContext);
                    if (RT_SUCCESS(rc))
                        rc = vmsvgaR3GboWrite(pSvgaR3State, &pMob->Gbo, 0, pSvgaDXContext, sizeof(SVGADXContextMobFormat));

                    RTMemFree(pSvgaDXContext);
                }
                else
                    rc = VERR_NO_MEMORY;
            }
        }
    }

    return rc;
#else
    RT_NOREF(pThisCC, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_INVALIDATE_CONTEXT 1147 */
static int vmsvga3dCmdDXInvalidateContext(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXInvalidateContext const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dDXInvalidateContext(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_SINGLE_CONSTANT_BUFFER 1148 */
static int vmsvga3dCmdDXSetSingleConstantBuffer(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetSingleConstantBuffer const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetSingleConstantBuffer(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_SHADER_RESOURCES 1149 */
static int vmsvga3dCmdDXSetShaderResources(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetShaderResources const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    SVGA3dShaderResourceViewId const *paShaderResourceViewId = (SVGA3dShaderResourceViewId *)&pCmd[1];
    uint32_t const cShaderResourceViewId = (cbCmd - sizeof(*pCmd)) / sizeof(SVGA3dShaderResourceViewId);
    return vmsvga3dDXSetShaderResources(pThisCC, idDXContext, pCmd, cShaderResourceViewId, paShaderResourceViewId);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_SHADER 1150 */
static int vmsvga3dCmdDXSetShader(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetShader const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetShader(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_SAMPLERS 1151 */
static int vmsvga3dCmdDXSetSamplers(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetSamplers const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    SVGA3dSamplerId const *paSamplerId = (SVGA3dSamplerId *)&pCmd[1];
    uint32_t const cSamplerId = (cbCmd - sizeof(*pCmd)) / sizeof(SVGA3dSamplerId);
    return vmsvga3dDXSetSamplers(pThisCC, idDXContext, pCmd, cSamplerId, paSamplerId);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DRAW 1152 */
static int vmsvga3dCmdDXDraw(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDraw const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDraw(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DRAW_INDEXED 1153 */
static int vmsvga3dCmdDXDrawIndexed(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDrawIndexed const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDrawIndexed(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DRAW_INSTANCED 1154 */
static int vmsvga3dCmdDXDrawInstanced(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDrawInstanced const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDrawInstanced(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DRAW_INDEXED_INSTANCED 1155 */
static int vmsvga3dCmdDXDrawIndexedInstanced(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDrawIndexedInstanced const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDrawIndexedInstanced(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DRAW_AUTO 1156 */
static int vmsvga3dCmdDXDrawAuto(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDrawAuto const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pCmd, cbCmd);
    return vmsvga3dDXDrawAuto(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_INPUT_LAYOUT 1157 */
static int vmsvga3dCmdDXSetInputLayout(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetInputLayout const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetInputLayout(pThisCC, idDXContext, pCmd->elementLayoutId);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_VERTEX_BUFFERS 1158 */
static int vmsvga3dCmdDXSetVertexBuffers(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetVertexBuffers const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    SVGA3dVertexBuffer const *paVertexBuffer = (SVGA3dVertexBuffer *)&pCmd[1];
    uint32_t const cVertexBuffer = (cbCmd - sizeof(*pCmd)) / sizeof(SVGA3dVertexBuffer);
    return vmsvga3dDXSetVertexBuffers(pThisCC, idDXContext, pCmd->startBuffer, cVertexBuffer, paVertexBuffer);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_INDEX_BUFFER 1159 */
static int vmsvga3dCmdDXSetIndexBuffer(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetIndexBuffer const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetIndexBuffer(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_TOPOLOGY 1160 */
static int vmsvga3dCmdDXSetTopology(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetTopology const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetTopology(pThisCC, idDXContext, pCmd->topology);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_RENDERTARGETS 1161 */
static int vmsvga3dCmdDXSetRenderTargets(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetRenderTargets const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    SVGA3dRenderTargetViewId const *paRenderTargetViewId = (SVGA3dRenderTargetViewId *)&pCmd[1];
    uint32_t const cRenderTargetViewId = (cbCmd - sizeof(*pCmd)) / sizeof(SVGA3dRenderTargetViewId);
    return vmsvga3dDXSetRenderTargets(pThisCC, idDXContext, pCmd->depthStencilViewId, cRenderTargetViewId, paRenderTargetViewId);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_BLEND_STATE 1162 */
static int vmsvga3dCmdDXSetBlendState(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetBlendState const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetBlendState(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_DEPTHSTENCIL_STATE 1163 */
static int vmsvga3dCmdDXSetDepthStencilState(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetDepthStencilState const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetDepthStencilState(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_RASTERIZER_STATE 1164 */
static int vmsvga3dCmdDXSetRasterizerState(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetRasterizerState const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetRasterizerState(pThisCC, idDXContext, pCmd->rasterizerId);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DEFINE_QUERY 1165 */
static int vmsvga3dCmdDXDefineQuery(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDefineQuery const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDefineQuery(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DESTROY_QUERY 1166 */
static int vmsvga3dCmdDXDestroyQuery(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDestroyQuery const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDestroyQuery(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_BIND_QUERY 1167 */
static int vmsvga3dCmdDXBindQuery(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXBindQuery const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(cbCmd);
    /* This returns NULL if mob does not exist. If the guest sends a wrong mob id, the current mob will be unbound. */
    PVMSVGAMOB pMob = vmsvgaR3MobGet(pSvgaR3State, pCmd->mobid);
    return vmsvga3dDXBindQuery(pThisCC, idDXContext, pCmd, pMob);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_QUERY_OFFSET 1168 */
static int vmsvga3dCmdDXSetQueryOffset(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetQueryOffset const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetQueryOffset(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_BEGIN_QUERY 1169 */
static int vmsvga3dCmdDXBeginQuery(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXBeginQuery const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXBeginQuery(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_END_QUERY 1170 */
static int vmsvga3dCmdDXEndQuery(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXEndQuery const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXEndQuery(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_READBACK_QUERY 1171 */
static int vmsvga3dCmdDXReadbackQuery(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXReadbackQuery const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXReadbackQuery(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_PREDICATION 1172 */
static int vmsvga3dCmdDXSetPredication(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetPredication const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetPredication(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_SOTARGETS 1173 */
static int vmsvga3dCmdDXSetSOTargets(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetSOTargets const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    SVGA3dSoTarget const *paSoTarget = (SVGA3dSoTarget *)&pCmd[1];
    uint32_t const cSoTarget = (cbCmd - sizeof(*pCmd)) / sizeof(SVGA3dSoTarget);
    return vmsvga3dDXSetSOTargets(pThisCC, idDXContext, cSoTarget, paSoTarget);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_VIEWPORTS 1174 */
static int vmsvga3dCmdDXSetViewports(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetViewports const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    SVGA3dViewport const *paViewport = (SVGA3dViewport *)&pCmd[1];
    uint32_t const cViewport = (cbCmd - sizeof(*pCmd)) / sizeof(SVGA3dViewport);
    return vmsvga3dDXSetViewports(pThisCC, idDXContext, cViewport, paViewport);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_SCISSORRECTS 1175 */
static int vmsvga3dCmdDXSetScissorRects(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetScissorRects const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    SVGASignedRect const *paRect = (SVGASignedRect *)&pCmd[1];
    uint32_t const cRect = (cbCmd - sizeof(*pCmd)) / sizeof(SVGASignedRect);
    return vmsvga3dDXSetScissorRects(pThisCC, idDXContext, cRect, paRect);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_CLEAR_RENDERTARGET_VIEW 1176 */
static int vmsvga3dCmdDXClearRenderTargetView(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXClearRenderTargetView const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXClearRenderTargetView(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_CLEAR_DEPTHSTENCIL_VIEW 1177 */
static int vmsvga3dCmdDXClearDepthStencilView(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXClearDepthStencilView const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXClearDepthStencilView(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_PRED_COPY_REGION 1178 */
static int vmsvga3dCmdDXPredCopyRegion(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXPredCopyRegion const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXPredCopyRegion(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_PRED_COPY 1179 */
static int vmsvga3dCmdDXPredCopy(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXPredCopy const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXPredCopy(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_PRESENTBLT 1180 */
static int vmsvga3dCmdDXPresentBlt(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXPresentBlt const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXPresentBlt(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_GENMIPS 1181 */
static int vmsvga3dCmdDXGenMips(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXGenMips const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXGenMips(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_UPDATE_SUBRESOURCE 1182 */
static int vmsvga3dCmdDXUpdateSubResource(PVGASTATECC pThisCC, SVGA3dCmdDXUpdateSubResource const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(cbCmd);

    LogFlowFunc(("sid=%u, subResource=%u, box=%d,%d,%d %ux%ux%u\n",
                 pCmd->sid, pCmd->subResource, pCmd->box.x, pCmd->box.y, pCmd->box.z, pCmd->box.w, pCmd->box.h, pCmd->box.d));

    /* "Inform the device that the guest-contents have been updated." */
    SVGAOTableSurfaceEntry entrySurface;
    int rc = vmsvgaR3OTableRead(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SURFACE],
                                pCmd->sid, SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, &entrySurface, sizeof(entrySurface));
    if (RT_SUCCESS(rc))
    {
        PVMSVGAMOB pMob = vmsvgaR3MobGet(pSvgaR3State, entrySurface.mobid);
        if (pMob)
        {
            uint32 const cSubresource = vmsvga3dGetSubresourceCount(pThisCC, pCmd->sid);
            ASSERT_GUEST_RETURN(pCmd->subResource < cSubresource, VERR_INVALID_PARAMETER);
            /* pCmd->box will be verified by the mapping function. */
            RT_UNTRUSTED_VALIDATED_FENCE();

            /** @todo Mapping functions should use subresource index rather than SVGA3dSurfaceImageId? */
            SVGA3dSurfaceImageId image;
            image.sid = pCmd->sid;
            vmsvga3dCalcMipmapAndFace(entrySurface.numMipLevels, pCmd->subResource, &image.mipmap, &image.face);

            rc = vmsvgaR3TransferSurfaceLevel(pThisCC, pMob, &image, &pCmd->box, SVGA3D_WRITE_HOST_VRAM);
            AssertRC(rc);
        }
    }

    return rc;
#else
    RT_NOREF(pThisCC, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_READBACK_SUBRESOURCE 1183 */
static int vmsvga3dCmdDXReadbackSubResource(PVGASTATECC pThisCC, SVGA3dCmdDXReadbackSubResource const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(cbCmd);

    LogFlowFunc(("sid=%u, subResource=%u\n",
                 pCmd->sid, pCmd->subResource));

    /* "Request the device to flush the dirty contents into the guest." */
    SVGAOTableSurfaceEntry entrySurface;
    int rc = vmsvgaR3OTableRead(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SURFACE],
                                pCmd->sid, SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, &entrySurface, sizeof(entrySurface));
    if (RT_SUCCESS(rc))
    {
        PVMSVGAMOB pMob = vmsvgaR3MobGet(pSvgaR3State, entrySurface.mobid);
        if (pMob)
        {
            uint32 const cSubresource = vmsvga3dGetSubresourceCount(pThisCC, pCmd->sid);
            ASSERT_GUEST_RETURN(pCmd->subResource < cSubresource, VERR_INVALID_PARAMETER);
            RT_UNTRUSTED_VALIDATED_FENCE();

            /** @todo Mapping functions should use subresource index rather than SVGA3dSurfaceImageId? */
            SVGA3dSurfaceImageId image;
            image.sid = pCmd->sid;
            vmsvga3dCalcMipmapAndFace(entrySurface.numMipLevels, pCmd->subResource, &image.mipmap, &image.face);

            rc = vmsvgaR3TransferSurfaceLevel(pThisCC, pMob, &image, /* all pBox = */ NULL, SVGA3D_READ_HOST_VRAM);
            AssertRC(rc);
        }
    }

    return rc;
#else
    RT_NOREF(pThisCC, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_INVALIDATE_SUBRESOURCE 1184 */
static int vmsvga3dCmdDXInvalidateSubResource(PVGASTATECC pThisCC, SVGA3dCmdDXInvalidateSubResource const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(cbCmd);

    LogFlowFunc(("sid=%u, subResource=%u\n",
                 pCmd->sid, pCmd->subResource));

    /* "Notify the device that the contents can be lost." */
    SVGAOTableSurfaceEntry entrySurface;
    int rc = vmsvgaR3OTableRead(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SURFACE],
                                pCmd->sid, SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, &entrySurface, sizeof(entrySurface));
    if (RT_SUCCESS(rc))
    {
        uint32_t iFace;
        uint32_t iMipmap;
        vmsvga3dCalcMipmapAndFace(entrySurface.numMipLevels, pCmd->subResource, &iMipmap, &iFace);
        vmsvga3dSurfaceInvalidate(pThisCC, pCmd->sid, iFace, iMipmap);
    }

    return rc;
#else
    RT_NOREF(pThisCC, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DEFINE_SHADERRESOURCE_VIEW 1185 */
static int vmsvga3dCmdDXDefineShaderResourceView(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDefineShaderResourceView const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDefineShaderResourceView(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DESTROY_SHADERRESOURCE_VIEW 1186 */
static int vmsvga3dCmdDXDestroyShaderResourceView(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDestroyShaderResourceView const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDestroyShaderResourceView(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DEFINE_RENDERTARGET_VIEW 1187 */
static int vmsvga3dCmdDXDefineRenderTargetView(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDefineRenderTargetView const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDefineRenderTargetView(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DESTROY_RENDERTARGET_VIEW 1188 */
static int vmsvga3dCmdDXDestroyRenderTargetView(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDestroyRenderTargetView const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDestroyRenderTargetView(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_VIEW 1189 */
static int vmsvga3dCmdDXDefineDepthStencilView(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDefineDepthStencilView const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    SVGA3dCmdDXDefineDepthStencilView_v2 cmd;
    cmd.depthStencilViewId = pCmd->depthStencilViewId;
    cmd.sid                = pCmd->sid;
    cmd.format             = pCmd->format;
    cmd.resourceDimension  = pCmd->resourceDimension;
    cmd.mipSlice           = pCmd->mipSlice;
    cmd.firstArraySlice    = pCmd->firstArraySlice;
    cmd.arraySize          = pCmd->arraySize;
    cmd.flags              = 0;
    return vmsvga3dDXDefineDepthStencilView(pThisCC, idDXContext, &cmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_VIEW 1190 */
static int vmsvga3dCmdDXDestroyDepthStencilView(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDestroyDepthStencilView const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDestroyDepthStencilView(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DEFINE_ELEMENTLAYOUT 1191 */
static int vmsvga3dCmdDXDefineElementLayout(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDefineElementLayout const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    SVGA3dInputElementDesc const *paDesc = (SVGA3dInputElementDesc *)&pCmd[1];
    uint32_t const cDesc = (cbCmd - sizeof(*pCmd)) / sizeof(SVGA3dInputElementDesc);
    return vmsvga3dDXDefineElementLayout(pThisCC, idDXContext, pCmd->elementLayoutId, cDesc, paDesc);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DESTROY_ELEMENTLAYOUT 1192 */
static int vmsvga3dCmdDXDestroyElementLayout(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDestroyElementLayout const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDestroyElementLayout(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DEFINE_BLEND_STATE 1193 */
static int vmsvga3dCmdDXDefineBlendState(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDefineBlendState const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDefineBlendState(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DESTROY_BLEND_STATE 1194 */
static int vmsvga3dCmdDXDestroyBlendState(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDestroyBlendState const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDestroyBlendState(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_STATE 1195 */
static int vmsvga3dCmdDXDefineDepthStencilState(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDefineDepthStencilState const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDefineDepthStencilState(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_STATE 1196 */
static int vmsvga3dCmdDXDestroyDepthStencilState(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDestroyDepthStencilState const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDestroyDepthStencilState(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DEFINE_RASTERIZER_STATE 1197 */
static int vmsvga3dCmdDXDefineRasterizerState(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDefineRasterizerState const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    SVGA3dCmdDXDefineRasterizerState_v2 cmd;
    cmd.rasterizerId          = pCmd->rasterizerId;
    cmd.fillMode              = pCmd->fillMode;
    cmd.cullMode              = pCmd->cullMode;
    cmd.frontCounterClockwise = pCmd->frontCounterClockwise;
    cmd.provokingVertexLast   = pCmd->provokingVertexLast;
    cmd.depthBias             = pCmd->depthBias;
    cmd.depthBiasClamp        = pCmd->depthBiasClamp;
    cmd.slopeScaledDepthBias  = pCmd->slopeScaledDepthBias;
    cmd.depthClipEnable       = pCmd->depthClipEnable;
    cmd.scissorEnable         = pCmd->scissorEnable;
    cmd.multisampleEnable     = pCmd->multisampleEnable;
    cmd.antialiasedLineEnable = pCmd->antialiasedLineEnable;
    cmd.lineWidth             = pCmd->lineWidth;
    cmd.lineStippleEnable     = pCmd->lineStippleEnable;
    cmd.lineStippleFactor     = pCmd->lineStippleFactor;
    cmd.lineStipplePattern    = pCmd->lineStipplePattern;
    cmd.forcedSampleCount     = 0;
    return vmsvga3dDXDefineRasterizerState(pThisCC, idDXContext, &cmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DEFINE_RASTERIZER_STATE_V2 1288 */
static int vmsvga3dCmdDXDefineRasterizerState_v2(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDefineRasterizerState_v2 const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDefineRasterizerState(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DESTROY_RASTERIZER_STATE 1198 */
static int vmsvga3dCmdDXDestroyRasterizerState(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDestroyRasterizerState const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDestroyRasterizerState(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DEFINE_SAMPLER_STATE 1199 */
static int vmsvga3dCmdDXDefineSamplerState(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDefineSamplerState const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDefineSamplerState(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DESTROY_SAMPLER_STATE 1200 */
static int vmsvga3dCmdDXDestroySamplerState(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDestroySamplerState const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDestroySamplerState(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DEFINE_SHADER 1201 */
static int vmsvga3dCmdDXDefineShader(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDefineShader const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDefineShader(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DESTROY_SHADER 1202 */
static int vmsvga3dCmdDXDestroyShader(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDestroyShader const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDestroyShader(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_BIND_SHADER 1203 */
static int vmsvga3dCmdDXBindShader(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXBindShader const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(idDXContext, cbCmd);
    /* This returns NULL if mob does not exist. If the guest sends a wrong mob id, the current mob will be unbound. */
    PVMSVGAMOB pMob = vmsvgaR3MobGet(pSvgaR3State, pCmd->mobid);
    return vmsvga3dDXBindShader(pThisCC, pCmd, pMob);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DEFINE_STREAMOUTPUT 1204 */
static int vmsvga3dCmdDXDefineStreamOutput(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDefineStreamOutput const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDefineStreamOutput(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DESTROY_STREAMOUTPUT 1205 */
static int vmsvga3dCmdDXDestroyStreamOutput(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDestroyStreamOutput const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDestroyStreamOutput(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_STREAMOUTPUT 1206 */
static int vmsvga3dCmdDXSetStreamOutput(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetStreamOutput const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetStreamOutput(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_COTABLE 1207 */
static int vmsvga3dCmdDXSetCOTable(PVGASTATECC pThisCC, SVGA3dCmdDXSetCOTable const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    /* This returns NULL if mob does not exist. If the guest sends a wrong mob id, the current mob will be unbound. */
    PVMSVGAMOB pMob = vmsvgaR3MobGet(pSvgaR3State, pCmd->mobid);
    return vmsvga3dDXSetCOTable(pThisCC, pCmd, pMob);
#else
    RT_NOREF(pThisCC, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_READBACK_COTABLE 1208 */
static int vmsvga3dCmdDXReadbackCOTable(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXReadbackCOTable const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(idDXContext, cbCmd);
    return vmsvga3dDXReadbackCOTable(pThisCC, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_BUFFER_COPY 1209 */
static int vmsvga3dCmdDXBufferCopy(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXBufferCopy const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(idDXContext, cbCmd);

    int rc;

    /** @todo Backend should o the copy is both buffers have a hardware resource. */
    SVGA3dSurfaceImageId imageBufferSrc;
    imageBufferSrc.sid = pCmd->src;
    imageBufferSrc.face = 0;
    imageBufferSrc.mipmap = 0;

    SVGA3dSurfaceImageId imageBufferDest;
    imageBufferDest.sid = pCmd->dest;
    imageBufferDest.face = 0;
    imageBufferDest.mipmap = 0;

    /*
     * Map the source buffer.
     */
    VMSVGA3D_MAPPED_SURFACE mapBufferSrc;
    rc = vmsvga3dSurfaceMap(pThisCC, &imageBufferSrc, NULL, VMSVGA3D_SURFACE_MAP_READ, &mapBufferSrc);
    if (RT_SUCCESS(rc))
    {
        /*
         * Map the destination buffer.
         */
        VMSVGA3D_MAPPED_SURFACE mapBufferDest;
        rc = vmsvga3dSurfaceMap(pThisCC, &imageBufferDest, NULL, VMSVGA3D_SURFACE_MAP_WRITE, &mapBufferDest);
        if (RT_SUCCESS(rc))
        {
            /*
             * Copy the source buffer to the destination.
             */
            uint8_t const *pu8BufferSrc = (uint8_t *)mapBufferSrc.pvData;
            uint32_t const cbBufferSrc = mapBufferSrc.cbRow;

            uint8_t *pu8BufferDest = (uint8_t *)mapBufferDest.pvData;
            uint32_t const cbBufferDest = mapBufferDest.cbRow;

            if (   pCmd->srcX < cbBufferSrc
                && pCmd->width <= cbBufferSrc- pCmd->srcX
                && pCmd->destX < cbBufferDest
                && pCmd->width <= cbBufferDest - pCmd->destX)
            {
                RT_UNTRUSTED_VALIDATED_FENCE();

                memcpy(&pu8BufferDest[pCmd->destX], &pu8BufferSrc[pCmd->srcX], pCmd->width);
            }
            else
                ASSERT_GUEST_FAILED_STMT(rc = VERR_INVALID_PARAMETER);

            vmsvga3dSurfaceUnmap(pThisCC, &imageBufferDest, &mapBufferDest, true);
        }

        vmsvga3dSurfaceUnmap(pThisCC, &imageBufferSrc, &mapBufferSrc, false);
    }

    return rc;
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_TRANSFER_FROM_BUFFER 1210 */
static int vmsvga3dCmdDXTransferFromBuffer(PVGASTATECC pThisCC, SVGA3dCmdDXTransferFromBuffer const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);

    /* Plan:
     * - map the buffer;
     * - map the surface;
     * - copy from buffer map to the surface map.
     */

    int rc;

    SVGA3dSurfaceImageId imageBuffer;
    imageBuffer.sid = pCmd->srcSid;
    imageBuffer.face = 0;
    imageBuffer.mipmap = 0;

    SVGA3dSurfaceImageId imageSurface;
    imageSurface.sid = pCmd->destSid;
    rc = vmsvga3dCalcSurfaceMipmapAndFace(pThisCC, pCmd->destSid, pCmd->destSubResource, &imageSurface.mipmap, &imageSurface.face);
    AssertRCReturn(rc, rc);

    /*
     * Map the buffer.
     */
    VMSVGA3D_MAPPED_SURFACE mapBuffer;
    rc = vmsvga3dSurfaceMap(pThisCC, &imageBuffer, NULL, VMSVGA3D_SURFACE_MAP_READ, &mapBuffer);
    if (RT_SUCCESS(rc))
    {
        /*
         * Map the surface.
         */
        VMSVGA3D_MAPPED_SURFACE mapSurface;
        rc = vmsvga3dSurfaceMap(pThisCC, &imageSurface, &pCmd->destBox, VMSVGA3D_SURFACE_MAP_WRITE, &mapSurface);
        if (RT_SUCCESS(rc))
        {
            /*
             * Copy the mapped buffer to the surface. "Raw byte wise transfer"
             */
            uint8_t const *pu8Buffer = (uint8_t *)mapBuffer.pvData;
            uint32_t const cbBuffer = mapBuffer.cbRow;

            if (pCmd->srcOffset <= cbBuffer)
            {
                RT_UNTRUSTED_VALIDATED_FENCE();
                uint8_t const *pu8BufferBegin = pu8Buffer;
                uint8_t const *pu8BufferEnd = pu8Buffer + cbBuffer;

                pu8Buffer += pCmd->srcOffset;

                uint8_t *pu8Surface = (uint8_t *)mapSurface.pvData;

                uint32_t const cbRowCopy = RT_MIN(pCmd->srcPitch, mapSurface.cbRow);
                for (uint32_t z = 0; z < mapSurface.box.d && RT_SUCCESS(rc); ++z)
                {
                    uint8_t const *pu8BufferRow = pu8Buffer;
                    uint8_t *pu8SurfaceRow = pu8Surface;
                    for (uint32_t iRow = 0; iRow < mapSurface.cRows; ++iRow)
                    {
                        ASSERT_GUEST_STMT_BREAK(   (uintptr_t)pu8BufferRow >= (uintptr_t)pu8BufferBegin
                                                && (uintptr_t)pu8BufferRow < (uintptr_t)pu8BufferEnd
                                                && (uintptr_t)pu8BufferRow < (uintptr_t)(pu8BufferRow + cbRowCopy)
                                                && (uintptr_t)(pu8BufferRow + cbRowCopy) > (uintptr_t)pu8BufferBegin
                                                && (uintptr_t)(pu8BufferRow + cbRowCopy) <= (uintptr_t)pu8BufferEnd,
                                                rc = VERR_INVALID_PARAMETER);

                        memcpy(pu8SurfaceRow, pu8BufferRow, cbRowCopy);

                        pu8SurfaceRow += mapSurface.cbRowPitch;
                        pu8BufferRow += pCmd->srcPitch;
                    }

                    pu8Buffer += pCmd->srcSlicePitch;
                    pu8Surface += mapSurface.cbDepthPitch;
                }
            }
            else
                ASSERT_GUEST_FAILED_STMT(rc = VERR_INVALID_PARAMETER);

            vmsvga3dSurfaceUnmap(pThisCC, &imageSurface, &mapSurface, true);
        }

        vmsvga3dSurfaceUnmap(pThisCC, &imageBuffer, &mapBuffer, false);
    }

    return rc;
#else
    RT_NOREF(pThisCC, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SURFACE_COPY_AND_READBACK 1211 */
static int vmsvga3dCmdDXSurfaceCopyAndReadback(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSurfaceCopyAndReadback const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dDXSurfaceCopyAndReadback(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_MOVE_QUERY 1212 */
static int vmsvga3dCmdDXMoveQuery(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXMoveQuery const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dDXMoveQuery(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_BIND_ALL_QUERY 1213 */
static int vmsvga3dCmdDXBindAllQuery(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXBindAllQuery const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXBindAllQuery(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_READBACK_ALL_QUERY 1214 */
static int vmsvga3dCmdDXReadbackAllQuery(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXReadbackAllQuery const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXReadbackAllQuery(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_PRED_TRANSFER_FROM_BUFFER 1215 */
static int vmsvga3dCmdDXPredTransferFromBuffer(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXPredTransferFromBuffer const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(idDXContext, cbCmd);

    /* This command is executed in a context: "The context is implied from the command buffer header."
     * However the device design allows to do the transfer without a context, so re-use context-less command handler.
     */
    SVGA3dCmdDXTransferFromBuffer cmd;
    cmd.srcSid          = pCmd->srcSid;
    cmd.srcOffset       = pCmd->srcOffset;
    cmd.srcPitch        = pCmd->srcPitch;
    cmd.srcSlicePitch   = pCmd->srcSlicePitch;
    cmd.destSid         = pCmd->destSid;
    cmd.destSubResource = pCmd->destSubResource;
    cmd.destBox         = pCmd->destBox;
    return vmsvga3dCmdDXTransferFromBuffer(pThisCC, &cmd, sizeof(cmd));
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_MOB_FENCE_64 1216 */
static int vmsvga3dCmdDXMobFence64(PVGASTATECC pThisCC, SVGA3dCmdDXMobFence64 const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(cbCmd);

    PVMSVGAMOB pMob = vmsvgaR3MobGet(pSvgaR3State, pCmd->mobId);
    ASSERT_GUEST_RETURN(pMob, VERR_INVALID_PARAMETER);

    int rc = vmsvgaR3MobWrite(pSvgaR3State, pMob, pCmd->mobOffset, &pCmd->value, sizeof(pCmd->value));
    ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);

    return VINF_SUCCESS;
#else
    RT_NOREF(pThisCC, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_BIND_ALL_SHADER 1217 */
static int vmsvga3dCmdDXBindAllShader(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXBindAllShader const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dDXBindAllShader(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_HINT 1218 */
static int vmsvga3dCmdDXHint(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXHint const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dDXHint(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_BUFFER_UPDATE 1219 */
static int vmsvga3dCmdDXBufferUpdate(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXBufferUpdate const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dDXBufferUpdate(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_VS_CONSTANT_BUFFER_OFFSET 1220 */
static int vmsvga3dCmdDXSetVSConstantBufferOffset(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetVSConstantBufferOffset const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetConstantBufferOffset(pThisCC, idDXContext, pCmd, SVGA3D_SHADERTYPE_VS);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_PS_CONSTANT_BUFFER_OFFSET 1221 */
static int vmsvga3dCmdDXSetPSConstantBufferOffset(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetPSConstantBufferOffset const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetConstantBufferOffset(pThisCC, idDXContext, pCmd, SVGA3D_SHADERTYPE_PS);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_GS_CONSTANT_BUFFER_OFFSET 1222 */
static int vmsvga3dCmdDXSetGSConstantBufferOffset(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetGSConstantBufferOffset const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetConstantBufferOffset(pThisCC, idDXContext, pCmd, SVGA3D_SHADERTYPE_GS);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_HS_CONSTANT_BUFFER_OFFSET 1223 */
static int vmsvga3dCmdDXSetHSConstantBufferOffset(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetHSConstantBufferOffset const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetConstantBufferOffset(pThisCC, idDXContext, pCmd, SVGA3D_SHADERTYPE_HS);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_DS_CONSTANT_BUFFER_OFFSET 1224 */
static int vmsvga3dCmdDXSetDSConstantBufferOffset(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetDSConstantBufferOffset const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetConstantBufferOffset(pThisCC, idDXContext, pCmd, SVGA3D_SHADERTYPE_DS);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_CS_CONSTANT_BUFFER_OFFSET 1225 */
static int vmsvga3dCmdDXSetCSConstantBufferOffset(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetCSConstantBufferOffset const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetConstantBufferOffset(pThisCC, idDXContext, pCmd, SVGA3D_SHADERTYPE_CS);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_COND_BIND_ALL_SHADER 1226 */
static int vmsvga3dCmdDXCondBindAllShader(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXCondBindAllShader const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dDXCondBindAllShader(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_SCREEN_COPY 1227 */
static int vmsvga3dCmdScreenCopy(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdScreenCopy const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dScreenCopy(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_GROW_OTABLE 1236 */
static int vmsvga3dCmdGrowOTable(PVGASTATECC pThisCC, SVGA3dCmdGrowOTable const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(cbCmd);
    return vmsvgaR3OTableSetOrGrow(pSvgaR3State, pCmd->type, pCmd->baseAddress,
                                   pCmd->sizeInBytes, pCmd->validSizeInBytes, pCmd->ptDepth, /*fGrow*/ true);
#else
    RT_NOREF(pThisCC, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_GROW_COTABLE 1237 */
static int vmsvga3dCmdDXGrowCOTable(PVGASTATECC pThisCC, SVGA3dCmdDXGrowCOTable const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXGrowCOTable(pThisCC, pCmd);
#else
    RT_NOREF(pThisCC, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_INTRA_SURFACE_COPY 1238 */
static int vmsvga3dCmdIntraSurfaceCopy(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdIntraSurfaceCopy const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dIntraSurfaceCopy(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DEFINE_GB_SURFACE_V3 1239 */
static int vmsvga3dCmdDefineGBSurface_v3(PVGASTATECC pThisCC, SVGA3dCmdDefineGBSurface_v3 const *pCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    /* Update the entry in the pSvgaR3State->pGboOTableSurface. */
    SVGAOTableSurfaceEntry entry;
    RT_ZERO(entry);
    entry.format = pCmd->format;
    entry.surface1Flags = (uint32_t)(pCmd->surfaceFlags);
    entry.numMipLevels = pCmd->numMipLevels;
    entry.multisampleCount = pCmd->multisampleCount;
    entry.autogenFilter = pCmd->autogenFilter;
    entry.size = pCmd->size;
    entry.mobid = SVGA_ID_INVALID;
    entry.arraySize = pCmd->arraySize;
    // entry.mobPitch = 0;
    entry.surface2Flags = (uint32_t)(pCmd->surfaceFlags >> UINT64_C(32));
    entry.multisamplePattern = pCmd->multisamplePattern;
    entry.qualityLevel = pCmd->qualityLevel;
    // entry.bufferByteStride = 0;
    // entry.minLOD = 0;

    int rc = vmsvgaR3OTableWrite(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SURFACE],
                                 pCmd->sid, SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, &entry, sizeof(entry));
    if (RT_SUCCESS(rc))
    {
        /* Create the host surface. */
        vmsvga3dSurfaceDefine(pThisCC, pCmd->sid, pCmd->surfaceFlags, pCmd->format,
                              pCmd->multisampleCount, pCmd->multisamplePattern, pCmd->qualityLevel, pCmd->autogenFilter,
                              pCmd->numMipLevels, &pCmd->size, pCmd->arraySize, /* bufferByteStride = */ 0, /* fAllocMipLevels = */ false);
    }
    return rc;
#else
    RT_NOREF(pThisCC, pCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_RESOLVE_COPY 1240 */
static int vmsvga3dCmdDXResolveCopy(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXResolveCopy const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXResolveCopy(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_PRED_RESOLVE_COPY 1241 */
static int vmsvga3dCmdDXPredResolveCopy(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXPredResolveCopy const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dDXPredResolveCopy(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_PRED_CONVERT_REGION 1242 */
static int vmsvga3dCmdDXPredConvertRegion(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXPredConvertRegion const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dDXPredConvertRegion(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_PRED_CONVERT 1243 */
static int vmsvga3dCmdDXPredConvert(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXPredConvert const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dDXPredConvert(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_WHOLE_SURFACE_COPY 1244 */
static int vmsvga3dCmdWholeSurfaceCopy(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdWholeSurfaceCopy const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dWholeSurfaceCopy(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DEFINE_UA_VIEW 1245 */
static int vmsvga3dCmdDXDefineUAView(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDefineUAView const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDefineUAView(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DESTROY_UA_VIEW 1246 */
static int vmsvga3dCmdDXDestroyUAView(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDestroyUAView const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDestroyUAView(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_CLEAR_UA_VIEW_UINT 1247 */
static int vmsvga3dCmdDXClearUAViewUint(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXClearUAViewUint const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXClearUAViewUint(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_CLEAR_UA_VIEW_FLOAT 1248 */
static int vmsvga3dCmdDXClearUAViewFloat(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXClearUAViewFloat const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXClearUAViewFloat(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_COPY_STRUCTURE_COUNT 1249 */
static int vmsvga3dCmdDXCopyStructureCount(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXCopyStructureCount const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXCopyStructureCount(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_UA_VIEWS 1250 */
static int vmsvga3dCmdDXSetUAViews(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetUAViews const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    SVGA3dUAViewId const *paUAViewId = (SVGA3dUAViewId *)&pCmd[1];
    uint32_t const cUAViewId = (cbCmd - sizeof(*pCmd)) / sizeof(SVGA3dUAViewId);
    return vmsvga3dDXSetUAViews(pThisCC, idDXContext, pCmd, cUAViewId, paUAViewId);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DRAW_INDEXED_INSTANCED_INDIRECT 1251 */
static int vmsvga3dCmdDXDrawIndexedInstancedIndirect(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDrawIndexedInstancedIndirect const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDrawIndexedInstancedIndirect(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DRAW_INSTANCED_INDIRECT 1252 */
static int vmsvga3dCmdDXDrawInstancedIndirect(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDrawInstancedIndirect const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDrawInstancedIndirect(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DISPATCH 1253 */
static int vmsvga3dCmdDXDispatch(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDispatch const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDispatch(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DISPATCH_INDIRECT 1254 */
static int vmsvga3dCmdDXDispatchIndirect(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDispatchIndirect const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dDXDispatchIndirect(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_WRITE_ZERO_SURFACE 1255 */
static int vmsvga3dCmdWriteZeroSurface(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdWriteZeroSurface const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dWriteZeroSurface(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_HINT_ZERO_SURFACE 1256 */
static int vmsvga3dCmdHintZeroSurface(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdHintZeroSurface const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dHintZeroSurface(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_TRANSFER_TO_BUFFER 1257 */
static int vmsvga3dCmdDXTransferToBuffer(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXTransferToBuffer const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dDXTransferToBuffer(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_STRUCTURE_COUNT 1258 */
static int vmsvga3dCmdDXSetStructureCount(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetStructureCount const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXSetStructureCount(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_LOGICOPS_BITBLT 1259 */
static int vmsvga3dCmdLogicOpsBitBlt(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdLogicOpsBitBlt const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dLogicOpsBitBlt(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_LOGICOPS_TRANSBLT 1260 */
static int vmsvga3dCmdLogicOpsTransBlt(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdLogicOpsTransBlt const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dLogicOpsTransBlt(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_LOGICOPS_STRETCHBLT 1261 */
static int vmsvga3dCmdLogicOpsStretchBlt(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdLogicOpsStretchBlt const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dLogicOpsStretchBlt(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_LOGICOPS_COLORFILL 1262 */
static int vmsvga3dCmdLogicOpsColorFill(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdLogicOpsColorFill const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dLogicOpsColorFill(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_LOGICOPS_ALPHABLEND 1263 */
static int vmsvga3dCmdLogicOpsAlphaBlend(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdLogicOpsAlphaBlend const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dLogicOpsAlphaBlend(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_LOGICOPS_CLEARTYPEBLEND 1264 */
static int vmsvga3dCmdLogicOpsClearTypeBlend(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdLogicOpsClearTypeBlend const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dLogicOpsClearTypeBlend(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DEFINE_GB_SURFACE_V4 1267 */
static int vmsvga3dCmdDefineGBSurface_v4(PVGASTATECC pThisCC, SVGA3dCmdDefineGBSurface_v4 const *pCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    /* Update the entry in the pSvgaR3State->pGboOTableSurface. */
    SVGAOTableSurfaceEntry entry;
    RT_ZERO(entry);
    entry.format = pCmd->format;
    entry.surface1Flags = (uint32_t)(pCmd->surfaceFlags);
    entry.numMipLevels = pCmd->numMipLevels;
    entry.multisampleCount = pCmd->multisampleCount;
    entry.autogenFilter = pCmd->autogenFilter;
    entry.size = pCmd->size;
    entry.mobid = SVGA_ID_INVALID;
    entry.arraySize = pCmd->arraySize;
    // entry.mobPitch = 0;
    entry.surface2Flags = (uint32_t)(pCmd->surfaceFlags >> UINT64_C(32));
    entry.multisamplePattern = pCmd->multisamplePattern;
    entry.qualityLevel = pCmd->qualityLevel;
    entry.bufferByteStride = pCmd->bufferByteStride;
    // entry.minLOD = 0;

    int rc = vmsvgaR3OTableWrite(pSvgaR3State, &pSvgaR3State->aGboOTables[SVGA_OTABLE_SURFACE],
                                 pCmd->sid, SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, &entry, sizeof(entry));
    if (RT_SUCCESS(rc))
    {
        /* Create the host surface. */
        vmsvga3dSurfaceDefine(pThisCC, pCmd->sid, pCmd->surfaceFlags, pCmd->format,
                              pCmd->multisampleCount, pCmd->multisamplePattern, pCmd->qualityLevel, pCmd->autogenFilter,
                              pCmd->numMipLevels, &pCmd->size, pCmd->arraySize, pCmd->bufferByteStride, /* fAllocMipLevels = */ false);
    }
    return rc;
#else
    RT_NOREF(pThisCC, pCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_CS_UA_VIEWS 1268 */
static int vmsvga3dCmdDXSetCSUAViews(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetCSUAViews const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    SVGA3dUAViewId const *paUAViewId = (SVGA3dUAViewId *)&pCmd[1];
    uint32_t const cUAViewId = (cbCmd - sizeof(*pCmd)) / sizeof(SVGA3dUAViewId);
    return vmsvga3dDXSetCSUAViews(pThisCC, idDXContext, pCmd, cUAViewId, paUAViewId);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_MIN_LOD 1269 */
static int vmsvga3dCmdDXSetMinLOD(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetMinLOD const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dDXSetMinLOD(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_VIEW_V2 1272 */
static int vmsvga3dCmdDXDefineDepthStencilView_v2(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDefineDepthStencilView_v2 const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDefineDepthStencilView(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_DEFINE_STREAMOUTPUT_WITH_MOB 1273 */
static int vmsvga3dCmdDXDefineStreamOutputWithMob(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXDefineStreamOutputWithMob const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXDefineStreamOutputWithMob(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_SET_SHADER_IFACE 1274 */
static int vmsvga3dCmdDXSetShaderIface(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXSetShaderIface const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dDXSetShaderIface(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_BIND_STREAMOUTPUT 1275 */
static int vmsvga3dCmdDXBindStreamOutput(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXBindStreamOutput const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dDXBindStreamOutput(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_SURFACE_STRETCHBLT_NON_MS_TO_MS 1276 */
static int vmsvga3dCmdSurfaceStretchBltNonMSToMS(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdSurfaceStretchBltNonMSToMS const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dSurfaceStretchBltNonMSToMS(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_DX_BIND_SHADER_IFACE 1277 */
static int vmsvga3dCmdDXBindShaderIface(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdDXBindShaderIface const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    RT_NOREF(pSvgaR3State, pCmd, cbCmd);
    return vmsvga3dDXBindShaderIface(pThisCC, idDXContext);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* SVGA_3D_CMD_VB_DX_CLEAR_RENDERTARGET_VIEW_REGION  1083 */
static int vmsvga3dCmdVBDXClearRenderTargetViewRegion(PVGASTATECC pThisCC, uint32_t idDXContext, SVGA3dCmdVBDXClearRenderTargetViewRegion *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    SVGASignedRect const *paRect = (SVGASignedRect *)&pCmd[1];
    uint32_t const cRect = (cbCmd - sizeof(*pCmd)) / sizeof(SVGASignedRect);
    return vmsvga3dVBDXClearRenderTargetViewRegion(pThisCC, idDXContext, pCmd, cRect, paRect);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_DEFINE_VIDEO_PROCESSOR  VBSVGA_3D_CMD_BASE + 0 */
static int vmsvga3dVBCmdDXDefineVideoProcessor(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXDefineVideoProcessor *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXDefineVideoProcessor(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_DEFINE_VIDEO_DECODER_OUTPUT_VIEW  VBSVGA_3D_CMD_BASE + 1 */
static int vmsvga3dVBCmdDXDefineVideoDecoderOutputView(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXDefineVideoDecoderOutputView *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXDefineVideoDecoderOutputView(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_DEFINE_VIDEO_DECODER  VBSVGA_3D_CMD_BASE + 2 */
static int vmsvga3dVBCmdDXDefineVideoDecoder(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXDefineVideoDecoder *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXDefineVideoDecoder(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_DECODER_BEGIN_FRAME  VBSVGA_3D_CMD_BASE + 3 */
static int vmsvga3dVBCmdDXVideoDecoderBeginFrame(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoDecoderBeginFrame *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoDecoderBeginFrame(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_DECODER_SUBMIT_BUFFERS  VBSVGA_3D_CMD_BASE + 4 */
static int vmsvga3dVBCmdDXVideoDecoderSubmitBuffers(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoDecoderSubmitBuffers *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    VBSVGA3dVideoDecoderBufferDesc const *paBufferDesc = (VBSVGA3dVideoDecoderBufferDesc *)&pCmd[1];
    uint32_t const cBuffer = (cbCmd - sizeof(*pCmd)) / sizeof(VBSVGA3dVideoDecoderBufferDesc);
    return vmsvga3dVBDXVideoDecoderSubmitBuffers(pThisCC, idDXContext, pCmd, cBuffer, paBufferDesc);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_DECODER_END_FRAME  VBSVGA_3D_CMD_BASE + 5 */
static int vmsvga3dVBCmdDXVideoDecoderEndFrame(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoDecoderEndFrame *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoDecoderEndFrame(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_DEFINE_VIDEO_PROCESSOR_INPUT_VIEW  VBSVGA_3D_CMD_BASE + 6 */
static int vmsvga3dVBCmdDXDefineVideoProcessorInputView(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXDefineVideoProcessorInputView *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXDefineVideoProcessorInputView(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_DEFINE_VIDEO_PROCESSOR_OUTPUT_VIEW  VBSVGA_3D_CMD_BASE + 7 */
static int vmsvga3dVBCmdDXDefineVideoProcessorOutputView(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXDefineVideoProcessorOutputView *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXDefineVideoProcessorOutputView(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_BLT  VBSVGA_3D_CMD_BASE + 8 */
static int vmsvga3dVBCmdDXVideoProcessorBlt(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorBlt *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    return vmsvga3dVBDXVideoProcessorBlt(pThisCC, idDXContext, pCmd, cbCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_DESTROY_VIDEO_DECODER  VBSVGA_3D_CMD_BASE + 9 */
static int vmsvga3dVBCmdDXDestroyVideoDecoder(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXDestroyVideoDecoder *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXDestroyVideoDecoder(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_DESTROY_VIDEO_DECODER_OUTPUT_VIEW  VBSVGA_3D_CMD_BASE + 10 */
static int vmsvga3dVBCmdDXDestroyVideoDecoderOutputView(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXDestroyVideoDecoderOutputView *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXDestroyVideoDecoderOutputView(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_DESTROY_VIDEO_PROCESSOR  VBSVGA_3D_CMD_BASE + 11 */
static int vmsvga3dVBCmdDXDestroyVideoProcessor(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXDestroyVideoProcessor *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXDestroyVideoProcessor(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_DESTROY_VIDEO_PROCESSOR_INPUT_VIEW  VBSVGA_3D_CMD_BASE + 12 */
static int vmsvga3dVBCmdDXDestroyVideoProcessorInputView(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXDestroyVideoProcessorInputView *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXDestroyVideoProcessorInputView(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_DESTROY_VIDEO_PROCESSOR_OUTPUT_VIEW  VBSVGA_3D_CMD_BASE + 13 */
static int vmsvga3dVBCmdDXDestroyVideoProcessorOutputView(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXDestroyVideoProcessorOutputView *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXDestroyVideoProcessorOutputView(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_TARGET_RECT  VBSVGA_3D_CMD_BASE + 14 */
static int vmsvga3dVBCmdDXVideoProcessorSetOutputTargetRect(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetOutputTargetRect const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetOutputTargetRect(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_BACKGROUND_COLOR  VBSVGA_3D_CMD_BASE + 15 */
static int vmsvga3dVBCmdDXVideoProcessorSetOutputBackgroundColor(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetOutputBackgroundColor const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetOutputBackgroundColor(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_COLOR_SPACE  VBSVGA_3D_CMD_BASE + 16 */
static int vmsvga3dVBCmdDXVideoProcessorSetOutputColorSpace(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetOutputColorSpace const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetOutputColorSpace(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_ALPHA_FILL_MODE  VBSVGA_3D_CMD_BASE + 17 */
static int vmsvga3dVBCmdDXVideoProcessorSetOutputAlphaFillMode(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetOutputAlphaFillMode const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetOutputAlphaFillMode(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_CONSTRICTION  VBSVGA_3D_CMD_BASE + 18 */
static int vmsvga3dVBCmdDXVideoProcessorSetOutputConstriction(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetOutputConstriction const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetOutputConstriction(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_STEREO_MODE  VBSVGA_3D_CMD_BASE + 19 */
static int vmsvga3dVBCmdDXVideoProcessorSetOutputStereoMode(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetOutputStereoMode const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetOutputStereoMode(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_FRAME_FORMAT  VBSVGA_3D_CMD_BASE + 20 */
static int vmsvga3dVBCmdDXVideoProcessorSetStreamFrameFormat(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetStreamFrameFormat const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetStreamFrameFormat(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_COLOR_SPACE  VBSVGA_3D_CMD_BASE + 21 */
static int vmsvga3dVBCmdDXVideoProcessorSetStreamColorSpace(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetStreamColorSpace const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetStreamColorSpace(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_OUTPUT_RATE  VBSVGA_3D_CMD_BASE + 22 */
static int vmsvga3dVBCmdDXVideoProcessorSetStreamOutputRate(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetStreamOutputRate const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetStreamOutputRate(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_SOURCE_RECT  VBSVGA_3D_CMD_BASE + 23 */
static int vmsvga3dVBCmdDXVideoProcessorSetStreamSourceRect(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetStreamSourceRect const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetStreamSourceRect(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_DEST_RECT  VBSVGA_3D_CMD_BASE + 24 */
static int vmsvga3dVBCmdDXVideoProcessorSetStreamDestRect(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetStreamDestRect const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetStreamDestRect(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_ALPHA  VBSVGA_3D_CMD_BASE + 25 */
static int vmsvga3dVBCmdDXVideoProcessorSetStreamAlpha(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetStreamAlpha const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetStreamAlpha(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_PALETTE  VBSVGA_3D_CMD_BASE + 26, */
static int vmsvga3dVBCmdDXVideoProcessorSetStreamPalette(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetStreamPalette const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    uint32_t const *paEntries = (uint32_t *)&pCmd[1];
    uint32_t const cEntries = (cbCmd - sizeof(*pCmd)) / sizeof(uint32_t);
    return vmsvga3dVBDXVideoProcessorSetStreamPalette(pThisCC, idDXContext, pCmd, cEntries, paEntries);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_PIXEL_ASPECT_RATIO  VBSVGA_3D_CMD_BASE + 27 */
static int vmsvga3dVBCmdDXVideoProcessorSetStreamPixelAspectRatio(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetStreamPixelAspectRatio const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetStreamPixelAspectRatio(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_LUMA_KEY  VBSVGA_3D_CMD_BASE + 28 */
static int vmsvga3dVBCmdDXVideoProcessorSetStreamLumaKey(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetStreamLumaKey const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetStreamLumaKey(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_STEREO_FORMAT  VBSVGA_3D_CMD_BASE + 29 */
static int vmsvga3dVBCmdDXVideoProcessorSetStreamStereoFormat(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetStreamStereoFormat const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetStreamStereoFormat(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_AUTO_PROCESSING_MODE  VBSVGA_3D_CMD_BASE + 30 */
static int vmsvga3dVBCmdDXVideoProcessorSetStreamAutoProcessingMode(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetStreamAutoProcessingMode const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetStreamAutoProcessingMode(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_FILTER  VBSVGA_3D_CMD_BASE + 31 */
static int vmsvga3dVBCmdDXVideoProcessorSetStreamFilter(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetStreamFilter const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetStreamFilter(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_ROTATION  VBSVGA_3D_CMD_BASE + 32 */
static int vmsvga3dVBCmdDXVideoProcessorSetStreamRotation(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXVideoProcessorSetStreamRotation const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXVideoProcessorSetStreamRotation(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_GET_VIDEO_CAPABILITY  VBSVGA_3D_CMD_BASE + 33 */
static int vmsvga3dVBCmdDXGetVideoCapability(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXGetVideoCapability const *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(cbCmd);
    return vmsvga3dVBDXGetVideoCapability(pThisCC, idDXContext, pCmd);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_CLEAR_RTV  VBSVGA_3D_CMD_BASE + 34 */
static int vmsvga3dVBCmdDXClearRTV(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXClearView *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    SVGASignedRect const *paRect = (SVGASignedRect *)&pCmd[1];
    uint32_t const cRect = (cbCmd - sizeof(*pCmd)) / sizeof(SVGASignedRect);
    return vmsvga3dVBDXClearRTV(pThisCC, idDXContext, pCmd, cRect, cRect ? paRect : NULL);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_CLEAR_UAV  VBSVGA_3D_CMD_BASE + 35 */
static int vmsvga3dVBCmdDXClearUAV(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXClearView *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    SVGASignedRect const *paRect = (SVGASignedRect *)&pCmd[1];
    uint32_t const cRect = (cbCmd - sizeof(*pCmd)) / sizeof(SVGASignedRect);
    return vmsvga3dVBDXClearUAV(pThisCC, idDXContext, pCmd, cRect, cRect ? paRect : NULL);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_CLEAR_VDOV  VBSVGA_3D_CMD_BASE + 36 */
static int vmsvga3dVBCmdDXClearVDOV(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXClearView *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    SVGASignedRect const *paRect = (SVGASignedRect *)&pCmd[1];
    uint32_t const cRect = (cbCmd - sizeof(*pCmd)) / sizeof(SVGASignedRect);
    return vmsvga3dVBDXClearVDOV(pThisCC, idDXContext, pCmd, cRect, cRect ? paRect : NULL);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_CLEAR_VPIV  VBSVGA_3D_CMD_BASE + 37 */
static int vmsvga3dVBCmdDXClearVPIV(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXClearView *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    SVGASignedRect const *paRect = (SVGASignedRect *)&pCmd[1];
    uint32_t const cRect = (cbCmd - sizeof(*pCmd)) / sizeof(SVGASignedRect);
    return vmsvga3dVBDXClearVPIV(pThisCC, idDXContext, pCmd, cRect, cRect ? paRect : NULL);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/* VBSVGA_3D_CMD_DX_CLEAR_VPOV  VBSVGA_3D_CMD_BASE + 38 */
static int vmsvga3dVBCmdDXClearVPOV(PVGASTATECC pThisCC, uint32_t idDXContext, VBSVGA3dCmdDXClearView *pCmd, uint32_t cbCmd)
{
#ifdef VMSVGA3D_DX
    //DEBUG_BREAKPOINT_TEST();
    SVGASignedRect const *paRect = (SVGASignedRect *)&pCmd[1];
    uint32_t const cRect = (cbCmd - sizeof(*pCmd)) / sizeof(SVGASignedRect);
    return vmsvga3dVBDXClearVPOV(pThisCC, idDXContext, pCmd, cRect, cRect ? paRect : NULL);
#else
    RT_NOREF(pThisCC, idDXContext, pCmd, cbCmd);
    return VERR_NOT_SUPPORTED;
#endif
}


/** @def VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK
 * Check that the 3D command has at least a_cbMin of payload bytes after the
 * header.  Will break out of the switch if it doesn't.
 */
# define VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(a_cbMin) \
     if (1) { \
          AssertMsgBreak(cbCmd >= (a_cbMin), ("size=%#x a_cbMin=%#zx\n", cbCmd, (size_t)(a_cbMin))); \
          RT_UNTRUSTED_VALIDATED_FENCE(); \
     } else do {} while (0)

# define VMSVGA_3D_CMD_NOTIMPL() \
     if (1) { \
          AssertMsgFailed(("Not implemented %d %s\n", enmCmdId, vmsvgaR3FifoCmdToString(enmCmdId))); \
     } else do {} while (0)

/** SVGA_3D_CMD_* handler.
 * This function parses the command and calls the corresponding command handler.
 *
 * @param   pThis       The shared VGA/VMSVGA state.
 * @param   pThisCC     The VGA/VMSVGA state for the current context.
 * @param   idDXContext VGPU10 DX context of the commands or SVGA3D_INVALID_ID if they are not for a specific context.
 * @param   enmCmdId    SVGA_3D_CMD_* command identifier.
 * @param   cbCmd       Size of the command in bytes.
 * @param   pvCmd       Pointer to the command.
 * @returns VBox status code if an error was detected parsing a command.
 */
int vmsvgaR3Process3dCmd(PVGASTATE pThis, PVGASTATECC pThisCC, uint32_t idDXContext, SVGAFifo3dCmdId enmCmdId, uint32_t cbCmd, void const *pvCmd)
{
    int rcParse = VINF_SUCCESS;
    PVMSVGAR3STATE pSvgaR3State = pThisCC->svga.pSvgaR3State;

    switch (enmCmdId)
    {
    case SVGA_3D_CMD_SURFACE_DEFINE:
    {
        SVGA3dCmdDefineSurface *pCmd = (SVGA3dCmdDefineSurface *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSurfaceDefine);

        SVGA3dCmdDefineSurface_v2 cmd;
        cmd.sid              = pCmd->sid;
        cmd.surfaceFlags     = pCmd->surfaceFlags;
        cmd.format           = pCmd->format;
        memcpy(cmd.face, pCmd->face, sizeof(cmd.face));
        cmd.multisampleCount = 0;
        cmd.autogenFilter    = SVGA3D_TEX_FILTER_NONE;

        uint32_t const cMipLevelSizes = (cbCmd - sizeof(*pCmd)) / sizeof(SVGA3dSize);
        vmsvga3dCmdDefineSurface(pThisCC, &cmd, cMipLevelSizes, (SVGA3dSize *)(pCmd + 1));
# ifdef DEBUG_GMR_ACCESS
        VMR3ReqCallWaitU(PDMDevHlpGetUVM(pDevIns), VMCPUID_ANY, (PFNRT)vmsvgaR3ResetGmrHandlers, 1, pThis);
# endif
        break;
    }

    case SVGA_3D_CMD_SURFACE_DEFINE_V2:
    {
        SVGA3dCmdDefineSurface_v2 *pCmd = (SVGA3dCmdDefineSurface_v2 *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSurfaceDefineV2);

        uint32_t const cMipLevelSizes = (cbCmd - sizeof(*pCmd)) / sizeof(SVGA3dSize);
        vmsvga3dCmdDefineSurface(pThisCC, pCmd, cMipLevelSizes, (SVGA3dSize *)(pCmd + 1));
# ifdef DEBUG_GMR_ACCESS
        VMR3ReqCallWaitU(PDMDevHlpGetUVM(pDevIns), VMCPUID_ANY, (PFNRT)vmsvgaR3ResetGmrHandlers, 1, pThis);
# endif
        break;
    }

    case SVGA_3D_CMD_SURFACE_DESTROY:
    {
        SVGA3dCmdDestroySurface *pCmd = (SVGA3dCmdDestroySurface *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSurfaceDestroy);

        vmsvga3dSurfaceDestroy(pThisCC, pCmd->sid);
        break;
    }

    case SVGA_3D_CMD_SURFACE_COPY:
    {
        SVGA3dCmdSurfaceCopy *pCmd = (SVGA3dCmdSurfaceCopy *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSurfaceCopy);

        uint32_t const cCopyBoxes = (cbCmd - sizeof(pCmd)) / sizeof(SVGA3dCopyBox);
        vmsvga3dSurfaceCopy(pThisCC, pCmd->dest, pCmd->src, cCopyBoxes, (SVGA3dCopyBox *)(pCmd + 1));
        break;
    }

    case SVGA_3D_CMD_SURFACE_STRETCHBLT:
    {
        SVGA3dCmdSurfaceStretchBlt *pCmd = (SVGA3dCmdSurfaceStretchBlt *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSurfaceStretchBlt);

        vmsvga3dSurfaceStretchBlt(pThis, pThisCC, &pCmd->dest, &pCmd->boxDest,
                                  &pCmd->src, &pCmd->boxSrc, pCmd->mode);
        break;
    }

    case SVGA_3D_CMD_SURFACE_DMA:
    {
        SVGA3dCmdSurfaceDMA *pCmd = (SVGA3dCmdSurfaceDMA *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSurfaceDma);

        uint64_t u64NanoTS = 0;
        if (LogRelIs3Enabled())
            u64NanoTS = RTTimeNanoTS();
        uint32_t const cCopyBoxes = (cbCmd - sizeof(*pCmd)) / sizeof(SVGA3dCopyBox);
        STAM_PROFILE_START(&pSvgaR3State->StatR3Cmd3dSurfaceDmaProf, a);
        vmsvga3dSurfaceDMA(pThis, pThisCC, pCmd->guest, pCmd->host, pCmd->transfer,
                           cCopyBoxes, (SVGA3dCopyBox *)(pCmd + 1));
        STAM_PROFILE_STOP(&pSvgaR3State->StatR3Cmd3dSurfaceDmaProf, a);
        if (LogRelIs3Enabled())
        {
            if (cCopyBoxes)
            {
                SVGA3dCopyBox *pFirstBox = (SVGA3dCopyBox *)(pCmd + 1);
                LogRel3(("VMSVGA: SURFACE_DMA: %d us %d boxes %d,%d %dx%d%s\n",
                         (RTTimeNanoTS() - u64NanoTS) / 1000ULL, cCopyBoxes,
                         pFirstBox->x, pFirstBox->y, pFirstBox->w, pFirstBox->h,
                         pCmd->transfer == SVGA3D_READ_HOST_VRAM ? " readback!!!" : ""));
            }
        }
        break;
    }

    case SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN:
    {
        SVGA3dCmdBlitSurfaceToScreen *pCmd = (SVGA3dCmdBlitSurfaceToScreen *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSurfaceScreen);

        static uint64_t u64FrameStartNanoTS = 0;
        static uint64_t u64ElapsedPerSecNano = 0;
        static int cFrames = 0;
        uint64_t u64NanoTS = 0;
        if (LogRelIs3Enabled())
            u64NanoTS = RTTimeNanoTS();
        uint32_t const cRects = (cbCmd - sizeof(*pCmd)) / sizeof(SVGASignedRect);
        STAM_REL_PROFILE_START(&pSvgaR3State->StatR3Cmd3dBlitSurfaceToScreenProf, a);
        vmsvga3dSurfaceBlitToScreen(pThis, pThisCC, pCmd->destScreenId, pCmd->destRect, pCmd->srcImage,
                                    pCmd->srcRect, cRects, (SVGASignedRect *)(pCmd + 1));
        STAM_REL_PROFILE_STOP(&pSvgaR3State->StatR3Cmd3dBlitSurfaceToScreenProf, a);
        if (LogRelIs3Enabled())
        {
            uint64_t u64ElapsedNano = RTTimeNanoTS() - u64NanoTS;
            u64ElapsedPerSecNano += u64ElapsedNano;

            SVGASignedRect *pFirstRect = cRects ? (SVGASignedRect *)(pCmd + 1) : &pCmd->destRect;
            LogRel3(("VMSVGA: SURFACE_TO_SCREEN: %d us %d rects %d,%d %dx%d\n",
                     (u64ElapsedNano) / 1000ULL, cRects,
                     pFirstRect->left, pFirstRect->top,
                     pFirstRect->right - pFirstRect->left, pFirstRect->bottom - pFirstRect->top));

            ++cFrames;
            if (u64NanoTS - u64FrameStartNanoTS >= UINT64_C(1000000000))
            {
                LogRel3(("VMSVGA: SURFACE_TO_SCREEN: FPS %d, elapsed %llu us\n",
                         cFrames, u64ElapsedPerSecNano / 1000ULL));
                u64FrameStartNanoTS = u64NanoTS;
                cFrames = 0;
                u64ElapsedPerSecNano = 0;
            }
        }
        break;
    }

    case SVGA_3D_CMD_CONTEXT_DEFINE:
    {
        SVGA3dCmdDefineContext *pCmd = (SVGA3dCmdDefineContext *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dContextDefine);

        vmsvga3dContextDefine(pThisCC, pCmd->cid);
        break;
    }

    case SVGA_3D_CMD_CONTEXT_DESTROY:
    {
        SVGA3dCmdDestroyContext *pCmd = (SVGA3dCmdDestroyContext *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dContextDestroy);

        vmsvga3dContextDestroy(pThisCC, pCmd->cid);
        break;
    }

    case SVGA_3D_CMD_SETTRANSFORM:
    {
        SVGA3dCmdSetTransform *pCmd = (SVGA3dCmdSetTransform *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSetTransform);

        vmsvga3dSetTransform(pThisCC, pCmd->cid, pCmd->type, pCmd->matrix);
        break;
    }

    case SVGA_3D_CMD_SETZRANGE:
    {
        SVGA3dCmdSetZRange *pCmd = (SVGA3dCmdSetZRange *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSetZRange);

        vmsvga3dSetZRange(pThisCC, pCmd->cid, pCmd->zRange);
        break;
    }

    case SVGA_3D_CMD_SETRENDERSTATE:
    {
        SVGA3dCmdSetRenderState *pCmd = (SVGA3dCmdSetRenderState *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSetRenderState);

        uint32_t const cRenderStates = (cbCmd - sizeof(*pCmd)) / sizeof(SVGA3dRenderState);
        vmsvga3dSetRenderState(pThisCC, pCmd->cid, cRenderStates, (SVGA3dRenderState *)(pCmd + 1));
        break;
    }

    case SVGA_3D_CMD_SETRENDERTARGET:
    {
        SVGA3dCmdSetRenderTarget *pCmd = (SVGA3dCmdSetRenderTarget *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSetRenderTarget);

        vmsvga3dSetRenderTarget(pThisCC, pCmd->cid, pCmd->type, pCmd->target);
        break;
    }

    case SVGA_3D_CMD_SETTEXTURESTATE:
    {
        SVGA3dCmdSetTextureState *pCmd = (SVGA3dCmdSetTextureState *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSetTextureState);

        uint32_t const cTextureStates = (cbCmd - sizeof(*pCmd)) / sizeof(SVGA3dTextureState);
        vmsvga3dSetTextureState(pThisCC, pCmd->cid, cTextureStates, (SVGA3dTextureState *)(pCmd + 1));
        break;
    }

    case SVGA_3D_CMD_SETMATERIAL:
    {
        SVGA3dCmdSetMaterial *pCmd = (SVGA3dCmdSetMaterial *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSetMaterial);

        vmsvga3dSetMaterial(pThisCC, pCmd->cid, pCmd->face, &pCmd->material);
        break;
    }

    case SVGA_3D_CMD_SETLIGHTDATA:
    {
        SVGA3dCmdSetLightData *pCmd = (SVGA3dCmdSetLightData *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSetLightData);

        vmsvga3dSetLightData(pThisCC, pCmd->cid, pCmd->index, &pCmd->data);
        break;
    }

    case SVGA_3D_CMD_SETLIGHTENABLED:
    {
        SVGA3dCmdSetLightEnabled *pCmd = (SVGA3dCmdSetLightEnabled *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSetLightEnable);

        vmsvga3dSetLightEnabled(pThisCC, pCmd->cid, pCmd->index, pCmd->enabled);
        break;
    }

    case SVGA_3D_CMD_SETVIEWPORT:
    {
        SVGA3dCmdSetViewport *pCmd = (SVGA3dCmdSetViewport *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSetViewPort);

        vmsvga3dSetViewPort(pThisCC, pCmd->cid, &pCmd->rect);
        break;
    }

    case SVGA_3D_CMD_SETCLIPPLANE:
    {
        SVGA3dCmdSetClipPlane *pCmd = (SVGA3dCmdSetClipPlane *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSetClipPlane);

        vmsvga3dSetClipPlane(pThisCC, pCmd->cid, pCmd->index, pCmd->plane);
        break;
    }

    case SVGA_3D_CMD_CLEAR:
    {
        SVGA3dCmdClear  *pCmd = (SVGA3dCmdClear *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dClear);

        uint32_t const cRects = (cbCmd - sizeof(*pCmd)) / sizeof(SVGA3dRect);
        vmsvga3dCommandClear(pThisCC, pCmd->cid, pCmd->clearFlag, pCmd->color, pCmd->depth, pCmd->stencil, cRects, (SVGA3dRect *)(pCmd + 1));
        break;
    }

    case SVGA_3D_CMD_PRESENT:
    case SVGA_3D_CMD_PRESENT_READBACK: /** @todo SVGA_3D_CMD_PRESENT_READBACK isn't quite the same as present... */
    {
        SVGA3dCmdPresent *pCmd = (SVGA3dCmdPresent *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        if (enmCmdId == SVGA_3D_CMD_PRESENT)
            STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dPresent);
        else
            STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dPresentReadBack);

        uint32_t const cRects = (cbCmd - sizeof(*pCmd)) / sizeof(SVGA3dCopyRect);
        STAM_PROFILE_START(&pSvgaR3State->StatR3Cmd3dPresentProf, a);
        vmsvga3dCommandPresent(pThis, pThisCC, pCmd->sid, cRects, (SVGA3dCopyRect *)(pCmd + 1));
        STAM_PROFILE_STOP(&pSvgaR3State->StatR3Cmd3dPresentProf, a);
        break;
    }

    case SVGA_3D_CMD_SHADER_DEFINE:
    {
        SVGA3dCmdDefineShader *pCmd = (SVGA3dCmdDefineShader *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dShaderDefine);

        uint32_t const cbData = (cbCmd - sizeof(*pCmd));
        vmsvga3dShaderDefine(pThisCC, pCmd->cid, pCmd->shid, pCmd->type, cbData, (uint32_t *)(pCmd + 1));
        break;
    }

    case SVGA_3D_CMD_SHADER_DESTROY:
    {
        SVGA3dCmdDestroyShader *pCmd = (SVGA3dCmdDestroyShader *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dShaderDestroy);

        vmsvga3dShaderDestroy(pThisCC, pCmd->cid, pCmd->shid, pCmd->type);
        break;
    }

    case SVGA_3D_CMD_SET_SHADER:
    {
        SVGA3dCmdSetShader *pCmd = (SVGA3dCmdSetShader *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSetShader);

        vmsvga3dShaderSet(pThisCC, NULL, pCmd->cid, pCmd->type, pCmd->shid);
        break;
    }

    case SVGA_3D_CMD_SET_SHADER_CONST:
    {
        SVGA3dCmdSetShaderConst *pCmd = (SVGA3dCmdSetShaderConst *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSetShaderConst);

        uint32_t const cRegisters = (cbCmd - sizeof(*pCmd)) / sizeof(pCmd->values) + 1;
        vmsvga3dShaderSetConst(pThisCC, pCmd->cid, pCmd->reg, pCmd->type, pCmd->ctype, cRegisters, pCmd->values);
        break;
    }

    case SVGA_3D_CMD_DRAW_PRIMITIVES:
    {
        SVGA3dCmdDrawPrimitives *pCmd = (SVGA3dCmdDrawPrimitives *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dDrawPrimitives);

        ASSERT_GUEST_STMT_BREAK(pCmd->numRanges <= SVGA3D_MAX_DRAW_PRIMITIVE_RANGES, rcParse = VERR_INVALID_PARAMETER);
        ASSERT_GUEST_STMT_BREAK(pCmd->numVertexDecls <= SVGA3D_MAX_VERTEX_ARRAYS, rcParse = VERR_INVALID_PARAMETER);
        uint32_t const cbRangesAndVertexDecls = pCmd->numVertexDecls * sizeof(SVGA3dVertexDecl)
                                              + pCmd->numRanges * sizeof(SVGA3dPrimitiveRange);
        ASSERT_GUEST_STMT_BREAK(cbRangesAndVertexDecls <= cbCmd - sizeof(*pCmd), rcParse = VERR_INVALID_PARAMETER);

        uint32_t const cVertexDivisor = (cbCmd - sizeof(*pCmd) - cbRangesAndVertexDecls) / sizeof(uint32_t);
        ASSERT_GUEST_STMT_BREAK(!cVertexDivisor || cVertexDivisor == pCmd->numVertexDecls, rcParse = VERR_INVALID_PARAMETER);
        RT_UNTRUSTED_VALIDATED_FENCE();

        SVGA3dVertexDecl     *pVertexDecl    = (SVGA3dVertexDecl *)(pCmd + 1);
        SVGA3dPrimitiveRange *pNumRange      = (SVGA3dPrimitiveRange *)&pVertexDecl[pCmd->numVertexDecls];
        SVGA3dVertexDivisor  *pVertexDivisor = cVertexDivisor ? (SVGA3dVertexDivisor *)&pNumRange[pCmd->numRanges] : NULL;

        STAM_PROFILE_START(&pSvgaR3State->StatR3Cmd3dDrawPrimitivesProf, a);
        vmsvga3dDrawPrimitives(pThisCC, pCmd->cid, pCmd->numVertexDecls, pVertexDecl, pCmd->numRanges,
                               pNumRange, cVertexDivisor, pVertexDivisor);
        STAM_PROFILE_STOP(&pSvgaR3State->StatR3Cmd3dDrawPrimitivesProf, a);
        break;
    }

    case SVGA_3D_CMD_SETSCISSORRECT:
    {
        SVGA3dCmdSetScissorRect *pCmd = (SVGA3dCmdSetScissorRect *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dSetScissorRect);

        vmsvga3dSetScissorRect(pThisCC, pCmd->cid, &pCmd->rect);
        break;
    }

    case SVGA_3D_CMD_BEGIN_QUERY:
    {
        SVGA3dCmdBeginQuery *pCmd = (SVGA3dCmdBeginQuery *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dBeginQuery);

        vmsvga3dQueryBegin(pThisCC, pCmd->cid, pCmd->type);
        break;
    }

    case SVGA_3D_CMD_END_QUERY:
    {
        SVGA3dCmdEndQuery *pCmd = (SVGA3dCmdEndQuery *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dEndQuery);

        vmsvga3dQueryEnd(pThisCC, pCmd->cid, pCmd->type);
        break;
    }

    case SVGA_3D_CMD_WAIT_FOR_QUERY:
    {
        SVGA3dCmdWaitForQuery *pCmd = (SVGA3dCmdWaitForQuery *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dWaitForQuery);

        vmsvga3dQueryWait(pThisCC, pCmd->cid, pCmd->type, pThis, &pCmd->guestResult);
        break;
    }

    case SVGA_3D_CMD_GENERATE_MIPMAPS:
    {
        SVGA3dCmdGenerateMipmaps *pCmd = (SVGA3dCmdGenerateMipmaps *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dGenerateMipmaps);

        vmsvga3dGenerateMipmaps(pThisCC, pCmd->sid, pCmd->filter);
        break;
    }

    case SVGA_3D_CMD_ACTIVATE_SURFACE:
        /* context id + surface id? */
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dActivateSurface);
        break;

    case SVGA_3D_CMD_DEACTIVATE_SURFACE:
        /* context id + surface id? */
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3Cmd3dDeactivateSurface);
        break;

    /*
     *
     * VPGU10: SVGA_CAP_GBOBJECTS+ commands.
     *
     */
    case SVGA_3D_CMD_SCREEN_DMA:
    {
        SVGA3dCmdScreenDMA *pCmd = (SVGA3dCmdScreenDMA *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    /* case SVGA_3D_CMD_DEAD1: New SVGA_3D_CMD_VB_DX_CLEAR_RENDERTARGET_VIEW_REGION */
    case SVGA_3D_CMD_DEAD2:
    case SVGA_3D_CMD_DEAD12: /* Old SVGA_3D_CMD_LOGICOPS_BITBLT */
    case SVGA_3D_CMD_DEAD13: /* Old SVGA_3D_CMD_LOGICOPS_TRANSBLT */
    case SVGA_3D_CMD_DEAD14: /* Old SVGA_3D_CMD_LOGICOPS_STRETCHBLT */
    case SVGA_3D_CMD_DEAD15: /* Old SVGA_3D_CMD_LOGICOPS_COLORFILL */
    case SVGA_3D_CMD_DEAD16: /* Old SVGA_3D_CMD_LOGICOPS_ALPHABLEND */
    case SVGA_3D_CMD_DEAD17: /* Old SVGA_3D_CMD_LOGICOPS_CLEARTYPEBLEND */
    {
        VMSVGA_3D_CMD_NOTIMPL();
        break;
    }

    case SVGA_3D_CMD_SET_OTABLE_BASE:
    {
        SVGA3dCmdSetOTableBase *pCmd = (SVGA3dCmdSetOTableBase *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdSetOTableBase(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_READBACK_OTABLE:
    {
        SVGA3dCmdReadbackOTable *pCmd = (SVGA3dCmdReadbackOTable *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_DEFINE_GB_MOB:
    {
        SVGA3dCmdDefineGBMob *pCmd = (SVGA3dCmdDefineGBMob *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdDefineGBMob(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_DESTROY_GB_MOB:
    {
        SVGA3dCmdDestroyGBMob *pCmd = (SVGA3dCmdDestroyGBMob *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdDestroyGBMob(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_DEAD3:
    {
        VMSVGA_3D_CMD_NOTIMPL();
        break;
    }

    case SVGA_3D_CMD_UPDATE_GB_MOB_MAPPING:
    {
        SVGA3dCmdUpdateGBMobMapping *pCmd = (SVGA3dCmdUpdateGBMobMapping *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_DEFINE_GB_SURFACE:
    {
        SVGA3dCmdDefineGBSurface *pCmd = (SVGA3dCmdDefineGBSurface *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdDefineGBSurface(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_DESTROY_GB_SURFACE:
    {
        SVGA3dCmdDestroyGBSurface *pCmd = (SVGA3dCmdDestroyGBSurface *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdDestroyGBSurface(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_BIND_GB_SURFACE:
    {
        SVGA3dCmdBindGBSurface *pCmd = (SVGA3dCmdBindGBSurface *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdBindGBSurface(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_COND_BIND_GB_SURFACE:
    {
        SVGA3dCmdCondBindGBSurface *pCmd = (SVGA3dCmdCondBindGBSurface *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_UPDATE_GB_IMAGE:
    {
        SVGA3dCmdUpdateGBImage *pCmd = (SVGA3dCmdUpdateGBImage *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdUpdateGBImage(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_UPDATE_GB_SURFACE:
    {
        SVGA3dCmdUpdateGBSurface *pCmd = (SVGA3dCmdUpdateGBSurface *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdUpdateGBSurface(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_READBACK_GB_IMAGE:
    {
        SVGA3dCmdReadbackGBImage *pCmd = (SVGA3dCmdReadbackGBImage *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdReadbackGBImage(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_READBACK_GB_SURFACE:
    {
        SVGA3dCmdReadbackGBSurface *pCmd = (SVGA3dCmdReadbackGBSurface *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdReadbackGBSurface(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_INVALIDATE_GB_IMAGE:
    {
        SVGA3dCmdInvalidateGBImage *pCmd = (SVGA3dCmdInvalidateGBImage *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdInvalidateGBImage(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_INVALIDATE_GB_SURFACE:
    {
        SVGA3dCmdInvalidateGBSurface *pCmd = (SVGA3dCmdInvalidateGBSurface *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdInvalidateGBSurface(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_DEFINE_GB_CONTEXT:
    {
        SVGA3dCmdDefineGBContext *pCmd = (SVGA3dCmdDefineGBContext *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_DESTROY_GB_CONTEXT:
    {
        SVGA3dCmdDestroyGBContext *pCmd = (SVGA3dCmdDestroyGBContext *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_BIND_GB_CONTEXT:
    {
        SVGA3dCmdBindGBContext *pCmd = (SVGA3dCmdBindGBContext *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_READBACK_GB_CONTEXT:
    {
        SVGA3dCmdReadbackGBContext *pCmd = (SVGA3dCmdReadbackGBContext *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_INVALIDATE_GB_CONTEXT:
    {
        SVGA3dCmdInvalidateGBContext *pCmd = (SVGA3dCmdInvalidateGBContext *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_DEFINE_GB_SHADER:
    {
        SVGA3dCmdDefineGBShader *pCmd = (SVGA3dCmdDefineGBShader *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_DESTROY_GB_SHADER:
    {
        SVGA3dCmdDestroyGBShader *pCmd = (SVGA3dCmdDestroyGBShader *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_BIND_GB_SHADER:
    {
        SVGA3dCmdBindGBShader *pCmd = (SVGA3dCmdBindGBShader *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_SET_OTABLE_BASE64:
    {
        SVGA3dCmdSetOTableBase64 *pCmd = (SVGA3dCmdSetOTableBase64 *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdSetOTableBase64(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_BEGIN_GB_QUERY:
    {
        SVGA3dCmdBeginGBQuery *pCmd = (SVGA3dCmdBeginGBQuery *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_END_GB_QUERY:
    {
        SVGA3dCmdEndGBQuery *pCmd = (SVGA3dCmdEndGBQuery *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_WAIT_FOR_GB_QUERY:
    {
        SVGA3dCmdWaitForGBQuery *pCmd = (SVGA3dCmdWaitForGBQuery *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_NOP:
    {
        /* Apparently there is nothing to do. */
        break;
    }

    case SVGA_3D_CMD_ENABLE_GART:
    {
        SVGA3dCmdEnableGart *pCmd = (SVGA3dCmdEnableGart *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_DISABLE_GART:
    {
        /* No corresponding SVGA3dCmd structure. */
        VMSVGA_3D_CMD_NOTIMPL();
        break;
    }

    case SVGA_3D_CMD_MAP_MOB_INTO_GART:
    {
        SVGA3dCmdMapMobIntoGart *pCmd = (SVGA3dCmdMapMobIntoGart *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_UNMAP_GART_RANGE:
    {
        SVGA3dCmdUnmapGartRange *pCmd = (SVGA3dCmdUnmapGartRange *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_DEFINE_GB_SCREENTARGET:
    {
        SVGA3dCmdDefineGBScreenTarget *pCmd = (SVGA3dCmdDefineGBScreenTarget *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdDefineGBScreenTarget(pThis, pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_DESTROY_GB_SCREENTARGET:
    {
        SVGA3dCmdDestroyGBScreenTarget *pCmd = (SVGA3dCmdDestroyGBScreenTarget *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdDestroyGBScreenTarget(pThis, pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_BIND_GB_SCREENTARGET:
    {
        SVGA3dCmdBindGBScreenTarget *pCmd = (SVGA3dCmdBindGBScreenTarget *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdBindGBScreenTarget(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_UPDATE_GB_SCREENTARGET:
    {
        SVGA3dCmdUpdateGBScreenTarget *pCmd = (SVGA3dCmdUpdateGBScreenTarget *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdUpdateGBScreenTarget(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_READBACK_GB_IMAGE_PARTIAL:
    {
        SVGA3dCmdReadbackGBImagePartial *pCmd = (SVGA3dCmdReadbackGBImagePartial *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_INVALIDATE_GB_IMAGE_PARTIAL:
    {
        SVGA3dCmdInvalidateGBImagePartial *pCmd = (SVGA3dCmdInvalidateGBImagePartial *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_SET_GB_SHADERCONSTS_INLINE:
    {
        SVGA3dCmdSetGBShaderConstInline *pCmd = (SVGA3dCmdSetGBShaderConstInline *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_GB_SCREEN_DMA:
    {
        SVGA3dCmdGBScreenDMA *pCmd = (SVGA3dCmdGBScreenDMA *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_BIND_GB_SURFACE_WITH_PITCH:
    {
        SVGA3dCmdBindGBSurfaceWithPitch *pCmd = (SVGA3dCmdBindGBSurfaceWithPitch *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_GB_MOB_FENCE:
    {
        SVGA3dCmdGBMobFence *pCmd = (SVGA3dCmdGBMobFence *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_DEFINE_GB_SURFACE_V2:
    {
        SVGA3dCmdDefineGBSurface_v2 *pCmd = (SVGA3dCmdDefineGBSurface_v2 *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdDefineGBSurface_v2(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_DEFINE_GB_MOB64:
    {
        SVGA3dCmdDefineGBMob64 *pCmd = (SVGA3dCmdDefineGBMob64 *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        vmsvga3dCmdDefineGBMob64(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_REDEFINE_GB_MOB64:
    {
        SVGA3dCmdRedefineGBMob64 *pCmd = (SVGA3dCmdRedefineGBMob64 *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_NOP_ERROR:
    {
        /* Apparently there is nothing to do. */
        break;
    }

    case SVGA_3D_CMD_SET_VERTEX_STREAMS:
    {
        SVGA3dCmdSetVertexStreams *pCmd = (SVGA3dCmdSetVertexStreams *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_SET_VERTEX_DECLS:
    {
        SVGA3dCmdSetVertexDecls *pCmd = (SVGA3dCmdSetVertexDecls *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_SET_VERTEX_DIVISORS:
    {
        SVGA3dCmdSetVertexDivisors *pCmd = (SVGA3dCmdSetVertexDivisors *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        VMSVGA_3D_CMD_NOTIMPL(); RT_NOREF(pCmd);
        break;
    }

    case SVGA_3D_CMD_DRAW:
    {
        /* No corresponding SVGA3dCmd structure. */
        VMSVGA_3D_CMD_NOTIMPL();
        break;
    }

    case SVGA_3D_CMD_DRAW_INDEXED:
    {
        /* No corresponding SVGA3dCmd structure. */
        VMSVGA_3D_CMD_NOTIMPL();
        break;
    }

    case SVGA_3D_CMD_DX_DEFINE_CONTEXT:
    {
        SVGA3dCmdDXDefineContext *pCmd = (SVGA3dCmdDXDefineContext *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDefineContext(pThisCC, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DESTROY_CONTEXT:
    {
        SVGA3dCmdDXDestroyContext *pCmd = (SVGA3dCmdDXDestroyContext *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDestroyContext(pThisCC, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_BIND_CONTEXT:
    {
        SVGA3dCmdDXBindContext *pCmd = (SVGA3dCmdDXBindContext *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXBindContext(pThisCC, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_READBACK_CONTEXT:
    {
        SVGA3dCmdDXReadbackContext *pCmd = (SVGA3dCmdDXReadbackContext *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXReadbackContext(pThisCC, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_INVALIDATE_CONTEXT:
    {
        SVGA3dCmdDXInvalidateContext *pCmd = (SVGA3dCmdDXInvalidateContext *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXInvalidateContext(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_SINGLE_CONSTANT_BUFFER:
    {
        SVGA3dCmdDXSetSingleConstantBuffer *pCmd = (SVGA3dCmdDXSetSingleConstantBuffer *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetSingleConstantBuffer(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_SHADER_RESOURCES:
    {
        SVGA3dCmdDXSetShaderResources *pCmd = (SVGA3dCmdDXSetShaderResources *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetShaderResources(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_SHADER:
    {
        SVGA3dCmdDXSetShader *pCmd = (SVGA3dCmdDXSetShader *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetShader(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_SAMPLERS:
    {
        SVGA3dCmdDXSetSamplers *pCmd = (SVGA3dCmdDXSetSamplers *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetSamplers(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DRAW:
    {
        SVGA3dCmdDXDraw *pCmd = (SVGA3dCmdDXDraw *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDraw(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DRAW_INDEXED:
    {
        SVGA3dCmdDXDrawIndexed *pCmd = (SVGA3dCmdDXDrawIndexed *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDrawIndexed(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DRAW_INSTANCED:
    {
        SVGA3dCmdDXDrawInstanced *pCmd = (SVGA3dCmdDXDrawInstanced *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDrawInstanced(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DRAW_INDEXED_INSTANCED:
    {
        SVGA3dCmdDXDrawIndexedInstanced *pCmd = (SVGA3dCmdDXDrawIndexedInstanced *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDrawIndexedInstanced(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DRAW_AUTO:
    {
        SVGA3dCmdDXDrawAuto *pCmd = (SVGA3dCmdDXDrawAuto *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDrawAuto(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_INPUT_LAYOUT:
    {
        SVGA3dCmdDXSetInputLayout *pCmd = (SVGA3dCmdDXSetInputLayout *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetInputLayout(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_VERTEX_BUFFERS:
    {
        SVGA3dCmdDXSetVertexBuffers *pCmd = (SVGA3dCmdDXSetVertexBuffers *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetVertexBuffers(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_INDEX_BUFFER:
    {
        SVGA3dCmdDXSetIndexBuffer *pCmd = (SVGA3dCmdDXSetIndexBuffer *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetIndexBuffer(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_TOPOLOGY:
    {
        SVGA3dCmdDXSetTopology *pCmd = (SVGA3dCmdDXSetTopology *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetTopology(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_RENDERTARGETS:
    {
        SVGA3dCmdDXSetRenderTargets *pCmd = (SVGA3dCmdDXSetRenderTargets *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetRenderTargets(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_BLEND_STATE:
    {
        SVGA3dCmdDXSetBlendState *pCmd = (SVGA3dCmdDXSetBlendState *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetBlendState(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_DEPTHSTENCIL_STATE:
    {
        SVGA3dCmdDXSetDepthStencilState *pCmd = (SVGA3dCmdDXSetDepthStencilState *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetDepthStencilState(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_RASTERIZER_STATE:
    {
        SVGA3dCmdDXSetRasterizerState *pCmd = (SVGA3dCmdDXSetRasterizerState *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetRasterizerState(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DEFINE_QUERY:
    {
        SVGA3dCmdDXDefineQuery *pCmd = (SVGA3dCmdDXDefineQuery *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDefineQuery(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DESTROY_QUERY:
    {
        SVGA3dCmdDXDestroyQuery *pCmd = (SVGA3dCmdDXDestroyQuery *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDestroyQuery(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_BIND_QUERY:
    {
        SVGA3dCmdDXBindQuery *pCmd = (SVGA3dCmdDXBindQuery *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXBindQuery(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_QUERY_OFFSET:
    {
        SVGA3dCmdDXSetQueryOffset *pCmd = (SVGA3dCmdDXSetQueryOffset *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetQueryOffset(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_BEGIN_QUERY:
    {
        SVGA3dCmdDXBeginQuery *pCmd = (SVGA3dCmdDXBeginQuery *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXBeginQuery(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_END_QUERY:
    {
        SVGA3dCmdDXEndQuery *pCmd = (SVGA3dCmdDXEndQuery *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXEndQuery(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_READBACK_QUERY:
    {
        SVGA3dCmdDXReadbackQuery *pCmd = (SVGA3dCmdDXReadbackQuery *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXReadbackQuery(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_PREDICATION:
    {
        SVGA3dCmdDXSetPredication *pCmd = (SVGA3dCmdDXSetPredication *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetPredication(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_SOTARGETS:
    {
        SVGA3dCmdDXSetSOTargets *pCmd = (SVGA3dCmdDXSetSOTargets *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetSOTargets(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_VIEWPORTS:
    {
        SVGA3dCmdDXSetViewports *pCmd = (SVGA3dCmdDXSetViewports *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetViewports(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_SCISSORRECTS:
    {
        SVGA3dCmdDXSetScissorRects *pCmd = (SVGA3dCmdDXSetScissorRects *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetScissorRects(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_CLEAR_RENDERTARGET_VIEW:
    {
        SVGA3dCmdDXClearRenderTargetView *pCmd = (SVGA3dCmdDXClearRenderTargetView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXClearRenderTargetView(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_CLEAR_DEPTHSTENCIL_VIEW:
    {
        SVGA3dCmdDXClearDepthStencilView *pCmd = (SVGA3dCmdDXClearDepthStencilView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXClearDepthStencilView(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_PRED_COPY_REGION:
    {
        SVGA3dCmdDXPredCopyRegion *pCmd = (SVGA3dCmdDXPredCopyRegion *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXPredCopyRegion(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_PRED_COPY:
    {
        SVGA3dCmdDXPredCopy *pCmd = (SVGA3dCmdDXPredCopy *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXPredCopy(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_PRESENTBLT:
    {
        SVGA3dCmdDXPresentBlt *pCmd = (SVGA3dCmdDXPresentBlt *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXPresentBlt(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_GENMIPS:
    {
        SVGA3dCmdDXGenMips *pCmd = (SVGA3dCmdDXGenMips *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXGenMips(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_UPDATE_SUBRESOURCE:
    {
        SVGA3dCmdDXUpdateSubResource *pCmd = (SVGA3dCmdDXUpdateSubResource *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXUpdateSubResource(pThisCC, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_READBACK_SUBRESOURCE:
    {
        SVGA3dCmdDXReadbackSubResource *pCmd = (SVGA3dCmdDXReadbackSubResource *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXReadbackSubResource(pThisCC, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_INVALIDATE_SUBRESOURCE:
    {
        SVGA3dCmdDXInvalidateSubResource *pCmd = (SVGA3dCmdDXInvalidateSubResource *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXInvalidateSubResource(pThisCC, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DEFINE_SHADERRESOURCE_VIEW:
    {
        SVGA3dCmdDXDefineShaderResourceView *pCmd = (SVGA3dCmdDXDefineShaderResourceView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDefineShaderResourceView(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DESTROY_SHADERRESOURCE_VIEW:
    {
        SVGA3dCmdDXDestroyShaderResourceView *pCmd = (SVGA3dCmdDXDestroyShaderResourceView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDestroyShaderResourceView(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DEFINE_RENDERTARGET_VIEW:
    {
        SVGA3dCmdDXDefineRenderTargetView *pCmd = (SVGA3dCmdDXDefineRenderTargetView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDefineRenderTargetView(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DESTROY_RENDERTARGET_VIEW:
    {
        SVGA3dCmdDXDestroyRenderTargetView *pCmd = (SVGA3dCmdDXDestroyRenderTargetView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDestroyRenderTargetView(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_VIEW:
    {
        SVGA3dCmdDXDefineDepthStencilView *pCmd = (SVGA3dCmdDXDefineDepthStencilView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDefineDepthStencilView(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_VIEW:
    {
        SVGA3dCmdDXDestroyDepthStencilView *pCmd = (SVGA3dCmdDXDestroyDepthStencilView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDestroyDepthStencilView(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DEFINE_ELEMENTLAYOUT:
    {
        SVGA3dCmdDXDefineElementLayout *pCmd = (SVGA3dCmdDXDefineElementLayout *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDefineElementLayout(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DESTROY_ELEMENTLAYOUT:
    {
        SVGA3dCmdDXDestroyElementLayout *pCmd = (SVGA3dCmdDXDestroyElementLayout *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDestroyElementLayout(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DEFINE_BLEND_STATE:
    {
        SVGA3dCmdDXDefineBlendState *pCmd = (SVGA3dCmdDXDefineBlendState *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDefineBlendState(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DESTROY_BLEND_STATE:
    {
        SVGA3dCmdDXDestroyBlendState *pCmd = (SVGA3dCmdDXDestroyBlendState *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDestroyBlendState(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_STATE:
    {
        SVGA3dCmdDXDefineDepthStencilState *pCmd = (SVGA3dCmdDXDefineDepthStencilState *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDefineDepthStencilState(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_STATE:
    {
        SVGA3dCmdDXDestroyDepthStencilState *pCmd = (SVGA3dCmdDXDestroyDepthStencilState *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDestroyDepthStencilState(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DEFINE_RASTERIZER_STATE:
    {
        SVGA3dCmdDXDefineRasterizerState *pCmd = (SVGA3dCmdDXDefineRasterizerState *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDefineRasterizerState(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DESTROY_RASTERIZER_STATE:
    {
        SVGA3dCmdDXDestroyRasterizerState *pCmd = (SVGA3dCmdDXDestroyRasterizerState *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDestroyRasterizerState(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DEFINE_SAMPLER_STATE:
    {
        SVGA3dCmdDXDefineSamplerState *pCmd = (SVGA3dCmdDXDefineSamplerState *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDefineSamplerState(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DESTROY_SAMPLER_STATE:
    {
        SVGA3dCmdDXDestroySamplerState *pCmd = (SVGA3dCmdDXDestroySamplerState *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDestroySamplerState(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DEFINE_SHADER:
    {
        SVGA3dCmdDXDefineShader *pCmd = (SVGA3dCmdDXDefineShader *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDefineShader(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DESTROY_SHADER:
    {
        SVGA3dCmdDXDestroyShader *pCmd = (SVGA3dCmdDXDestroyShader *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDestroyShader(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_BIND_SHADER:
    {
        SVGA3dCmdDXBindShader *pCmd = (SVGA3dCmdDXBindShader *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXBindShader(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DEFINE_STREAMOUTPUT:
    {
        SVGA3dCmdDXDefineStreamOutput *pCmd = (SVGA3dCmdDXDefineStreamOutput *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDefineStreamOutput(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DESTROY_STREAMOUTPUT:
    {
        SVGA3dCmdDXDestroyStreamOutput *pCmd = (SVGA3dCmdDXDestroyStreamOutput *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDestroyStreamOutput(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_STREAMOUTPUT:
    {
        SVGA3dCmdDXSetStreamOutput *pCmd = (SVGA3dCmdDXSetStreamOutput *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetStreamOutput(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_COTABLE:
    {
        SVGA3dCmdDXSetCOTable *pCmd = (SVGA3dCmdDXSetCOTable *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetCOTable(pThisCC, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_READBACK_COTABLE:
    {
        SVGA3dCmdDXReadbackCOTable *pCmd = (SVGA3dCmdDXReadbackCOTable *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXReadbackCOTable(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_BUFFER_COPY:
    {
        SVGA3dCmdDXBufferCopy *pCmd = (SVGA3dCmdDXBufferCopy *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXBufferCopy(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_TRANSFER_FROM_BUFFER:
    {
        SVGA3dCmdDXTransferFromBuffer *pCmd = (SVGA3dCmdDXTransferFromBuffer *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXTransferFromBuffer(pThisCC, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SURFACE_COPY_AND_READBACK:
    {
        SVGA3dCmdDXSurfaceCopyAndReadback *pCmd = (SVGA3dCmdDXSurfaceCopyAndReadback *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSurfaceCopyAndReadback(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_MOVE_QUERY:
    {
        SVGA3dCmdDXMoveQuery *pCmd = (SVGA3dCmdDXMoveQuery *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXMoveQuery(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_BIND_ALL_QUERY:
    {
        SVGA3dCmdDXBindAllQuery *pCmd = (SVGA3dCmdDXBindAllQuery *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXBindAllQuery(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_READBACK_ALL_QUERY:
    {
        SVGA3dCmdDXReadbackAllQuery *pCmd = (SVGA3dCmdDXReadbackAllQuery *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXReadbackAllQuery(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_PRED_TRANSFER_FROM_BUFFER:
    {
        SVGA3dCmdDXPredTransferFromBuffer *pCmd = (SVGA3dCmdDXPredTransferFromBuffer *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXPredTransferFromBuffer(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_MOB_FENCE_64:
    {
        SVGA3dCmdDXMobFence64 *pCmd = (SVGA3dCmdDXMobFence64 *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXMobFence64(pThisCC, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_BIND_ALL_SHADER:
    {
        SVGA3dCmdDXBindAllShader *pCmd = (SVGA3dCmdDXBindAllShader *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXBindAllShader(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_HINT:
    {
        SVGA3dCmdDXHint *pCmd = (SVGA3dCmdDXHint *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXHint(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_BUFFER_UPDATE:
    {
        SVGA3dCmdDXBufferUpdate *pCmd = (SVGA3dCmdDXBufferUpdate *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXBufferUpdate(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_VS_CONSTANT_BUFFER_OFFSET:
    {
        SVGA3dCmdDXSetVSConstantBufferOffset *pCmd = (SVGA3dCmdDXSetVSConstantBufferOffset *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetVSConstantBufferOffset(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_PS_CONSTANT_BUFFER_OFFSET:
    {
        SVGA3dCmdDXSetPSConstantBufferOffset *pCmd = (SVGA3dCmdDXSetPSConstantBufferOffset *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetPSConstantBufferOffset(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_GS_CONSTANT_BUFFER_OFFSET:
    {
        SVGA3dCmdDXSetGSConstantBufferOffset *pCmd = (SVGA3dCmdDXSetGSConstantBufferOffset *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetGSConstantBufferOffset(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_HS_CONSTANT_BUFFER_OFFSET:
    {
        SVGA3dCmdDXSetHSConstantBufferOffset *pCmd = (SVGA3dCmdDXSetHSConstantBufferOffset *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetHSConstantBufferOffset(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_DS_CONSTANT_BUFFER_OFFSET:
    {
        SVGA3dCmdDXSetDSConstantBufferOffset *pCmd = (SVGA3dCmdDXSetDSConstantBufferOffset *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetDSConstantBufferOffset(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_CS_CONSTANT_BUFFER_OFFSET:
    {
        SVGA3dCmdDXSetCSConstantBufferOffset *pCmd = (SVGA3dCmdDXSetCSConstantBufferOffset *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetCSConstantBufferOffset(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_COND_BIND_ALL_SHADER:
    {
        SVGA3dCmdDXCondBindAllShader *pCmd = (SVGA3dCmdDXCondBindAllShader *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXCondBindAllShader(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_SCREEN_COPY:
    {
        SVGA3dCmdScreenCopy *pCmd = (SVGA3dCmdScreenCopy *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdScreenCopy(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_RESERVED1:
    {
        VMSVGA_3D_CMD_NOTIMPL();
        break;
    }

    case SVGA_3D_CMD_RESERVED2:
    {
        VMSVGA_3D_CMD_NOTIMPL();
        break;
    }

    case SVGA_3D_CMD_RESERVED3:
    {
        VMSVGA_3D_CMD_NOTIMPL();
        break;
    }

    case SVGA_3D_CMD_RESERVED4:
    {
        VMSVGA_3D_CMD_NOTIMPL();
        break;
    }

    case SVGA_3D_CMD_RESERVED5:
    {
        VMSVGA_3D_CMD_NOTIMPL();
        break;
    }

    case SVGA_3D_CMD_RESERVED6:
    {
        VMSVGA_3D_CMD_NOTIMPL();
        break;
    }

    case SVGA_3D_CMD_RESERVED7:
    {
        VMSVGA_3D_CMD_NOTIMPL();
        break;
    }

    case SVGA_3D_CMD_RESERVED8:
    {
        VMSVGA_3D_CMD_NOTIMPL();
        break;
    }

    case SVGA_3D_CMD_GROW_OTABLE:
    {
        SVGA3dCmdGrowOTable *pCmd = (SVGA3dCmdGrowOTable *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdGrowOTable(pThisCC, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_GROW_COTABLE:
    {
        SVGA3dCmdDXGrowCOTable *pCmd = (SVGA3dCmdDXGrowCOTable *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXGrowCOTable(pThisCC, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_INTRA_SURFACE_COPY:
    {
        SVGA3dCmdIntraSurfaceCopy *pCmd = (SVGA3dCmdIntraSurfaceCopy *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdIntraSurfaceCopy(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DEFINE_GB_SURFACE_V3:
    {
        SVGA3dCmdDefineGBSurface_v3 *pCmd = (SVGA3dCmdDefineGBSurface_v3 *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDefineGBSurface_v3(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_DX_RESOLVE_COPY:
    {
        SVGA3dCmdDXResolveCopy *pCmd = (SVGA3dCmdDXResolveCopy *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXResolveCopy(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_PRED_RESOLVE_COPY:
    {
        SVGA3dCmdDXPredResolveCopy *pCmd = (SVGA3dCmdDXPredResolveCopy *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXPredResolveCopy(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_PRED_CONVERT_REGION:
    {
        SVGA3dCmdDXPredConvertRegion *pCmd = (SVGA3dCmdDXPredConvertRegion *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXPredConvertRegion(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_PRED_CONVERT:
    {
        SVGA3dCmdDXPredConvert *pCmd = (SVGA3dCmdDXPredConvert *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXPredConvert(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_WHOLE_SURFACE_COPY:
    {
        SVGA3dCmdWholeSurfaceCopy *pCmd = (SVGA3dCmdWholeSurfaceCopy *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdWholeSurfaceCopy(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DEFINE_UA_VIEW:
    {
        SVGA3dCmdDXDefineUAView *pCmd = (SVGA3dCmdDXDefineUAView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDefineUAView(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DESTROY_UA_VIEW:
    {
        SVGA3dCmdDXDestroyUAView *pCmd = (SVGA3dCmdDXDestroyUAView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDestroyUAView(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_CLEAR_UA_VIEW_UINT:
    {
        SVGA3dCmdDXClearUAViewUint *pCmd = (SVGA3dCmdDXClearUAViewUint *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXClearUAViewUint(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_CLEAR_UA_VIEW_FLOAT:
    {
        SVGA3dCmdDXClearUAViewFloat *pCmd = (SVGA3dCmdDXClearUAViewFloat *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXClearUAViewFloat(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_COPY_STRUCTURE_COUNT:
    {
        SVGA3dCmdDXCopyStructureCount *pCmd = (SVGA3dCmdDXCopyStructureCount *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXCopyStructureCount(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_UA_VIEWS:
    {
        SVGA3dCmdDXSetUAViews *pCmd = (SVGA3dCmdDXSetUAViews *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetUAViews(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DRAW_INDEXED_INSTANCED_INDIRECT:
    {
        SVGA3dCmdDXDrawIndexedInstancedIndirect *pCmd = (SVGA3dCmdDXDrawIndexedInstancedIndirect *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDrawIndexedInstancedIndirect(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DRAW_INSTANCED_INDIRECT:
    {
        SVGA3dCmdDXDrawInstancedIndirect *pCmd = (SVGA3dCmdDXDrawInstancedIndirect *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDrawInstancedIndirect(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DISPATCH:
    {
        SVGA3dCmdDXDispatch *pCmd = (SVGA3dCmdDXDispatch *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDispatch(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DISPATCH_INDIRECT:
    {
        SVGA3dCmdDXDispatchIndirect *pCmd = (SVGA3dCmdDXDispatchIndirect *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDispatchIndirect(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_WRITE_ZERO_SURFACE:
    {
        SVGA3dCmdWriteZeroSurface *pCmd = (SVGA3dCmdWriteZeroSurface *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdWriteZeroSurface(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_HINT_ZERO_SURFACE:
    {
        SVGA3dCmdHintZeroSurface *pCmd = (SVGA3dCmdHintZeroSurface *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdHintZeroSurface(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_TRANSFER_TO_BUFFER:
    {
        SVGA3dCmdDXTransferToBuffer *pCmd = (SVGA3dCmdDXTransferToBuffer *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXTransferToBuffer(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_STRUCTURE_COUNT:
    {
        SVGA3dCmdDXSetStructureCount *pCmd = (SVGA3dCmdDXSetStructureCount *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetStructureCount(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_LOGICOPS_BITBLT:
    {
        SVGA3dCmdLogicOpsBitBlt *pCmd = (SVGA3dCmdLogicOpsBitBlt *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdLogicOpsBitBlt(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_LOGICOPS_TRANSBLT:
    {
        SVGA3dCmdLogicOpsTransBlt *pCmd = (SVGA3dCmdLogicOpsTransBlt *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdLogicOpsTransBlt(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_LOGICOPS_STRETCHBLT:
    {
        SVGA3dCmdLogicOpsStretchBlt *pCmd = (SVGA3dCmdLogicOpsStretchBlt *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdLogicOpsStretchBlt(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_LOGICOPS_COLORFILL:
    {
        SVGA3dCmdLogicOpsColorFill *pCmd = (SVGA3dCmdLogicOpsColorFill *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdLogicOpsColorFill(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_LOGICOPS_ALPHABLEND:
    {
        SVGA3dCmdLogicOpsAlphaBlend *pCmd = (SVGA3dCmdLogicOpsAlphaBlend *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdLogicOpsAlphaBlend(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_LOGICOPS_CLEARTYPEBLEND:
    {
        SVGA3dCmdLogicOpsClearTypeBlend *pCmd = (SVGA3dCmdLogicOpsClearTypeBlend *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdLogicOpsClearTypeBlend(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_RESERVED2_1:
    {
        VMSVGA_3D_CMD_NOTIMPL();
        break;
    }

    case SVGA_3D_CMD_RESERVED2_2:
    {
        VMSVGA_3D_CMD_NOTIMPL();
        break;
    }

    case SVGA_3D_CMD_DEFINE_GB_SURFACE_V4:
    {
        SVGA3dCmdDefineGBSurface_v4 *pCmd = (SVGA3dCmdDefineGBSurface_v4 *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDefineGBSurface_v4(pThisCC, pCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_CS_UA_VIEWS:
    {
        SVGA3dCmdDXSetCSUAViews *pCmd = (SVGA3dCmdDXSetCSUAViews *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetCSUAViews(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_MIN_LOD:
    {
        SVGA3dCmdDXSetMinLOD *pCmd = (SVGA3dCmdDXSetMinLOD *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetMinLOD(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_RESERVED2_3:
    {
        VMSVGA_3D_CMD_NOTIMPL();
        break;
    }

    case SVGA_3D_CMD_RESERVED2_4:
    {
        VMSVGA_3D_CMD_NOTIMPL();
        break;
    }

    case SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_VIEW_V2:
    {
        SVGA3dCmdDXDefineDepthStencilView_v2 *pCmd = (SVGA3dCmdDXDefineDepthStencilView_v2 *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDefineDepthStencilView_v2(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DEFINE_STREAMOUTPUT_WITH_MOB:
    {
        SVGA3dCmdDXDefineStreamOutputWithMob *pCmd = (SVGA3dCmdDXDefineStreamOutputWithMob *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDefineStreamOutputWithMob(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_SET_SHADER_IFACE:
    {
        SVGA3dCmdDXSetShaderIface *pCmd = (SVGA3dCmdDXSetShaderIface *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXSetShaderIface(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_BIND_STREAMOUTPUT:
    {
        SVGA3dCmdDXBindStreamOutput *pCmd = (SVGA3dCmdDXBindStreamOutput *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXBindStreamOutput(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_SURFACE_STRETCHBLT_NON_MS_TO_MS:
    {
        SVGA3dCmdSurfaceStretchBltNonMSToMS *pCmd = (SVGA3dCmdSurfaceStretchBltNonMSToMS *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdSurfaceStretchBltNonMSToMS(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_BIND_SHADER_IFACE:
    {
        SVGA3dCmdDXBindShaderIface *pCmd = (SVGA3dCmdDXBindShaderIface *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXBindShaderIface(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_VB_DX_CLEAR_RENDERTARGET_VIEW_REGION:
    {
        SVGA3dCmdVBDXClearRenderTargetViewRegion *pCmd = (SVGA3dCmdVBDXClearRenderTargetViewRegion *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdVBDXClearRenderTargetViewRegion(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case SVGA_3D_CMD_DX_DEFINE_RASTERIZER_STATE_V2:
    {
        SVGA3dCmdDXDefineRasterizerState_v2 *pCmd = (SVGA3dCmdDXDefineRasterizerState_v2 *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dCmdDXDefineRasterizerState_v2(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_DEFINE_VIDEO_PROCESSOR:
    {
        VBSVGA3dCmdDXDefineVideoProcessor *pCmd = (VBSVGA3dCmdDXDefineVideoProcessor *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXDefineVideoProcessor(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_DEFINE_VIDEO_DECODER_OUTPUT_VIEW:
    {
        VBSVGA3dCmdDXDefineVideoDecoderOutputView *pCmd = (VBSVGA3dCmdDXDefineVideoDecoderOutputView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXDefineVideoDecoderOutputView(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_DEFINE_VIDEO_DECODER:
    {
        VBSVGA3dCmdDXDefineVideoDecoder *pCmd = (VBSVGA3dCmdDXDefineVideoDecoder *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXDefineVideoDecoder(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_DECODER_BEGIN_FRAME:
    {
        VBSVGA3dCmdDXVideoDecoderBeginFrame *pCmd = (VBSVGA3dCmdDXVideoDecoderBeginFrame *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoDecoderBeginFrame(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_DECODER_SUBMIT_BUFFERS:
    {
        VBSVGA3dCmdDXVideoDecoderSubmitBuffers *pCmd = (VBSVGA3dCmdDXVideoDecoderSubmitBuffers *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoDecoderSubmitBuffers(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_DECODER_END_FRAME:
    {
        VBSVGA3dCmdDXVideoDecoderEndFrame *pCmd = (VBSVGA3dCmdDXVideoDecoderEndFrame *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoDecoderEndFrame(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_DEFINE_VIDEO_PROCESSOR_INPUT_VIEW:
    {
        VBSVGA3dCmdDXDefineVideoProcessorInputView *pCmd = (VBSVGA3dCmdDXDefineVideoProcessorInputView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXDefineVideoProcessorInputView(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_DEFINE_VIDEO_PROCESSOR_OUTPUT_VIEW:
    {
        VBSVGA3dCmdDXDefineVideoProcessorOutputView *pCmd = (VBSVGA3dCmdDXDefineVideoProcessorOutputView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXDefineVideoProcessorOutputView(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_BLT:
    {
        VBSVGA3dCmdDXVideoProcessorBlt *pCmd = (VBSVGA3dCmdDXVideoProcessorBlt *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorBlt(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_DESTROY_VIDEO_DECODER:
    {
        VBSVGA3dCmdDXDestroyVideoDecoder *pCmd = (VBSVGA3dCmdDXDestroyVideoDecoder *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXDestroyVideoDecoder(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_DESTROY_VIDEO_DECODER_OUTPUT_VIEW:
    {
        VBSVGA3dCmdDXDestroyVideoDecoderOutputView *pCmd = (VBSVGA3dCmdDXDestroyVideoDecoderOutputView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXDestroyVideoDecoderOutputView(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_DESTROY_VIDEO_PROCESSOR:
    {
        VBSVGA3dCmdDXDestroyVideoProcessor *pCmd = (VBSVGA3dCmdDXDestroyVideoProcessor *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXDestroyVideoProcessor(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_DESTROY_VIDEO_PROCESSOR_INPUT_VIEW:
    {
        VBSVGA3dCmdDXDestroyVideoProcessorInputView *pCmd = (VBSVGA3dCmdDXDestroyVideoProcessorInputView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXDestroyVideoProcessorInputView(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_DESTROY_VIDEO_PROCESSOR_OUTPUT_VIEW:
    {
        VBSVGA3dCmdDXDestroyVideoProcessorOutputView *pCmd = (VBSVGA3dCmdDXDestroyVideoProcessorOutputView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXDestroyVideoProcessorOutputView(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_TARGET_RECT:
    {
        VBSVGA3dCmdDXVideoProcessorSetOutputTargetRect *pCmd = (VBSVGA3dCmdDXVideoProcessorSetOutputTargetRect *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetOutputTargetRect(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_BACKGROUND_COLOR:
    {
        VBSVGA3dCmdDXVideoProcessorSetOutputBackgroundColor *pCmd = (VBSVGA3dCmdDXVideoProcessorSetOutputBackgroundColor *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetOutputBackgroundColor(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_COLOR_SPACE:
    {
        VBSVGA3dCmdDXVideoProcessorSetOutputColorSpace *pCmd = (VBSVGA3dCmdDXVideoProcessorSetOutputColorSpace *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetOutputColorSpace(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_ALPHA_FILL_MODE:
    {
        VBSVGA3dCmdDXVideoProcessorSetOutputAlphaFillMode *pCmd = (VBSVGA3dCmdDXVideoProcessorSetOutputAlphaFillMode *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetOutputAlphaFillMode(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_CONSTRICTION:
    {
        VBSVGA3dCmdDXVideoProcessorSetOutputConstriction *pCmd = (VBSVGA3dCmdDXVideoProcessorSetOutputConstriction *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetOutputConstriction(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_STEREO_MODE:
    {
        VBSVGA3dCmdDXVideoProcessorSetOutputStereoMode *pCmd = (VBSVGA3dCmdDXVideoProcessorSetOutputStereoMode *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetOutputStereoMode(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_FRAME_FORMAT:
    {
        VBSVGA3dCmdDXVideoProcessorSetStreamFrameFormat *pCmd = (VBSVGA3dCmdDXVideoProcessorSetStreamFrameFormat *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetStreamFrameFormat(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_COLOR_SPACE:
    {
        VBSVGA3dCmdDXVideoProcessorSetStreamColorSpace *pCmd = (VBSVGA3dCmdDXVideoProcessorSetStreamColorSpace *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetStreamColorSpace(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_OUTPUT_RATE:
    {
        VBSVGA3dCmdDXVideoProcessorSetStreamOutputRate *pCmd = (VBSVGA3dCmdDXVideoProcessorSetStreamOutputRate *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetStreamOutputRate(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_SOURCE_RECT:
    {
        VBSVGA3dCmdDXVideoProcessorSetStreamSourceRect *pCmd = (VBSVGA3dCmdDXVideoProcessorSetStreamSourceRect *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetStreamSourceRect(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_DEST_RECT:
    {
        VBSVGA3dCmdDXVideoProcessorSetStreamDestRect *pCmd = (VBSVGA3dCmdDXVideoProcessorSetStreamDestRect *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetStreamDestRect(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_ALPHA:
    {
        VBSVGA3dCmdDXVideoProcessorSetStreamAlpha *pCmd = (VBSVGA3dCmdDXVideoProcessorSetStreamAlpha *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetStreamAlpha(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_PALETTE:
    {
        VBSVGA3dCmdDXVideoProcessorSetStreamPalette *pCmd = (VBSVGA3dCmdDXVideoProcessorSetStreamPalette *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetStreamPalette(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_PIXEL_ASPECT_RATIO:
    {
        VBSVGA3dCmdDXVideoProcessorSetStreamPixelAspectRatio *pCmd = (VBSVGA3dCmdDXVideoProcessorSetStreamPixelAspectRatio *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetStreamPixelAspectRatio(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_LUMA_KEY:
    {
        VBSVGA3dCmdDXVideoProcessorSetStreamLumaKey *pCmd = (VBSVGA3dCmdDXVideoProcessorSetStreamLumaKey *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetStreamLumaKey(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_STEREO_FORMAT:
    {
        VBSVGA3dCmdDXVideoProcessorSetStreamStereoFormat *pCmd = (VBSVGA3dCmdDXVideoProcessorSetStreamStereoFormat *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetStreamStereoFormat(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_AUTO_PROCESSING_MODE:
    {
        VBSVGA3dCmdDXVideoProcessorSetStreamAutoProcessingMode *pCmd = (VBSVGA3dCmdDXVideoProcessorSetStreamAutoProcessingMode *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetStreamAutoProcessingMode(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_FILTER:
    {
        VBSVGA3dCmdDXVideoProcessorSetStreamFilter *pCmd = (VBSVGA3dCmdDXVideoProcessorSetStreamFilter *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetStreamFilter(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_ROTATION:
    {
        VBSVGA3dCmdDXVideoProcessorSetStreamRotation *pCmd = (VBSVGA3dCmdDXVideoProcessorSetStreamRotation *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXVideoProcessorSetStreamRotation(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_GET_VIDEO_CAPABILITY:
    {
        VBSVGA3dCmdDXGetVideoCapability *pCmd = (VBSVGA3dCmdDXGetVideoCapability *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXGetVideoCapability(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_CLEAR_RTV:
    {
        VBSVGA3dCmdDXClearView *pCmd = (VBSVGA3dCmdDXClearView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXClearRTV(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_CLEAR_UAV:
    {
        VBSVGA3dCmdDXClearView *pCmd = (VBSVGA3dCmdDXClearView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXClearUAV(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_CLEAR_VDOV:
    {
        VBSVGA3dCmdDXClearView *pCmd = (VBSVGA3dCmdDXClearView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXClearVDOV(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_CLEAR_VPIV:
    {
        VBSVGA3dCmdDXClearView *pCmd = (VBSVGA3dCmdDXClearView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXClearVPIV(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    case VBSVGA_3D_CMD_DX_CLEAR_VPOV:
    {
        VBSVGA3dCmdDXClearView *pCmd = (VBSVGA3dCmdDXClearView *)pvCmd;
        VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK(sizeof(*pCmd));
        rcParse = vmsvga3dVBCmdDXClearVPOV(pThisCC, idDXContext, pCmd, cbCmd);
        break;
    }

    /* Unsupported commands. */
    case SVGA_3D_CMD_DEAD4: /* SVGA_3D_CMD_VIDEO_CREATE_DECODER */
    case SVGA_3D_CMD_DEAD5: /* SVGA_3D_CMD_VIDEO_DESTROY_DECODER */
    case SVGA_3D_CMD_DEAD6: /* SVGA_3D_CMD_VIDEO_CREATE_PROCESSOR */
    case SVGA_3D_CMD_DEAD7: /* SVGA_3D_CMD_VIDEO_DESTROY_PROCESSOR */
    case SVGA_3D_CMD_DEAD8: /* SVGA_3D_CMD_VIDEO_DECODE_START_FRAME */
    case SVGA_3D_CMD_DEAD9: /* SVGA_3D_CMD_VIDEO_DECODE_RENDER */
    case SVGA_3D_CMD_DEAD10: /* SVGA_3D_CMD_VIDEO_DECODE_END_FRAME */
    case SVGA_3D_CMD_DEAD11: /* SVGA_3D_CMD_VIDEO_PROCESS_FRAME */
    /* Prevent the compiler warning. */
    case SVGA_3D_CMD_LEGACY_BASE:
    case SVGA_3D_CMD_MAX:
    case SVGA_3D_CMD_FUTURE_MAX:
    case VBSVGA_3D_CMD_MAX:
#ifndef DEBUG_sunlover
    default: /* Compiler warning. */
#else
    /* No 'default' case */
#endif
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatFifoUnkCmds);
        ASSERT_GUEST_MSG_FAILED(("enmCmdId=%d\n", enmCmdId));
        LogRelMax(16, ("VMSVGA: unsupported 3D command %d\n", enmCmdId));
        rcParse = VERR_NOT_IMPLEMENTED;
        break;
    }

    if (RT_FAILURE(rcParse))
        LogRelMax(16, ("VMSVGA: command %d: %Rrc\n", enmCmdId, rcParse));
    return VINF_SUCCESS;
}
# undef VMSVGAFIFO_CHECK_3D_CMD_MIN_SIZE_BREAK
#endif /* VBOX_WITH_VMSVGA3D */


/*
 *
 * Handlers for FIFO commands.
 *
 * Every handler takes the following parameters:
 *
 *    pThis               The shared VGA/VMSVGA state.
 *    pThisCC             The VGA/VMSVGA state for ring-3.
 *    pCmd                The command data.
 */


/* SVGA_CMD_UPDATE */
void vmsvgaR3CmdUpdate(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdUpdate const *pCmd)
{
    RT_NOREF(pThis);
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdUpdate);
    Log(("SVGA_CMD_UPDATE %d,%d %dx%d\n", pCmd->x, pCmd->y, pCmd->width, pCmd->height));

    /* These conditions are too strict but prevent any further integer overflow */
    ASSERT_GUEST_RETURN_VOID(pCmd->x < pThis->svga.u32MaxWidth);
    ASSERT_GUEST_RETURN_VOID(pCmd->y < pThis->svga.u32MaxHeight);
    ASSERT_GUEST_RETURN_VOID(pCmd->width  < pThis->svga.u32MaxWidth);
    ASSERT_GUEST_RETURN_VOID(pCmd->height < pThis->svga.u32MaxHeight);
    RT_UNTRUSTED_VALIDATED_FENCE();

    for (uint32_t idScreen = 0; idScreen < (uint32_t)RT_ELEMENTS(pSvgaR3State->aScreens); idScreen++)
    {
        VMSVGASCREENOBJECT *pScreen = vmsvgaR3GetScreenObject(pThisCC, idScreen);
        if (!pScreen) /* Can happen if screen is not defined (aScreens[idScreen].fDefined == false) yet. */
            continue;

        /* Clip updated Rect to the screen dimensions. */
        SVGASignedRect screenRect;
        screenRect.left   = pScreen->xOrigin;
        screenRect.top    = pScreen->yOrigin;
        screenRect.right  = pScreen->xOrigin + pScreen->cWidth;
        screenRect.bottom = pScreen->yOrigin + pScreen->cHeight;

        SVGASignedRect clipRect;
        clipRect.left = (int32)pCmd->x;
        clipRect.top  = (int32)pCmd->y;
        clipRect.right  = (int32)(pCmd->x + pCmd->width);
        clipRect.bottom = (int32)(pCmd->y + pCmd->height);
        vmsvgaR3ClipRect(&screenRect, &clipRect);

        if (clipRect.left == clipRect.right || clipRect.top == clipRect.bottom)
            continue;

        vmsvgaR3UpdateScreen(pThisCC, pScreen,
            clipRect.left - screenRect.left,
            clipRect.top  - screenRect.top,
            clipRect.right  - clipRect.left,
            clipRect.bottom - clipRect.top);
    }
}


/* SVGA_CMD_UPDATE_VERBOSE */
void vmsvgaR3CmdUpdateVerbose(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdUpdateVerbose const *pCmd)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdUpdateVerbose);
    Log(("SVGA_CMD_UPDATE_VERBOSE %d,%d %dx%d reason %#x\n", pCmd->x, pCmd->y, pCmd->width, pCmd->height, pCmd->reason));

    /* Just like SVGA_CMD_UPDATE, but also provides a per-rectangle 'reason' value */
    vmsvgaR3CmdUpdate(pThis, pThisCC, (SVGAFifoCmdUpdate const *)pCmd);
}


/* SVGA_CMD_RECT_FILL */
void vmsvgaR3CmdRectFill(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdRectFill const *pCmd)
{
    RT_NOREF(pThis, pCmd);
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdRectFill);
    Log(("SVGA_CMD_RECT_FILL %08X @ %d,%d (%dx%d)\n", pCmd->pixel, pCmd->destX, pCmd->destY, pCmd->width, pCmd->height));
    LogRelMax(4, ("VMSVGA: Unsupported SVGA_CMD_RECT_FILL command ignored.\n"));
}


/* SVGA_CMD_RECT_COPY */
void vmsvgaR3CmdRectCopy(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdRectCopy const *pCmd)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdRectCopy);
    Log(("SVGA_CMD_RECT_COPY %d,%d -> %d,%d %dx%d\n", pCmd->srcX, pCmd->srcY, pCmd->destX, pCmd->destY, pCmd->width, pCmd->height));

    VMSVGASCREENOBJECT *pScreen = vmsvgaR3GetScreenObject(pThisCC, 0);
    AssertPtrReturnVoid(pScreen);

    /* Check that arguments aren't complete junk. A precise check is done in vmsvgaR3RectCopy(). */
    ASSERT_GUEST_RETURN_VOID(pCmd->srcX < pThis->svga.u32MaxWidth);
    ASSERT_GUEST_RETURN_VOID(pCmd->destX < pThis->svga.u32MaxWidth);
    ASSERT_GUEST_RETURN_VOID(pCmd->width < pThis->svga.u32MaxWidth);
    ASSERT_GUEST_RETURN_VOID(pCmd->srcY < pThis->svga.u32MaxHeight);
    ASSERT_GUEST_RETURN_VOID(pCmd->destY < pThis->svga.u32MaxHeight);
    ASSERT_GUEST_RETURN_VOID(pCmd->height < pThis->svga.u32MaxHeight);

    vmsvgaR3RectCopy(pThisCC, pScreen, pCmd->srcX, pCmd->srcY, pCmd->destX, pCmd->destY,
                     pCmd->width, pCmd->height, pThis->vram_size);
    vmsvgaR3UpdateScreen(pThisCC, pScreen, pCmd->destX, pCmd->destY, pCmd->width, pCmd->height);
}


/* SVGA_CMD_RECT_ROP_COPY */
void vmsvgaR3CmdRectRopCopy(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdRectRopCopy const *pCmd)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdRectRopCopy);
    Log(("SVGA_CMD_RECT_ROP_COPY %d,%d -> %d,%d %dx%d ROP %#X\n", pCmd->srcX, pCmd->srcY, pCmd->destX, pCmd->destY, pCmd->width, pCmd->height, pCmd->rop));

    if (pCmd->rop != SVGA_ROP_COPY)
    {
        /* We only support the plain copy ROP which makes SVGA_CMD_RECT_ROP_COPY exactly the same
         * as SVGA_CMD_RECT_COPY. XFree86 4.1.0 and 4.2.0 drivers (driver version 10.4.0 and 10.7.0,
         * respectively) issue SVGA_CMD_RECT_ROP_COPY when SVGA_CAP_RECT_COPY is present even when
         * SVGA_CAP_RASTER_OP is not. However, the ROP will always be SVGA_ROP_COPY.
         */
        LogRelMax(4, ("VMSVGA: SVGA_CMD_RECT_ROP_COPY %d,%d -> %d,%d (%dx%d) ROP %X unsupported\n",
                      pCmd->srcX, pCmd->srcY, pCmd->destX, pCmd->destY, pCmd->width, pCmd->height, pCmd->rop));
        return;
    }

    VMSVGASCREENOBJECT *pScreen = vmsvgaR3GetScreenObject(pThisCC, 0);
    AssertPtrReturnVoid(pScreen);

    /* Check that arguments aren't complete junk. A precise check is done in vmsvgaR3RectCopy(). */
    ASSERT_GUEST_RETURN_VOID(pCmd->srcX < pThis->svga.u32MaxWidth);
    ASSERT_GUEST_RETURN_VOID(pCmd->destX < pThis->svga.u32MaxWidth);
    ASSERT_GUEST_RETURN_VOID(pCmd->width < pThis->svga.u32MaxWidth);
    ASSERT_GUEST_RETURN_VOID(pCmd->srcY < pThis->svga.u32MaxHeight);
    ASSERT_GUEST_RETURN_VOID(pCmd->destY < pThis->svga.u32MaxHeight);
    ASSERT_GUEST_RETURN_VOID(pCmd->height < pThis->svga.u32MaxHeight);

    vmsvgaR3RectCopy(pThisCC, pScreen, pCmd->srcX, pCmd->srcY, pCmd->destX, pCmd->destY,
                     pCmd->width, pCmd->height, pThis->vram_size);
    vmsvgaR3UpdateScreen(pThisCC, pScreen, pCmd->destX, pCmd->destY, pCmd->width, pCmd->height);
}


/* SVGA_CMD_DISPLAY_CURSOR */
void vmsvgaR3CmdDisplayCursor(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdDisplayCursor const *pCmd)
{
    RT_NOREF(pThis, pCmd);
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdDisplayCursor);
    Log(("SVGA_CMD_DISPLAY_CURSOR id=%d state=%d\n", pCmd->id, pCmd->state));
    LogRelMax(4, ("VMSVGA: Unsupported SVGA_CMD_DISPLAY_CURSOR command ignored.\n"));
}


/* SVGA_CMD_MOVE_CURSOR */
void vmsvgaR3CmdMoveCursor(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdMoveCursor const *pCmd)
{
    RT_NOREF(pThis, pCmd);
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdMoveCursor);
    Log(("SVGA_CMD_MOVE_CURSOR to %d,%d\n", pCmd->pos.x, pCmd->pos.y));
    LogRelMax(4, ("VMSVGA: Unsupported SVGA_CMD_MOVE_CURSOR command ignored.\n"));
}


/* SVGA_CMD_DEFINE_CURSOR */
void vmsvgaR3CmdDefineCursor(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdDefineCursor const *pCmd)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdDefineCursor);
    Log(("SVGA_CMD_DEFINE_CURSOR id=%d size (%dx%d) hotspot (%d,%d) andMaskDepth=%d xorMaskDepth=%d\n",
             pCmd->id, pCmd->width, pCmd->height, pCmd->hotspotX, pCmd->hotspotY, pCmd->andMaskDepth, pCmd->xorMaskDepth));

    ASSERT_GUEST_RETURN_VOID(pCmd->height < 2048 && pCmd->width < 2048);
    ASSERT_GUEST_RETURN_VOID(pCmd->andMaskDepth <= 32);
    ASSERT_GUEST_RETURN_VOID(pCmd->xorMaskDepth <= 32);
    RT_UNTRUSTED_VALIDATED_FENCE();

    uint32_t const cbSrcAndLine = RT_ALIGN_32(pCmd->width * (pCmd->andMaskDepth + (pCmd->andMaskDepth == 15)), 32) / 8;
    uint32_t const cbSrcAndMask = cbSrcAndLine * pCmd->height;
    uint32_t const cbSrcXorLine = RT_ALIGN_32(pCmd->width * (pCmd->xorMaskDepth + (pCmd->xorMaskDepth == 15)), 32) / 8;

    uint8_t const *pbSrcAndMask = (uint8_t const *)(pCmd + 1);
    uint8_t const *pbSrcXorMask = (uint8_t const *)(pCmd + 1) + cbSrcAndMask;

    uint32_t const cx = pCmd->width;
    uint32_t const cy = pCmd->height;

    /*
     * Convert the input to 1-bit AND mask and a 32-bit BRGA XOR mask.
     * The AND data uses 8-bit aligned scanlines.
     * The XOR data must be starting on a 32-bit boundrary.
     */
    uint32_t cbDstAndLine = RT_ALIGN_32(cx, 8) / 8;
    uint32_t cbDstAndMask = cbDstAndLine          * cy;
    uint32_t cbDstXorMask = cx * sizeof(uint32_t) * cy;
    uint32_t cbCopy = RT_ALIGN_32(cbDstAndMask, 4) + cbDstXorMask;

    uint8_t *pbCopy = (uint8_t *)RTMemAlloc(cbCopy);
    AssertReturnVoid(pbCopy);

    /* Convert the AND mask. */
    uint8_t       *pbDst     = pbCopy;
    uint8_t const *pbSrc     = pbSrcAndMask;
    switch (pCmd->andMaskDepth)
    {
        case 1:
            if (cbSrcAndLine == cbDstAndLine)
                memcpy(pbDst, pbSrc, cbSrcAndLine * cy);
            else
            {
                Assert(cbSrcAndLine > cbDstAndLine); /* lines are dword alined in source, but only byte in destination. */
                for (uint32_t y = 0; y < cy; y++)
                {
                    memcpy(pbDst, pbSrc, cbDstAndLine);
                    pbDst += cbDstAndLine;
                    pbSrc += cbSrcAndLine;
                }
            }
            break;
        /* Should take the XOR mask into account for the multi-bit AND mask. */
        case 8:
            for (uint32_t y = 0; y < cy; y++)
            {
                for (uint32_t x = 0; x < cx; )
                {
                    uint8_t bDst = 0;
                    uint8_t fBit = 0x80;
                    do
                    {
                        uintptr_t const idxPal = pbSrc[x] * 3;
                        if (((   pThis->last_palette[idxPal]
                              | (pThis->last_palette[idxPal] >>  8)
                              | (pThis->last_palette[idxPal] >> 16)) & 0xff) > 0xfc)
                            bDst |= fBit;
                        fBit >>= 1;
                        x++;
                    } while (x < cx && (x & 7));
                    pbDst[(x - 1) / 8] = bDst;
                }
                pbDst += cbDstAndLine;
                pbSrc += cbSrcAndLine;
            }
            break;
        case 15:
            for (uint32_t y = 0; y < cy; y++)
            {
                for (uint32_t x = 0; x < cx; )
                {
                    uint8_t bDst = 0;
                    uint8_t fBit = 0x80;
                    do
                    {
                        if ((pbSrc[x * 2] | (pbSrc[x * 2 + 1] & 0x7f)) >= 0xfc)
                            bDst |= fBit;
                        fBit >>= 1;
                        x++;
                    } while (x < cx && (x & 7));
                    pbDst[(x - 1) / 8] = bDst;
                }
                pbDst += cbDstAndLine;
                pbSrc += cbSrcAndLine;
            }
            break;
        case 16:
            for (uint32_t y = 0; y < cy; y++)
            {
                for (uint32_t x = 0; x < cx; )
                {
                    uint8_t bDst = 0;
                    uint8_t fBit = 0x80;
                    do
                    {
                        if ((pbSrc[x * 2] | pbSrc[x * 2 + 1]) >= 0xfc)
                            bDst |= fBit;
                        fBit >>= 1;
                        x++;
                    } while (x < cx && (x & 7));
                    pbDst[(x - 1) / 8] = bDst;
                }
                pbDst += cbDstAndLine;
                pbSrc += cbSrcAndLine;
            }
            break;
        case 24:
            for (uint32_t y = 0; y < cy; y++)
            {
                for (uint32_t x = 0; x < cx; )
                {
                    uint8_t bDst = 0;
                    uint8_t fBit = 0x80;
                    do
                    {
                        if ((pbSrc[x * 3] | pbSrc[x * 3 + 1] | pbSrc[x * 3 + 2]) >= 0xfc)
                            bDst |= fBit;
                        fBit >>= 1;
                        x++;
                    } while (x < cx && (x & 7));
                    pbDst[(x - 1) / 8] = bDst;
                }
                pbDst += cbDstAndLine;
                pbSrc += cbSrcAndLine;
            }
            break;
        case 32:
            for (uint32_t y = 0; y < cy; y++)
            {
                for (uint32_t x = 0; x < cx; )
                {
                    uint8_t bDst = 0;
                    uint8_t fBit = 0x80;
                    do
                    {
                        if ((pbSrc[x * 4] | pbSrc[x * 4 + 1] | pbSrc[x * 4 + 2] | pbSrc[x * 4 + 3]) >= 0xfc)
                            bDst |= fBit;
                        fBit >>= 1;
                        x++;
                    } while (x < cx && (x & 7));
                    pbDst[(x - 1) / 8] = bDst;
                }
                pbDst += cbDstAndLine;
                pbSrc += cbSrcAndLine;
            }
            break;
        default:
            RTMemFreeZ(pbCopy, cbCopy);
            AssertFailedReturnVoid();
    }

    /* Convert the XOR mask. */
    uint32_t *pu32Dst = (uint32_t *)(pbCopy + RT_ALIGN_32(cbDstAndMask, 4));
    pbSrc  = pbSrcXorMask;
    switch (pCmd->xorMaskDepth)
    {
        case 1:
            for (uint32_t y = 0; y < cy; y++)
            {
                for (uint32_t x = 0; x < cx; )
                {
                    /* most significant bit is the left most one. */
                    uint8_t bSrc = pbSrc[x / 8];
                    do
                    {
                        *pu32Dst++ = bSrc & 0x80 ? UINT32_C(0x00ffffff) : 0;
                        bSrc <<= 1;
                        x++;
                    } while ((x & 7) && x < cx);
                }
                pbSrc += cbSrcXorLine;
            }
            break;
        case 8:
            for (uint32_t y = 0; y < cy; y++)
            {
                for (uint32_t x = 0; x < cx; x++)
                {
                    uint32_t u = pThis->last_palette[pbSrc[x]];
                    *pu32Dst++ = u;//RT_MAKE_U32_FROM_U8(RT_BYTE1(u), RT_BYTE2(u), RT_BYTE3(u), 0);
                }
                pbSrc += cbSrcXorLine;
            }
            break;
        case 15: /* Src: RGB-5-5-5 */
            for (uint32_t y = 0; y < cy; y++)
            {
                for (uint32_t x = 0; x < cx; x++)
                {
                    uint32_t const uValue = RT_MAKE_U16(pbSrc[x * 2], pbSrc[x * 2 + 1]);
                    *pu32Dst++ = RT_MAKE_U32_FROM_U8(( uValue        & 0x1f) << 3,
                                                     ((uValue >>  5) & 0x1f) << 3,
                                                     ((uValue >> 10) & 0x1f) << 3, 0);
                }
                pbSrc += cbSrcXorLine;
            }
            break;
        case 16: /* Src: RGB-5-6-5 */
            for (uint32_t y = 0; y < cy; y++)
            {
                for (uint32_t x = 0; x < cx; x++)
                {
                    uint32_t const uValue = RT_MAKE_U16(pbSrc[x * 2], pbSrc[x * 2 + 1]);
                    *pu32Dst++ = RT_MAKE_U32_FROM_U8(( uValue        & 0x1f) << 3,
                                                     ((uValue >>  5) & 0x3f) << 2,
                                                     ((uValue >> 11) & 0x1f) << 3, 0);
                }
                pbSrc += cbSrcXorLine;
            }
            break;
        case 24:
            for (uint32_t y = 0; y < cy; y++)
            {
                for (uint32_t x = 0; x < cx; x++)
                    *pu32Dst++ = RT_MAKE_U32_FROM_U8(pbSrc[x*3], pbSrc[x*3 + 1], pbSrc[x*3 + 2], 0);
                pbSrc += cbSrcXorLine;
            }
            break;
        case 32:
            for (uint32_t y = 0; y < cy; y++)
            {
                for (uint32_t x = 0; x < cx; x++)
                    *pu32Dst++ = RT_MAKE_U32_FROM_U8(pbSrc[x*4], pbSrc[x*4 + 1], pbSrc[x*4 + 2], 0);
                pbSrc += cbSrcXorLine;
            }
            break;
        default:
            RTMemFreeZ(pbCopy, cbCopy);
            AssertFailedReturnVoid();
    }

    /*
     * Pass it to the frontend/whatever.
     */
    vmsvgaR3InstallNewCursor(pThisCC, pSvgaR3State, false /*fAlpha*/, pCmd->hotspotX, pCmd->hotspotY,
                             cx, cy, pbCopy, cbCopy);
}


/* SVGA_CMD_DEFINE_ALPHA_CURSOR */
void vmsvgaR3CmdDefineAlphaCursor(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdDefineAlphaCursor const *pCmd)
{
    RT_NOREF(pThis);
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdDefineAlphaCursor);
    Log(("VMSVGA cmd: SVGA_CMD_DEFINE_ALPHA_CURSOR id=%d size (%dx%d) hotspot (%d,%d)\n", pCmd->id, pCmd->width, pCmd->height, pCmd->hotspotX, pCmd->hotspotY));

    /* Check against a reasonable upper limit to prevent integer overflows in the sanity checks below. */
    ASSERT_GUEST_RETURN_VOID(pCmd->height < 2048 && pCmd->width < 2048);
    RT_UNTRUSTED_VALIDATED_FENCE();

    /* The mouse pointer interface always expects an AND mask followed by the color data (XOR mask). */
    uint32_t cbAndMask     = (pCmd->width + 7) / 8 * pCmd->height;          /* size of the AND mask */
    cbAndMask              = ((cbAndMask + 3) & ~3);                        /* + gap for alignment */
    uint32_t cbXorMask     = pCmd->width * sizeof(uint32_t) * pCmd->height; /* + size of the XOR mask (32-bit BRGA format) */
    uint32_t cbCursorShape = cbAndMask + cbXorMask;

    uint8_t *pCursorCopy = (uint8_t *)RTMemAlloc(cbCursorShape);
    AssertPtrReturnVoid(pCursorCopy);

    /* Transparency is defined by the alpha bytes, so make the whole bitmap visible. */
    memset(pCursorCopy, 0xff, cbAndMask);
    /* Colour data */
    memcpy(pCursorCopy + cbAndMask, pCmd + 1, cbXorMask);

    vmsvgaR3InstallNewCursor(pThisCC, pSvgaR3State, true /*fAlpha*/, pCmd->hotspotX, pCmd->hotspotY,
                             pCmd->width, pCmd->height, pCursorCopy, cbCursorShape);
}


/* SVGA_CMD_ESCAPE */
void vmsvgaR3CmdEscape(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdEscape const *pCmd)
{
    RT_NOREF(pThis);
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdEscape);

    if (pCmd->nsid == SVGA_ESCAPE_NSID_VMWARE)
    {
        ASSERT_GUEST_RETURN_VOID(pCmd->size >= sizeof(uint32_t));
        RT_UNTRUSTED_VALIDATED_FENCE();

        uint32_t const cmd = *(uint32_t *)(pCmd + 1);
        Log(("SVGA_CMD_ESCAPE (%#x %#x) VMWARE cmd=%#x\n", pCmd->nsid, pCmd->size, cmd));

        switch (cmd)
        {
            case SVGA_ESCAPE_VMWARE_VIDEO_SET_REGS:
            {
                SVGAEscapeVideoSetRegs *pVideoCmd = (SVGAEscapeVideoSetRegs *)(pCmd + 1);
                ASSERT_GUEST_RETURN_VOID(pCmd->size >= sizeof(pVideoCmd->header));
                RT_UNTRUSTED_VALIDATED_FENCE();

                uint32_t const cRegs = (pCmd->size - sizeof(pVideoCmd->header)) / sizeof(pVideoCmd->items[0]);

                Log(("SVGA_ESCAPE_VMWARE_VIDEO_SET_REGS: stream %#x\n", pVideoCmd->header.streamId));
                for (uint32_t iReg = 0; iReg < cRegs; iReg++)
                    Log(("SVGA_ESCAPE_VMWARE_VIDEO_SET_REGS: reg %#x val %#x\n", pVideoCmd->items[iReg].registerId, pVideoCmd->items[iReg].value));
                RT_NOREF_PV(pVideoCmd);
                break;
            }

            case SVGA_ESCAPE_VMWARE_VIDEO_FLUSH:
            {
                SVGAEscapeVideoFlush *pVideoCmd = (SVGAEscapeVideoFlush *)(pCmd + 1);
                ASSERT_GUEST_RETURN_VOID(pCmd->size >= sizeof(*pVideoCmd));
                Log(("SVGA_ESCAPE_VMWARE_VIDEO_FLUSH: stream %#x\n", pVideoCmd->streamId));
                RT_NOREF_PV(pVideoCmd);
                break;
            }

            default:
                Log(("SVGA_CMD_ESCAPE: Unknown vmware escape: %#x\n", cmd));
                break;
        }
    }
    else
        Log(("SVGA_CMD_ESCAPE %#x %#x\n", pCmd->nsid, pCmd->size));
}


/* SVGA_CMD_DEFINE_SCREEN */
void vmsvgaR3CmdDefineScreen(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdDefineScreen const *pCmd)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdDefineScreen);
    Log(("SVGA_CMD_DEFINE_SCREEN id=%x flags=%x size=(%d,%d) root=(%d,%d) %d:0x%x 0x%x\n",
             pCmd->screen.id, pCmd->screen.flags, pCmd->screen.size.width, pCmd->screen.size.height, pCmd->screen.root.x, pCmd->screen.root.y,
             pCmd->screen.backingStore.ptr.gmrId, pCmd->screen.backingStore.ptr.offset, pCmd->screen.backingStore.pitch));

    uint32_t const idScreen = pCmd->screen.id;
    ASSERT_GUEST_RETURN_VOID(idScreen < RT_ELEMENTS(pSvgaR3State->aScreens));

    uint32_t const uWidth = pCmd->screen.size.width;
    ASSERT_GUEST_RETURN_VOID(uWidth <= pThis->svga.u32MaxWidth);

    uint32_t const uHeight = pCmd->screen.size.height;
    ASSERT_GUEST_RETURN_VOID(uHeight <= pThis->svga.u32MaxHeight);

    uint32_t const cbWidth = uWidth * ((32 + 7) / 8); /** @todo 32? */
    uint32_t const cbPitch = pCmd->screen.backingStore.pitch ? pCmd->screen.backingStore.pitch : cbWidth;
    ASSERT_GUEST_RETURN_VOID(cbWidth <= cbPitch);

    uint32_t const uScreenOffset = pCmd->screen.backingStore.ptr.offset;
    ASSERT_GUEST_RETURN_VOID(uScreenOffset < pThis->vram_size);

    uint32_t const cbVram = pThis->vram_size - uScreenOffset;
    /* If we have a not zero pitch, then height can't exceed the available VRAM. */
    ASSERT_GUEST_RETURN_VOID(   (uHeight == 0 && cbPitch == 0)
                             || (cbPitch > 0 && uHeight <= cbVram / cbPitch));
    RT_UNTRUSTED_VALIDATED_FENCE();

    VMSVGASCREENOBJECT *pScreen = &pSvgaR3State->aScreens[idScreen];
    Assert(pScreen->idScreen == idScreen);
    pScreen->cDpi      = 0; /* SVGAFifoCmdDefineScreen does not support dpi. */

    /* SVGAFifoCmdDefineScreen uses the guest VRAM. The screen bitmap must be deallocated after 'vmsvgaR3ChangeMode'. */
    void *pvOldScreenBitmap = pScreen->pvScreenBitmap;
    pScreen->pvScreenBitmap = 0;

    pScreen->fDefined  = true;
    pScreen->fModified = true;
    pScreen->fuScreen  = pCmd->screen.flags;
    if (!RT_BOOL(pCmd->screen.flags & (SVGA_SCREEN_DEACTIVATE | SVGA_SCREEN_BLANKING)))
    {
        /* Not blanked. */
        ASSERT_GUEST_RETURN_VOID(uWidth > 0 && uHeight > 0);
        RT_UNTRUSTED_VALIDATED_FENCE();

        pScreen->xOrigin = pCmd->screen.root.x;
        pScreen->yOrigin = pCmd->screen.root.y;
        pScreen->cWidth  = uWidth;
        pScreen->cHeight = uHeight;
        pScreen->offVRAM = uScreenOffset;
        pScreen->cbPitch = cbPitch;
        pScreen->cBpp    = 32;
    }
    else
    {
        /* Screen blanked. Keep old values. */
    }

#ifdef VBOX_WITH_VMSVGA3D
    if (RT_LIKELY(pThis->svga.f3DEnabled))
        vmsvga3dDefineScreen(pThis, pThisCC, pScreen);
#endif

    pThis->svga.fGFBRegisters = false;
    vmsvgaR3ChangeMode(pThis, pThisCC);

    RTMemFree(pvOldScreenBitmap);
}


/* SVGA_CMD_DESTROY_SCREEN */
void vmsvgaR3CmdDestroyScreen(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdDestroyScreen const *pCmd)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdDestroyScreen);
    Log(("SVGA_CMD_DESTROY_SCREEN id=%x\n", pCmd->screenId));

    uint32_t const idScreen = pCmd->screenId;
    ASSERT_GUEST_RETURN_VOID(idScreen < RT_ELEMENTS(pSvgaR3State->aScreens));
    RT_UNTRUSTED_VALIDATED_FENCE();

    VMSVGASCREENOBJECT *pScreen = &pSvgaR3State->aScreens[idScreen];
    Assert(pScreen->idScreen == idScreen);
    vmsvgaR3DestroyScreen(pThis, pThisCC, pScreen);
}


/* SVGA_CMD_DEFINE_GMRFB */
void vmsvgaR3CmdDefineGMRFB(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdDefineGMRFB const *pCmd)
{
    RT_NOREF(pThis);
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdDefineGmrFb);
    Log(("SVGA_CMD_DEFINE_GMRFB gmr=0x%x offset=0x%x bytesPerLine=0x%x(%d) bpp=%d color depth=%d\n",
             pCmd->ptr.gmrId, pCmd->ptr.offset, pCmd->bytesPerLine, pCmd->bytesPerLine, pCmd->format.bitsPerPixel, pCmd->format.colorDepth));

    pSvgaR3State->GMRFB.ptr          = pCmd->ptr;
    pSvgaR3State->GMRFB.bytesPerLine = pCmd->bytesPerLine;
    pSvgaR3State->GMRFB.format       = pCmd->format;
}


/* SVGA_CMD_BLIT_GMRFB_TO_SCREEN */
void vmsvgaR3CmdBlitGMRFBToScreen(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdBlitGMRFBToScreen const *pCmd)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdBlitGmrFbToScreen);
    Log(("SVGA_CMD_BLIT_GMRFB_TO_SCREEN src=(%d,%d) dest id=%d (%d,%d)(%d,%d)\n",
             pCmd->srcOrigin.x, pCmd->srcOrigin.y, pCmd->destScreenId, pCmd->destRect.left, pCmd->destRect.top, pCmd->destRect.right, pCmd->destRect.bottom));

    ASSERT_GUEST_RETURN_VOID(pCmd->destScreenId < RT_ELEMENTS(pSvgaR3State->aScreens));
    RT_UNTRUSTED_VALIDATED_FENCE();

    VMSVGASCREENOBJECT *pScreen = vmsvgaR3GetScreenObject(pThisCC, pCmd->destScreenId);
    AssertPtrReturnVoid(pScreen);

    Log(("SVGA_CMD_BLIT_GMRFB_TO_SCREEN screen(%d): x=%d y=%d w=%d h=%d offVRAM=0x%x cbPitch=0x%x(%d)\n",
            pScreen->idScreen,
            pScreen->xOrigin, pScreen->yOrigin, pScreen->cWidth, pScreen->cHeight,
            pScreen->offVRAM, pScreen->cbPitch, pScreen->cbPitch));

    /** @todo Support GMRFB.format.s.bitsPerPixel != pThis->svga.uBpp ?  */
    AssertReturnVoid(pSvgaR3State->GMRFB.format.bitsPerPixel == pScreen->cBpp);

    /* Clip destRect to the screen dimensions. */
    SVGASignedRect screenRect;
    screenRect.left   = 0;
    screenRect.top    = 0;
    screenRect.right  = pScreen->cWidth;
    screenRect.bottom = pScreen->cHeight;
    SVGASignedRect clipRect = pCmd->destRect;
    vmsvgaR3ClipRect(&screenRect, &clipRect);
    RT_UNTRUSTED_VALIDATED_FENCE();

    uint32_t const width  = clipRect.right - clipRect.left;
    uint32_t const height = clipRect.bottom - clipRect.top;

    if (   width == 0
        || height == 0)
        return;  /* Nothing to do. */

    int32_t const srcx = pCmd->srcOrigin.x + (clipRect.left - pCmd->destRect.left);
    int32_t const srcy = pCmd->srcOrigin.y + (clipRect.top - pCmd->destRect.top);

    /* Copy the defined by GMRFB image to the screen 0 VRAM area.
     * Prepare parameters for vmsvgaR3GmrTransfer.
     */
    AssertReturnVoid(pScreen->offVRAM < pThis->vram_size); /* Paranoia. Ensured by SVGA_CMD_DEFINE_SCREEN. */

    /* Destination: host buffer which describes the screen 0 VRAM.
     * Important are pbHstBuf and cbHstBuf. offHst and cbHstPitch are verified by vmsvgaR3GmrTransfer.
     */
    uint8_t * const pbHstBuf = (uint8_t *)pThisCC->pbVRam + pScreen->offVRAM;
    uint32_t const cbScanline = pScreen->cbPitch ? pScreen->cbPitch :
                                                   width * (RT_ALIGN(pScreen->cBpp, 8) / 8);
    uint32_t cbHstBuf = cbScanline * pScreen->cHeight;
    if (cbHstBuf > pThis->vram_size - pScreen->offVRAM)
       cbHstBuf = pThis->vram_size - pScreen->offVRAM; /* Paranoia. */
    uint32_t const offHst =   (clipRect.left * RT_ALIGN(pScreen->cBpp, 8)) / 8
                            + cbScanline * clipRect.top;
    int32_t const cbHstPitch = cbScanline;

    /* Source: GMRFB. vmsvgaR3GmrTransfer ensures that no memory outside the GMR is read. */
    SVGAGuestPtr const gstPtr = pSvgaR3State->GMRFB.ptr;
    uint32_t const offGst =  (srcx * RT_ALIGN(pSvgaR3State->GMRFB.format.bitsPerPixel, 8)) / 8
                           + pSvgaR3State->GMRFB.bytesPerLine * srcy;
    int32_t const cbGstPitch = pSvgaR3State->GMRFB.bytesPerLine;

    int rc = vmsvgaR3GmrTransfer(pThis, pThisCC, SVGA3D_WRITE_HOST_VRAM,
                                 pbHstBuf, cbHstBuf, offHst, cbHstPitch,
                                 gstPtr, offGst, cbGstPitch,
                                 (width * RT_ALIGN(pScreen->cBpp, 8)) / 8, height);
    AssertRC(rc);
    vmsvgaR3UpdateScreen(pThisCC, pScreen, clipRect.left, clipRect.top, width, height);
}


/* SVGA_CMD_BLIT_SCREEN_TO_GMRFB */
void vmsvgaR3CmdBlitScreenToGMRFB(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdBlitScreenToGMRFB const *pCmd)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdBlitScreentoGmrFb);
    /* Note! This can fetch 3d render results as well!! */
    Log(("SVGA_CMD_BLIT_SCREEN_TO_GMRFB dest=(%d,%d) src id=%d (%d,%d)(%d,%d)\n",
             pCmd->destOrigin.x, pCmd->destOrigin.y, pCmd->srcScreenId, pCmd->srcRect.left, pCmd->srcRect.top, pCmd->srcRect.right, pCmd->srcRect.bottom));

    ASSERT_GUEST_RETURN_VOID(pCmd->srcScreenId < RT_ELEMENTS(pSvgaR3State->aScreens));
    RT_UNTRUSTED_VALIDATED_FENCE();

    VMSVGASCREENOBJECT *pScreen = vmsvgaR3GetScreenObject(pThisCC, pCmd->srcScreenId);
    AssertPtrReturnVoid(pScreen);

    /** @todo Support GMRFB.format.bitsPerPixel != pThis->svga.uBpp ? */
    AssertReturnVoid(pSvgaR3State->GMRFB.format.bitsPerPixel == pScreen->cBpp);

    /* Clip destRect to the screen dimensions. */
    SVGASignedRect screenRect;
    screenRect.left   = 0;
    screenRect.top    = 0;
    screenRect.right  = pScreen->cWidth;
    screenRect.bottom = pScreen->cHeight;
    SVGASignedRect clipRect = pCmd->srcRect;
    vmsvgaR3ClipRect(&screenRect, &clipRect);
    RT_UNTRUSTED_VALIDATED_FENCE();

    uint32_t const width  = clipRect.right - clipRect.left;
    uint32_t const height = clipRect.bottom - clipRect.top;

    if (   width == 0
        || height == 0)
        return;  /* Nothing to do. */

    int32_t const dstx = pCmd->destOrigin.x + (clipRect.left - pCmd->srcRect.left);
    int32_t const dsty = pCmd->destOrigin.y + (clipRect.top - pCmd->srcRect.top);

    /* Copy the defined by GMRFB image to the screen 0 VRAM area.
     * Prepare parameters for vmsvgaR3GmrTransfer.
     */
    AssertReturnVoid(pScreen->offVRAM < pThis->vram_size); /* Paranoia. Ensured by SVGA_CMD_DEFINE_SCREEN. */

    /* Source: host buffer which describes the screen 0 VRAM.
     * Important are pbHstBuf and cbHstBuf. offHst and cbHstPitch are verified by vmsvgaR3GmrTransfer.
     */
    uint8_t * const pbHstBuf = (uint8_t *)pThisCC->pbVRam + pScreen->offVRAM;
    uint32_t const cbScanline = pScreen->cbPitch ? pScreen->cbPitch :
                                                   width * (RT_ALIGN(pScreen->cBpp, 8) / 8);
    uint32_t cbHstBuf = cbScanline * pScreen->cHeight;
    if (cbHstBuf > pThis->vram_size - pScreen->offVRAM)
       cbHstBuf = pThis->vram_size - pScreen->offVRAM; /* Paranoia. */
    uint32_t const offHst =   (clipRect.left * RT_ALIGN(pScreen->cBpp, 8)) / 8
                            + cbScanline * clipRect.top;
    int32_t const cbHstPitch = cbScanline;

    /* Destination: GMRFB. vmsvgaR3GmrTransfer ensures that no memory outside the GMR is read. */
    SVGAGuestPtr const gstPtr = pSvgaR3State->GMRFB.ptr;
    uint32_t const offGst =  (dstx * RT_ALIGN(pSvgaR3State->GMRFB.format.bitsPerPixel, 8)) / 8
                           + pSvgaR3State->GMRFB.bytesPerLine * dsty;
    int32_t const cbGstPitch = pSvgaR3State->GMRFB.bytesPerLine;

    int rc = vmsvgaR3GmrTransfer(pThis, pThisCC, SVGA3D_READ_HOST_VRAM,
                                 pbHstBuf, cbHstBuf, offHst, cbHstPitch,
                                 gstPtr, offGst, cbGstPitch,
                                 (width * RT_ALIGN(pScreen->cBpp, 8)) / 8, height);
    AssertRC(rc);
}


/* SVGA_CMD_ANNOTATION_FILL */
void vmsvgaR3CmdAnnotationFill(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdAnnotationFill const *pCmd)
{
    RT_NOREF(pThis);
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdAnnotationFill);
    Log(("SVGA_CMD_ANNOTATION_FILL red=%x green=%x blue=%x\n", pCmd->color.r, pCmd->color.g, pCmd->color.b));

    pSvgaR3State->colorAnnotation = pCmd->color;
}


/* SVGA_CMD_ANNOTATION_COPY */
void vmsvgaR3CmdAnnotationCopy(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdAnnotationCopy const *pCmd)
{
    RT_NOREF(pThis, pCmd);
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdAnnotationCopy);
    Log(("SVGA_CMD_ANNOTATION_COPY srcOrigin %d,%d, srcScreenId %u\n", pCmd->srcOrigin.x, pCmd->srcOrigin.y, pCmd->srcScreenId));

    AssertFailed();
}


#ifdef VBOX_WITH_VMSVGA3D
/* SVGA_CMD_DEFINE_GMR2 */
void vmsvgaR3CmdDefineGMR2(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdDefineGMR2 const *pCmd)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdDefineGmr2);
    Log(("SVGA_CMD_DEFINE_GMR2 id=%#x %#x pages\n", pCmd->gmrId, pCmd->numPages));

    /* Validate current GMR id. */
    ASSERT_GUEST_RETURN_VOID(pCmd->gmrId < pThis->svga.cGMR);
    ASSERT_GUEST_RETURN_VOID(pCmd->numPages <= VMSVGA_MAX_GMR_PAGES);
    RT_UNTRUSTED_VALIDATED_FENCE();

    if (!pCmd->numPages)
    {
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdDefineGmr2Free);
        vmsvgaR3GmrFree(pThisCC, pCmd->gmrId);
    }
    else
    {
        PGMR pGMR = &pSvgaR3State->paGMR[pCmd->gmrId];
        if (pGMR->cMaxPages)
            STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdDefineGmr2Modify);

        /* Not sure if we should always free the descriptor, but for simplicity
           we do so if the new size is smaller than the current. */
        /** @todo always free the descriptor in SVGA_CMD_DEFINE_GMR2? */
        if (pGMR->cbTotal / X86_PAGE_SIZE > pCmd->numPages)
            vmsvgaR3GmrFree(pThisCC, pCmd->gmrId);

        pGMR->cMaxPages = pCmd->numPages;
        /* The rest is done by the REMAP_GMR2 command. */
    }
}


/* SVGA_CMD_REMAP_GMR2 */
void vmsvgaR3CmdRemapGMR2(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdRemapGMR2 const *pCmd)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdRemapGmr2);
    Log(("SVGA_CMD_REMAP_GMR2 id=%#x flags=%#x offset=%#x npages=%#x\n", pCmd->gmrId, pCmd->flags, pCmd->offsetPages, pCmd->numPages));

    /* Validate current GMR id and size. */
    ASSERT_GUEST_RETURN_VOID(pCmd->gmrId < pThis->svga.cGMR);
    RT_UNTRUSTED_VALIDATED_FENCE();
    PGMR pGMR = &pSvgaR3State->paGMR[pCmd->gmrId];
    ASSERT_GUEST_RETURN_VOID(   (uint64_t)pCmd->offsetPages + pCmd->numPages
                             <= RT_MIN(pGMR->cMaxPages, RT_MIN(VMSVGA_MAX_GMR_PAGES, UINT32_MAX / X86_PAGE_SIZE)));
    ASSERT_GUEST_RETURN_VOID(!pCmd->offsetPages || pGMR->paDesc); /** @todo */

    if (pCmd->numPages == 0)
        return;
    RT_UNTRUSTED_VALIDATED_FENCE();

    /* Calc new total page count so we can use it instead of cMaxPages for allocations below. */
    uint32_t const cNewTotalPages = RT_MAX(pGMR->cbTotal >> X86_PAGE_SHIFT, pCmd->offsetPages + pCmd->numPages);

    /*
     * We flatten the existing descriptors into a page array, overwrite the
     * pages specified in this command and then recompress the descriptor.
     */
    /** @todo Optimize the GMR remap algorithm! */

    /* Save the old page descriptors as an array of page frame numbers (address >> X86_PAGE_SHIFT) */
    uint64_t *paNewPage64 = NULL;
    if (pGMR->paDesc)
    {
        STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdRemapGmr2Modify);

        paNewPage64 = (uint64_t *)RTMemAllocZ(cNewTotalPages * sizeof(uint64_t));
        AssertPtrReturnVoid(paNewPage64);

        uint32_t idxPage = 0;
        for (uint32_t i = 0; i < pGMR->numDescriptors; i++)
            for (uint32_t j = 0; j < pGMR->paDesc[i].numPages; j++)
                paNewPage64[idxPage++] = (pGMR->paDesc[i].GCPhys + j * X86_PAGE_SIZE) >> X86_PAGE_SHIFT;
        AssertReturnVoidStmt(idxPage == pGMR->cbTotal >> X86_PAGE_SHIFT, RTMemFree(paNewPage64));
        RT_UNTRUSTED_VALIDATED_FENCE();
    }

    /* Free the old GMR if present. */
    if (pGMR->paDesc)
        RTMemFree(pGMR->paDesc);

    /* Allocate the maximum amount possible (everything non-continuous) */
    PVMSVGAGMRDESCRIPTOR paDescs;
    pGMR->paDesc = paDescs = (PVMSVGAGMRDESCRIPTOR)RTMemAllocZ(cNewTotalPages * sizeof(VMSVGAGMRDESCRIPTOR));
    AssertReturnVoidStmt(paDescs, RTMemFree(paNewPage64));

    if (pCmd->flags & SVGA_REMAP_GMR2_VIA_GMR)
    {
        /** @todo */
        AssertFailed();
        pGMR->numDescriptors = 0;
    }
    else
    {
        uint32_t  *paPages32 = (uint32_t *)(pCmd + 1);
        uint64_t  *paPages64 = (uint64_t *)(pCmd + 1);
        bool       fGCPhys64 = RT_BOOL(pCmd->flags & SVGA_REMAP_GMR2_PPN64);

        uint32_t cPages;
        if (paNewPage64)
        {
            /* Overwrite the old page array with the new page values. */
            if (fGCPhys64)
                for (uint32_t i = pCmd->offsetPages; i < pCmd->offsetPages + pCmd->numPages; i++)
                    paNewPage64[i] = paPages64[i - pCmd->offsetPages];
            else
                for (uint32_t i = pCmd->offsetPages; i < pCmd->offsetPages + pCmd->numPages; i++)
                    paNewPage64[i] = paPages32[i - pCmd->offsetPages];

            /* Use the updated page array instead of the command data. */
            fGCPhys64      = true;
            paPages64      = paNewPage64;
            cPages = cNewTotalPages;
        }
        else
            cPages = pCmd->numPages;

        /* The first page. */
        /** @todo The 0x00000FFFFFFFFFFF mask limits to 44 bits and should not be
         *        applied to paNewPage64. */
        RTGCPHYS GCPhys;
        if (fGCPhys64)
            GCPhys = (paPages64[0] << X86_PAGE_SHIFT) & UINT64_C(0x00000FFFFFFFFFFF); /* Seeing rubbish in the top bits with certain linux guests. */
        else
            GCPhys = (RTGCPHYS)paPages32[0] << GUEST_PAGE_SHIFT;
        paDescs[0].GCPhys    = GCPhys;
        paDescs[0].numPages  = 1;

        /* Subsequent pages. */
        uint32_t iDescriptor = 0;
        for (uint32_t i = 1; i < cPages; i++)
        {
            if (pCmd->flags & SVGA_REMAP_GMR2_PPN64)
                GCPhys = (paPages64[i] << X86_PAGE_SHIFT) & UINT64_C(0x00000FFFFFFFFFFF); /* Seeing rubbish in the top bits with certain linux guests. */
            else
                GCPhys = (RTGCPHYS)paPages32[i] << X86_PAGE_SHIFT;

            /* Continuous physical memory? */
            if (GCPhys == paDescs[iDescriptor].GCPhys + paDescs[iDescriptor].numPages * X86_PAGE_SIZE)
            {
                Assert(paDescs[iDescriptor].numPages);
                paDescs[iDescriptor].numPages++;
                Log5Func(("Page %x GCPhys=%RGp successor\n", i, GCPhys));
            }
            else
            {
                iDescriptor++;
                paDescs[iDescriptor].GCPhys   = GCPhys;
                paDescs[iDescriptor].numPages = 1;
                Log5Func(("Page %x GCPhys=%RGp\n", i, paDescs[iDescriptor].GCPhys));
            }
        }

        pGMR->cbTotal = cNewTotalPages << X86_PAGE_SHIFT;
        Log5Func(("Nr of descriptors %x; cbTotal=%#x\n", iDescriptor + 1, cNewTotalPages));
        pGMR->numDescriptors = iDescriptor + 1;
    }

    if (paNewPage64)
        RTMemFree(paNewPage64);
}


/**
 * Free the specified GMR
 *
 * @param   pThisCC         The VGA/VMSVGA state for ring-3.
 * @param   idGMR           GMR id
 */
void vmsvgaR3GmrFree(PVGASTATECC pThisCC, uint32_t idGMR)
{
    PVMSVGAR3STATE pSVGAState = pThisCC->svga.pSvgaR3State;

    /* Free the old descriptor if present. */
    PGMR pGMR = &pSVGAState->paGMR[idGMR];
    if (   pGMR->numDescriptors
        || pGMR->paDesc /* needed till we implement SVGA_REMAP_GMR2_VIA_GMR */)
    {
# ifdef DEBUG_GMR_ACCESS
        VMR3ReqCallWaitU(PDMDevHlpGetUVM(pThisCC->pDevIns), VMCPUID_ANY, (PFNRT)vmsvgaR3DeregisterGmr, 2, pDevIns, idGMR);
# endif

        Assert(pGMR->paDesc);
        RTMemFree(pGMR->paDesc);
        pGMR->paDesc         = NULL;
        pGMR->numDescriptors = 0;
        pGMR->cbTotal        = 0;
        pGMR->cMaxPages      = 0;
    }
    Assert(!pGMR->cMaxPages);
    Assert(!pGMR->cbTotal);
}
#endif /* VBOX_WITH_VMSVGA3D */


/**
 * Copy between a GMR and a host memory buffer.
 *
 * @returns VBox status code.
 * @param   pThis           The shared VGA/VMSVGA instance data.
 * @param   pThisCC         The VGA/VMSVGA state for ring-3.
 * @param   enmTransferType Transfer type (read/write)
 * @param   pbHstBuf        Host buffer pointer (valid)
 * @param   cbHstBuf        Size of host buffer (valid)
 * @param   offHst          Host buffer offset of the first scanline
 * @param   cbHstPitch      Destination buffer pitch
 * @param   gstPtr          GMR description
 * @param   offGst          Guest buffer offset of the first scanline
 * @param   cbGstPitch      Guest buffer pitch
 * @param   cbWidth         Width in bytes to copy
 * @param   cHeight         Number of scanllines to copy
 */
int vmsvgaR3GmrTransfer(PVGASTATE pThis, PVGASTATECC pThisCC, const SVGA3dTransferType enmTransferType,
                        uint8_t *pbHstBuf, uint32_t cbHstBuf, uint32_t offHst, int32_t cbHstPitch,
                        SVGAGuestPtr gstPtr, uint32_t offGst, int32_t cbGstPitch,
                        uint32_t cbWidth, uint32_t cHeight)
{
    PVMSVGAR3STATE  pSVGAState = pThisCC->svga.pSvgaR3State;
    PPDMDEVINS      pDevIns = pThisCC->pDevIns; /* simpler */
    int             rc;

    LogFunc(("%s host %p size=%d offset %d pitch=%d; guest gmr=%#x:%#x offset=%d pitch=%d cbWidth=%d cHeight=%d\n",
             enmTransferType == SVGA3D_READ_HOST_VRAM ? "WRITE" : "READ", /* GMR op: READ host VRAM means WRITE GMR */
             pbHstBuf, cbHstBuf, offHst, cbHstPitch,
             gstPtr.gmrId, gstPtr.offset, offGst, cbGstPitch, cbWidth, cHeight));
    AssertReturn(cbWidth && cHeight, VERR_INVALID_PARAMETER);

    PGMR pGMR;
    uint32_t cbGmr; /* The GMR size in bytes. */
    if (gstPtr.gmrId == SVGA_GMR_FRAMEBUFFER)
    {
        pGMR = NULL;
        cbGmr = pThis->vram_size;
    }
    else
    {
        AssertReturn(gstPtr.gmrId < pThis->svga.cGMR, VERR_INVALID_PARAMETER);
        RT_UNTRUSTED_VALIDATED_FENCE();
        pGMR = &pSVGAState->paGMR[gstPtr.gmrId];
        cbGmr = pGMR->cbTotal;
    }

    /*
     * GMR
     */
    /* Calculate GMR offset of the data to be copied. */
    AssertMsgReturn(gstPtr.offset < cbGmr,
                    ("gmr=%#x:%#x offGst=%#x cbGstPitch=%#x cHeight=%#x cbWidth=%#x cbGmr=%#x\n",
                     gstPtr.gmrId, gstPtr.offset, offGst, cbGstPitch, cHeight, cbWidth, cbGmr),
                    VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();
    AssertMsgReturn(offGst < cbGmr - gstPtr.offset,
                    ("gmr=%#x:%#x offGst=%#x cbGstPitch=%#x cHeight=%#x cbWidth=%#x cbGmr=%#x\n",
                     gstPtr.gmrId, gstPtr.offset, offGst, cbGstPitch, cHeight, cbWidth, cbGmr),
                    VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();
    uint32_t const offGmr = offGst + gstPtr.offset; /* Offset in the GMR, where the first scanline is located. */

    /* Verify that cbWidth is less than scanline and fits into the GMR. */
    uint32_t const cbGmrScanline = cbGstPitch > 0 ? cbGstPitch : -cbGstPitch;
    AssertMsgReturn(cbGmrScanline != 0,
                    ("gmr=%#x:%#x offGst=%#x cbGstPitch=%#x cHeight=%#x cbWidth=%#x cbGmr=%#x\n",
                     gstPtr.gmrId, gstPtr.offset, offGst, cbGstPitch, cHeight, cbWidth, cbGmr),
                    VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();
    AssertMsgReturn(cbWidth <= cbGmrScanline,
                    ("gmr=%#x:%#x offGst=%#x cbGstPitch=%#x cHeight=%#x cbWidth=%#x cbGmr=%#x\n",
                     gstPtr.gmrId, gstPtr.offset, offGst, cbGstPitch, cHeight, cbWidth, cbGmr),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(cbWidth <= cbGmr - offGmr,
                    ("gmr=%#x:%#x offGst=%#x cbGstPitch=%#x cHeight=%#x cbWidth=%#x cbGmr=%#x\n",
                     gstPtr.gmrId, gstPtr.offset, offGst, cbGstPitch, cHeight, cbWidth, cbGmr),
                    VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();

    /* How many bytes are available for the data in the GMR. */
    uint32_t const cbGmrLeft = cbGstPitch > 0 ? cbGmr - offGmr : offGmr + cbWidth;

    /* How many scanlines would fit into the available data. */
    uint32_t cGmrScanlines = cbGmrLeft / cbGmrScanline;
    uint32_t const cbGmrLastScanline = cbGmrLeft - cGmrScanlines * cbGmrScanline; /* Slack space. */
    if (cbWidth <= cbGmrLastScanline)
        ++cGmrScanlines;

    if (cHeight > cGmrScanlines)
        cHeight = cGmrScanlines;

    AssertMsgReturn(cHeight > 0,
                    ("gmr=%#x:%#x offGst=%#x cbGstPitch=%#x cHeight=%#x cbWidth=%#x cbGmr=%#x\n",
                     gstPtr.gmrId, gstPtr.offset, offGst, cbGstPitch, cHeight, cbWidth, cbGmr),
                    VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();

    /*
     * Host buffer.
     */
    AssertMsgReturn(offHst < cbHstBuf,
                    ("buffer=%p size %d offHst=%d cbHstPitch=%d cHeight=%d cbWidth=%d\n",
                     pbHstBuf, cbHstBuf, offHst, cbHstPitch, cHeight, cbWidth),
                    VERR_INVALID_PARAMETER);

    /* Verify that cbWidth is less than scanline and fits into the buffer. */
    uint32_t const cbHstScanline = cbHstPitch > 0 ? cbHstPitch : -cbHstPitch;
    AssertMsgReturn(cbHstScanline != 0,
                    ("buffer=%p size %d offHst=%d cbHstPitch=%d cHeight=%d cbWidth=%d\n",
                     pbHstBuf, cbHstBuf, offHst, cbHstPitch, cHeight, cbWidth),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(cbWidth <= cbHstScanline,
                    ("buffer=%p size %d offHst=%d cbHstPitch=%d cHeight=%d cbWidth=%d\n",
                     pbHstBuf, cbHstBuf, offHst, cbHstPitch, cHeight, cbWidth),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(cbWidth <= cbHstBuf - offHst,
                    ("buffer=%p size %d offHst=%d cbHstPitch=%d cHeight=%d cbWidth=%d\n",
                     pbHstBuf, cbHstBuf, offHst, cbHstPitch, cHeight, cbWidth),
                    VERR_INVALID_PARAMETER);

    /* How many bytes are available for the data in the buffer. */
    uint32_t const cbHstLeft = cbHstPitch > 0 ? cbHstBuf - offHst : offHst + cbWidth;

    /* How many scanlines would fit into the available data. */
    uint32_t cHstScanlines = cbHstLeft / cbHstScanline;
    uint32_t const cbHstLastScanline = cbHstLeft - cHstScanlines * cbHstScanline; /* Slack space. */
    if (cbWidth <= cbHstLastScanline)
        ++cHstScanlines;

    if (cHeight > cHstScanlines)
        cHeight = cHstScanlines;

    AssertMsgReturn(cHeight > 0,
                    ("buffer=%p size %d offHst=%d cbHstPitch=%d cHeight=%d cbWidth=%d\n",
                     pbHstBuf, cbHstBuf, offHst, cbHstPitch, cHeight, cbWidth),
                    VERR_INVALID_PARAMETER);

    uint8_t *pbHst = pbHstBuf + offHst;

    /* Shortcut for the framebuffer. */
    if (gstPtr.gmrId == SVGA_GMR_FRAMEBUFFER)
    {
        uint8_t *pbGst = pThisCC->pbVRam + offGmr;

        uint8_t const *pbSrc;
        int32_t cbSrcPitch;
        uint8_t *pbDst;
        int32_t cbDstPitch;

        if (enmTransferType == SVGA3D_READ_HOST_VRAM)
        {
            pbSrc      = pbHst;
            cbSrcPitch = cbHstPitch;
            pbDst      = pbGst;
            cbDstPitch = cbGstPitch;
        }
        else
        {
            pbSrc      = pbGst;
            cbSrcPitch = cbGstPitch;
            pbDst      = pbHst;
            cbDstPitch = cbHstPitch;
        }

        if (pbDst > pbSrc)
        {
            if (pbDst - pbSrc < cbSrcPitch * cHeight)
            {
                LogFlow(("Src buffer 0x%p overlaps Dst buffer 0x%p, cbSrcPitch %d, cbDstPitch %d, cbWidth %u, cHeight %u\n",
                    pbSrc, pbDst, cbSrcPitch, cbDstPitch, cbWidth, cHeight));
            }
        }
        else if (pbSrc > pbDst)
        {
            if (pbSrc - pbDst < cbDstPitch * cHeight)
            {
                LogFlow(("Dst buffer 0x%p overlaps Src buffer 0x%p, cbSrcPitch %d, cbDstPitch %d, cbWidth %u, cHeight %u\n",
                    pbDst, pbSrc, cbSrcPitch, cbDstPitch, cbWidth, cHeight));
            }
        }
        else
        {
            LogFlow(("Src and Dst buffers are both start at 0x%p, cbSrcPitch %d, cbDstPitch %d, cbWidth %u, cHeight %u\n",
                pbSrc, cbSrcPitch, cbDstPitch, cbWidth, cHeight));
        }

        if (   cbWidth == (uint32_t)cbGstPitch
            && cbGstPitch == cbHstPitch)
        {
            /* Entire scanlines, positive pitch. */
            memmove(pbDst, pbSrc, cbWidth * cHeight);
        }
        else
        {
            for (uint32_t i = 0; i < cHeight; ++i)
            {
                memmove(pbDst, pbSrc, cbWidth);

                pbDst += cbDstPitch;
                pbSrc += cbSrcPitch;
            }
        }
        return VINF_SUCCESS;
    }

    AssertPtrReturn(pGMR, VERR_INVALID_PARAMETER);
    AssertReturn(pGMR->numDescriptors > 0, VERR_INVALID_PARAMETER);

    PVMSVGAGMRDESCRIPTOR const paDesc = pGMR->paDesc; /* Local copy of the pointer. */
    uint32_t iDesc = 0;                               /* Index in the descriptor array. */
    uint32_t offDesc = 0;                             /* GMR offset of the current descriptor. */
    uint32_t offGmrScanline = offGmr;                 /* GMR offset of the scanline which is being copied. */
    uint8_t *pbHstScanline = pbHst;                   /* Host address of the scanline which is being copied. */
    for (uint32_t i = 0; i < cHeight; ++i)
    {
        uint32_t cbCurrentWidth = cbWidth;
        uint32_t offGmrCurrent  = offGmrScanline;
        uint8_t *pbCurrentHost  = pbHstScanline;

        /* Find the right descriptor */
        while (offDesc + paDesc[iDesc].numPages * GUEST_PAGE_SIZE <= offGmrCurrent)
        {
            offDesc += paDesc[iDesc].numPages * GUEST_PAGE_SIZE;
            AssertReturn(offDesc < pGMR->cbTotal, VERR_INTERNAL_ERROR); /* overflow protection */
            ++iDesc;
            AssertReturn(iDesc < pGMR->numDescriptors, VERR_INTERNAL_ERROR);
        }

        while (cbCurrentWidth)
        {
            uint32_t cbToCopy;

            if (offGmrCurrent + cbCurrentWidth <= offDesc + paDesc[iDesc].numPages * GUEST_PAGE_SIZE)
                cbToCopy = cbCurrentWidth;
            else
            {
                cbToCopy = (offDesc + paDesc[iDesc].numPages * GUEST_PAGE_SIZE - offGmrCurrent);
                AssertReturn(cbToCopy <= cbCurrentWidth, VERR_INVALID_PARAMETER);
            }

            RTGCPHYS const GCPhys = paDesc[iDesc].GCPhys + offGmrCurrent - offDesc;

            Log5Func(("%s phys=%RGp\n", (enmTransferType == SVGA3D_WRITE_HOST_VRAM) ? "READ" : "WRITE", GCPhys));

            /*
             * We are deliberately using the non-PCI version of PDMDevHlpPCIPhys[Read|Write] as the
             * guest-side VMSVGA driver seems to allocate non-DMA (physical memory) addresses,
             * see @bugref{9654#c75}.
             */
            if (enmTransferType == SVGA3D_WRITE_HOST_VRAM)
                rc = PDMDevHlpPhysRead(pDevIns, GCPhys, pbCurrentHost, cbToCopy);
            else
                rc = PDMDevHlpPhysWrite(pDevIns, GCPhys, pbCurrentHost, cbToCopy);
            AssertRCBreak(rc);

            cbCurrentWidth -= cbToCopy;
            offGmrCurrent  += cbToCopy;
            pbCurrentHost  += cbToCopy;

            /* Go to the next descriptor if there's anything left. */
            if (cbCurrentWidth)
            {
                offDesc += paDesc[iDesc].numPages * GUEST_PAGE_SIZE;
                AssertReturn(offDesc < pGMR->cbTotal, VERR_INTERNAL_ERROR);
                ++iDesc;
                AssertReturn(iDesc < pGMR->numDescriptors, VERR_INTERNAL_ERROR);
            }
        }

        offGmrScanline += cbGstPitch;
        pbHstScanline  += cbHstPitch;
    }

    return VINF_SUCCESS;
}


/**
 * Unsigned coordinates in pBox. Clip to [0; pSizeSrc), [0; pSizeDest).
 *
 * @param   pSizeSrc    Source surface dimensions.
 * @param   pSizeDest   Destination surface dimensions.
 * @param   pBox        Coordinates to be clipped.
 */
void vmsvgaR3ClipCopyBox(const SVGA3dSize *pSizeSrc, const SVGA3dSize *pSizeDest, SVGA3dCopyBox *pBox)
{
    /* Src x, w */
    if (pBox->srcx > pSizeSrc->width)
        pBox->srcx = pSizeSrc->width;
    if (pBox->w > pSizeSrc->width - pBox->srcx)
        pBox->w = pSizeSrc->width - pBox->srcx;

    /* Src y, h */
    if (pBox->srcy > pSizeSrc->height)
        pBox->srcy = pSizeSrc->height;
    if (pBox->h > pSizeSrc->height - pBox->srcy)
        pBox->h = pSizeSrc->height - pBox->srcy;

    /* Src z, d */
    if (pBox->srcz > pSizeSrc->depth)
        pBox->srcz = pSizeSrc->depth;
    if (pBox->d > pSizeSrc->depth - pBox->srcz)
        pBox->d = pSizeSrc->depth - pBox->srcz;

    /* Dest x, w */
    if (pBox->x > pSizeDest->width)
        pBox->x = pSizeDest->width;
    if (pBox->w > pSizeDest->width - pBox->x)
        pBox->w = pSizeDest->width - pBox->x;

    /* Dest y, h */
    if (pBox->y > pSizeDest->height)
        pBox->y = pSizeDest->height;
    if (pBox->h > pSizeDest->height - pBox->y)
        pBox->h = pSizeDest->height - pBox->y;

    /* Dest z, d */
    if (pBox->z > pSizeDest->depth)
        pBox->z = pSizeDest->depth;
    if (pBox->d > pSizeDest->depth - pBox->z)
        pBox->d = pSizeDest->depth - pBox->z;
}


/**
 * Unsigned coordinates in pBox. Clip to [0; pSize).
 *
 * @param   pSize   Source surface dimensions.
 * @param   pBox    Coordinates to be clipped.
 */
void vmsvgaR3ClipBox(const SVGA3dSize *pSize, SVGA3dBox *pBox)
{
    /* x, w */
    if (pBox->x > pSize->width)
        pBox->x = pSize->width;
    if (pBox->w > pSize->width - pBox->x)
        pBox->w = pSize->width - pBox->x;

    /* y, h */
    if (pBox->y > pSize->height)
        pBox->y = pSize->height;
    if (pBox->h > pSize->height - pBox->y)
        pBox->h = pSize->height - pBox->y;

    /* z, d */
    if (pBox->z > pSize->depth)
        pBox->z = pSize->depth;
    if (pBox->d > pSize->depth - pBox->z)
        pBox->d = pSize->depth - pBox->z;
}


/**
 * Clip.
 *
 * @param   pBound  Bounding rectangle.
 * @param   pRect   Rectangle to be clipped.
 */
void vmsvgaR3ClipRect(SVGASignedRect const *pBound, SVGASignedRect *pRect)
{
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;

    /* Right order. */
    Assert(pBound->left <= pBound->right && pBound->top <= pBound->bottom);
    if (pRect->left < pRect->right)
    {
        left = pRect->left;
        right = pRect->right;
    }
    else
    {
        left = pRect->right;
        right = pRect->left;
    }
    if (pRect->top < pRect->bottom)
    {
        top = pRect->top;
        bottom = pRect->bottom;
    }
    else
    {
        top = pRect->bottom;
        bottom = pRect->top;
    }

    if (left < pBound->left)
        left = pBound->left;
    if (right < pBound->left)
        right = pBound->left;

    if (left > pBound->right)
        left = pBound->right;
    if (right > pBound->right)
        right = pBound->right;

    if (top < pBound->top)
        top = pBound->top;
    if (bottom < pBound->top)
        bottom = pBound->top;

    if (top > pBound->bottom)
        top = pBound->bottom;
    if (bottom > pBound->bottom)
        bottom = pBound->bottom;

    pRect->left = left;
    pRect->right = right;
    pRect->top = top;
    pRect->bottom = bottom;
}


/**
 * Clip.
 *
 * @param   pBound  Bounding rectangle.
 * @param   pRect   Rectangle to be clipped.
 */
void vmsvgaR3Clip3dRect(SVGA3dRect const *pBound, SVGA3dRect RT_UNTRUSTED_GUEST *pRect)
{
    uint32_t const leftBound   = pBound->x;
    uint32_t const rightBound  = pBound->x + pBound->w;
    uint32_t const topBound    = pBound->y;
    uint32_t const bottomBound = pBound->y + pBound->h;

    uint32_t x = pRect->x;
    uint32_t y = pRect->y;
    uint32_t w = pRect->w;
    uint32_t h = pRect->h;

    /* Make sure that right and bottom coordinates can be safely computed. */
    if (x > rightBound)
        x = rightBound;
    if (w > rightBound - x)
        w = rightBound - x;
    if (y > bottomBound)
        y = bottomBound;
    if (h > bottomBound - y)
        h = bottomBound - y;

    /* Switch from x, y, w, h to left, top, right, bottom. */
    uint32_t left   = x;
    uint32_t right  = x + w;
    uint32_t top    = y;
    uint32_t bottom = y + h;

    /* A standard left, right, bottom, top clipping. */
    if (left < leftBound)
        left = leftBound;
    if (right < leftBound)
        right = leftBound;

    if (left > rightBound)
        left = rightBound;
    if (right > rightBound)
        right = rightBound;

    if (top < topBound)
        top = topBound;
    if (bottom < topBound)
        bottom = topBound;

    if (top > bottomBound)
        top = bottomBound;
    if (bottom > bottomBound)
        bottom = bottomBound;

    /* Back to x, y, w, h representation. */
    pRect->x = left;
    pRect->y = top;
    pRect->w = right - left;
    pRect->h = bottom - top;
}

