/* $Id: SvgaRender.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Graphics Driver - VMSVGA command verification routines.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

#define GALOG_GROUP GALOG_GROUP_SVGA

#include "Svga.h"
#include "SvgaFifo.h"
#include "SvgaHw.h"
#include "SvgaCmd.h"

#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/memobj.h>

#ifdef DEBUG
#define DEBUG_VERIFYCMD_RETURN() do { \
    if (pSvgaContext->fDebugVerifyCommands) return STATUS_SUCCESS; \
} while (0)
#define DEBUG_VERIFYCMD_ASSERT(cond) do { \
    if (pSvgaContext->fDebugVerifyCommands) \
    { \
        Assert(cond); \
    } \
} while (0)
#else
#define DEBUG_VERIFYCMD_RETURN() do {} while (0)
#define DEBUG_VERIFYCMD_ASSERT(cond) do {} while (0)
#endif

/* SVGA_3D_CMD_SURFACE_DEFINE 1040 */
static NTSTATUS procCmdDefineSurface(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDefineSurface *pCmd = (SVGA3dCmdDefineSurface *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SURFACE_DESTROY 1041 */
static NTSTATUS procCmdDestroySurface(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDestroySurface *pCmd = (SVGA3dCmdDestroySurface *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SURFACE_COPY 1042 */
static NTSTATUS procCmdSurfaceCopy(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSurfaceCopy *pCmd = (SVGA3dCmdSurfaceCopy *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SURFACE_STRETCHBLT 1043 */
static NTSTATUS procCmdSurfaceStretchBlt(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSurfaceStretchBlt *pCmd = (SVGA3dCmdSurfaceStretchBlt *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SURFACE_DMA 1044 */
static NTSTATUS procCmdSurfaceDMA(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSurfaceDMA *pCmd = (SVGA3dCmdSurfaceDMA *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_CONTEXT_DEFINE 1045 */
static NTSTATUS procCmdDefineContext(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDefineContext *pCmd = (SVGA3dCmdDefineContext *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_CONTEXT_DESTROY 1046 */
static NTSTATUS procCmdDestroyContext(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDestroyContext *pCmd = (SVGA3dCmdDestroyContext *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SETTRANSFORM 1047 */
static NTSTATUS procCmdSetTransform(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetTransform *pCmd = (SVGA3dCmdSetTransform *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SETZRANGE 1048 */
static NTSTATUS procCmdSetZRange(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetZRange *pCmd = (SVGA3dCmdSetZRange *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SETRENDERSTATE 1049 */
static NTSTATUS procCmdSetRenderState(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetRenderState *pCmd = (SVGA3dCmdSetRenderState *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SETRENDERTARGET 1050 */
static NTSTATUS procCmdSetRenderTarget(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetRenderTarget *pCmd = (SVGA3dCmdSetRenderTarget *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SETTEXTURESTATE 1051 */
static NTSTATUS procCmdSetTextureState(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetTextureState *pCmd = (SVGA3dCmdSetTextureState *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SETMATERIAL 1052 */
static NTSTATUS procCmdSetMaterial(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetMaterial *pCmd = (SVGA3dCmdSetMaterial *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SETLIGHTDATA 1053 */
static NTSTATUS procCmdSetLightData(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetLightData *pCmd = (SVGA3dCmdSetLightData *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SETLIGHTENABLED 1054 */
static NTSTATUS procCmdSetLightEnabled(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetLightEnabled *pCmd = (SVGA3dCmdSetLightEnabled *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SETVIEWPORT 1055 */
static NTSTATUS procCmdSetViewport(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetViewport *pCmd = (SVGA3dCmdSetViewport *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SETCLIPPLANE 1056 */
static NTSTATUS procCmdSetClipPlane(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetClipPlane *pCmd = (SVGA3dCmdSetClipPlane *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_CLEAR 1057 */
static NTSTATUS procCmdClear(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdClear *pCmd = (SVGA3dCmdClear *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_PRESENT 1058 */
static NTSTATUS procCmdPresent(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdPresent *pCmd = (SVGA3dCmdPresent *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SHADER_DEFINE 1059 */
static NTSTATUS procCmdDefineShader(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDefineShader *pCmd = (SVGA3dCmdDefineShader *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SHADER_DESTROY 1060 */
static NTSTATUS procCmdDestroyShader(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDestroyShader *pCmd = (SVGA3dCmdDestroyShader *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SET_SHADER 1061 */
static NTSTATUS procCmdSetShader(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetShader *pCmd = (SVGA3dCmdSetShader *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SET_SHADER_CONST 1062 */
static NTSTATUS procCmdSetShaderConst(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetShaderConst *pCmd = (SVGA3dCmdSetShaderConst *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DRAW_PRIMITIVES 1063 */
static NTSTATUS procCmdDrawPrimitives(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDrawPrimitives *pCmd = (SVGA3dCmdDrawPrimitives *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SETSCISSORRECT 1064 */
static NTSTATUS procCmdSetScissorRect(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetScissorRect *pCmd = (SVGA3dCmdSetScissorRect *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_BEGIN_QUERY 1065 */
static NTSTATUS procCmdBeginQuery(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdBeginQuery *pCmd = (SVGA3dCmdBeginQuery *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_END_QUERY 1066 */
static NTSTATUS procCmdEndQuery(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdEndQuery *pCmd = (SVGA3dCmdEndQuery *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_WAIT_FOR_QUERY 1067 */
static NTSTATUS procCmdWaitForQuery(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdWaitForQuery *pCmd = (SVGA3dCmdWaitForQuery *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN 1069 */
static NTSTATUS procCmdBlitSurfaceToScreen(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdBlitSurfaceToScreen *pCmd = (SVGA3dCmdBlitSurfaceToScreen *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SURFACE_DEFINE_V2 1070 */
static NTSTATUS procCmdDefineSurface_v2(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDefineSurface_v2 *pCmd = (SVGA3dCmdDefineSurface_v2 *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_GENERATE_MIPMAPS 1071 */
static NTSTATUS procCmdGenerateMipmaps(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdGenerateMipmaps *pCmd = (SVGA3dCmdGenerateMipmaps *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_ACTIVATE_SURFACE 1080 */
static NTSTATUS procCmdActivateSurface(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdActivateSurface *pCmd = (SVGA3dCmdActivateSurface *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DEACTIVATE_SURFACE 1081 */
static NTSTATUS procCmdDeactivateSurface(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDeactivateSurface *pCmd = (SVGA3dCmdDeactivateSurface *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SCREEN_DMA 1082 */
static NTSTATUS procCmdScreenDMA(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdScreenDMA *pCmd = (SVGA3dCmdScreenDMA *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SET_OTABLE_BASE 1091 */
static NTSTATUS procCmdSetOTableBase(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetOTableBase *pCmd = (SVGA3dCmdSetOTableBase *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_READBACK_OTABLE 1092 */
static NTSTATUS procCmdReadbackOTable(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdReadbackOTable *pCmd = (SVGA3dCmdReadbackOTable *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DEFINE_GB_MOB 1093 */
static NTSTATUS procCmdDefineGBMob(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDefineGBMob *pCmd = (SVGA3dCmdDefineGBMob *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DESTROY_GB_MOB 1094 */
static NTSTATUS procCmdDestroyGBMob(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDestroyGBMob *pCmd = (SVGA3dCmdDestroyGBMob *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_UPDATE_GB_MOB_MAPPING 1096 */
static NTSTATUS procCmdUpdateGBMobMapping(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdUpdateGBMobMapping *pCmd = (SVGA3dCmdUpdateGBMobMapping *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DESTROY_GB_SURFACE 1098 */
static NTSTATUS procCmdDestroyGBSurface(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDestroyGBSurface *pCmd = (SVGA3dCmdDestroyGBSurface *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_BIND_GB_SURFACE 1099 */
static NTSTATUS procCmdBindGBSurface(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdBindGBSurface *pCmd = (SVGA3dCmdBindGBSurface *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_COND_BIND_GB_SURFACE 1100 */
static NTSTATUS procCmdCondBindGBSurface(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdCondBindGBSurface *pCmd = (SVGA3dCmdCondBindGBSurface *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_UPDATE_GB_IMAGE 1101 */
static NTSTATUS procCmdUpdateGBImage(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdUpdateGBImage *pCmd = (SVGA3dCmdUpdateGBImage *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_UPDATE_GB_SURFACE 1102 */
static NTSTATUS procCmdUpdateGBSurface(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdUpdateGBSurface *pCmd = (SVGA3dCmdUpdateGBSurface *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_READBACK_GB_IMAGE 1103 */
static NTSTATUS procCmdReadbackGBImage(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdReadbackGBImage *pCmd = (SVGA3dCmdReadbackGBImage *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_READBACK_GB_SURFACE 1104 */
static NTSTATUS procCmdReadbackGBSurface(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdReadbackGBSurface *pCmd = (SVGA3dCmdReadbackGBSurface *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_INVALIDATE_GB_IMAGE 1105 */
static NTSTATUS procCmdInvalidateGBImage(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdInvalidateGBImage *pCmd = (SVGA3dCmdInvalidateGBImage *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_INVALIDATE_GB_SURFACE 1106 */
static NTSTATUS procCmdInvalidateGBSurface(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdInvalidateGBSurface *pCmd = (SVGA3dCmdInvalidateGBSurface *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DEFINE_GB_CONTEXT 1107 */
static NTSTATUS procCmdDefineGBContext(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDefineGBContext *pCmd = (SVGA3dCmdDefineGBContext *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DESTROY_GB_CONTEXT 1108 */
static NTSTATUS procCmdDestroyGBContext(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDestroyGBContext *pCmd = (SVGA3dCmdDestroyGBContext *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_BIND_GB_CONTEXT 1109 */
static NTSTATUS procCmdBindGBContext(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdBindGBContext *pCmd = (SVGA3dCmdBindGBContext *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_READBACK_GB_CONTEXT 1110 */
static NTSTATUS procCmdReadbackGBContext(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdReadbackGBContext *pCmd = (SVGA3dCmdReadbackGBContext *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_INVALIDATE_GB_CONTEXT 1111 */
static NTSTATUS procCmdInvalidateGBContext(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdInvalidateGBContext *pCmd = (SVGA3dCmdInvalidateGBContext *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DEFINE_GB_SHADER 1112 */
static NTSTATUS procCmdDefineGBShader(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDefineGBShader *pCmd = (SVGA3dCmdDefineGBShader *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DESTROY_GB_SHADER 1113 */
static NTSTATUS procCmdDestroyGBShader(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDestroyGBShader *pCmd = (SVGA3dCmdDestroyGBShader *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_BIND_GB_SHADER 1114 */
static NTSTATUS procCmdBindGBShader(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdBindGBShader *pCmd = (SVGA3dCmdBindGBShader *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SET_OTABLE_BASE64 1115 */
static NTSTATUS procCmdSetOTableBase64(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetOTableBase64 *pCmd = (SVGA3dCmdSetOTableBase64 *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_BEGIN_GB_QUERY 1116 */
static NTSTATUS procCmdBeginGBQuery(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdBeginGBQuery *pCmd = (SVGA3dCmdBeginGBQuery *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_END_GB_QUERY 1117 */
static NTSTATUS procCmdEndGBQuery(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdEndGBQuery *pCmd = (SVGA3dCmdEndGBQuery *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_WAIT_FOR_GB_QUERY 1118 */
static NTSTATUS procCmdWaitForGBQuery(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdWaitForGBQuery *pCmd = (SVGA3dCmdWaitForGBQuery *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DEFINE_GB_SCREENTARGET 1124 */
static NTSTATUS procCmdDefineGBScreenTarget(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDefineGBScreenTarget *pCmd = (SVGA3dCmdDefineGBScreenTarget *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DESTROY_GB_SCREENTARGET 1125 */
static NTSTATUS procCmdDestroyGBScreenTarget(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDestroyGBScreenTarget *pCmd = (SVGA3dCmdDestroyGBScreenTarget *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_BIND_GB_SCREENTARGET 1126 */
static NTSTATUS procCmdBindGBScreenTarget(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdBindGBScreenTarget *pCmd = (SVGA3dCmdBindGBScreenTarget *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_UPDATE_GB_SCREENTARGET 1127 */
static NTSTATUS procCmdUpdateGBScreenTarget(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdUpdateGBScreenTarget *pCmd = (SVGA3dCmdUpdateGBScreenTarget *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_READBACK_GB_IMAGE_PARTIAL 1128 */
static NTSTATUS procCmdReadbackGBImagePartial(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdReadbackGBImagePartial *pCmd = (SVGA3dCmdReadbackGBImagePartial *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_INVALIDATE_GB_IMAGE_PARTIAL 1129 */
static NTSTATUS procCmdInvalidateGBImagePartial(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdInvalidateGBImagePartial *pCmd = (SVGA3dCmdInvalidateGBImagePartial *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SET_GB_SHADERCONSTS_INLINE 1130 */
static NTSTATUS procCmdSetGBShaderConstInline(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetGBShaderConstInline *pCmd = (SVGA3dCmdSetGBShaderConstInline *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_GB_SCREEN_DMA 1131 */
static NTSTATUS procCmdGBScreenDMA(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdGBScreenDMA *pCmd = (SVGA3dCmdGBScreenDMA *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_BIND_GB_SURFACE_WITH_PITCH 1132 */
static NTSTATUS procCmdBindGBSurfaceWithPitch(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdBindGBSurfaceWithPitch *pCmd = (SVGA3dCmdBindGBSurfaceWithPitch *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_GB_MOB_FENCE 1133 */
static NTSTATUS procCmdGBMobFence(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdGBMobFence *pCmd = (SVGA3dCmdGBMobFence *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DEFINE_GB_MOB64 1135 */
static NTSTATUS procCmdDefineGBMob64(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDefineGBMob64 *pCmd = (SVGA3dCmdDefineGBMob64 *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_REDEFINE_GB_MOB64 1136 */
static NTSTATUS procCmdRedefineGBMob64(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdRedefineGBMob64 *pCmd = (SVGA3dCmdRedefineGBMob64 *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SET_VERTEX_STREAMS 1138 */
static NTSTATUS procCmdSetVertexStreams(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetVertexStreams *pCmd = (SVGA3dCmdSetVertexStreams *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SET_VERTEX_DECLS 1139 */
static NTSTATUS procCmdSetVertexDecls(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetVertexDecls *pCmd = (SVGA3dCmdSetVertexDecls *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SET_VERTEX_DIVISORS 1140 */
static NTSTATUS procCmdSetVertexDivisors(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSetVertexDivisors *pCmd = (SVGA3dCmdSetVertexDivisors *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DRAW 1141 */
static NTSTATUS procCmdDraw(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDraw *pCmd = (SVGA3dCmdDraw *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DRAW_INDEXED 1142 */
static NTSTATUS procCmdDrawIndexed(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDrawIndexed *pCmd = (SVGA3dCmdDrawIndexed *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_DEFINE_CONTEXT 1143 */
static NTSTATUS procCmdDXDefineContext(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDefineContext *pCmd = (SVGA3dCmdDXDefineContext *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_DESTROY_CONTEXT 1144 */
static NTSTATUS procCmdDXDestroyContext(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDestroyContext *pCmd = (SVGA3dCmdDXDestroyContext *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_BIND_CONTEXT 1145 */
static NTSTATUS procCmdDXBindContext(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXBindContext *pCmd = (SVGA3dCmdDXBindContext *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_READBACK_CONTEXT 1146 */
static NTSTATUS procCmdDXReadbackContext(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXReadbackContext *pCmd = (SVGA3dCmdDXReadbackContext *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_INVALIDATE_CONTEXT 1147 */
static NTSTATUS procCmdDXInvalidateContext(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXInvalidateContext *pCmd = (SVGA3dCmdDXInvalidateContext *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_SINGLE_CONSTANT_BUFFER 1148 */
static NTSTATUS procCmdDXSetSingleConstantBuffer(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetSingleConstantBuffer *pCmd = (SVGA3dCmdDXSetSingleConstantBuffer *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_SHADER_RESOURCES 1149 */
static NTSTATUS procCmdDXSetShaderResources(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetShaderResources *pCmd = (SVGA3dCmdDXSetShaderResources *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_SHADER 1150 */
static NTSTATUS procCmdDXSetShader(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetShader *pCmd = (SVGA3dCmdDXSetShader *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_SAMPLERS 1151 */
static NTSTATUS procCmdDXSetSamplers(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetSamplers *pCmd = (SVGA3dCmdDXSetSamplers *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_DRAW 1152 */
static NTSTATUS procCmdDXDraw(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDraw *pCmd = (SVGA3dCmdDXDraw *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_DRAW_INDEXED 1153 */
static NTSTATUS procCmdDXDrawIndexed(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDrawIndexed *pCmd = (SVGA3dCmdDXDrawIndexed *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_DRAW_INSTANCED 1154 */
static NTSTATUS procCmdDXDrawInstanced(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDrawInstanced *pCmd = (SVGA3dCmdDXDrawInstanced *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_DRAW_INDEXED_INSTANCED 1155 */
static NTSTATUS procCmdDXDrawIndexedInstanced(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDrawIndexedInstanced *pCmd = (SVGA3dCmdDXDrawIndexedInstanced *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_DRAW_AUTO 1156 */
static NTSTATUS procCmdDXDrawAuto(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDrawAuto *pCmd = (SVGA3dCmdDXDrawAuto *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_INPUT_LAYOUT 1157 */
static NTSTATUS procCmdDXSetInputLayout(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetInputLayout *pCmd = (SVGA3dCmdDXSetInputLayout *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_VERTEX_BUFFERS 1158 */
static NTSTATUS procCmdDXSetVertexBuffers(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetVertexBuffers *pCmd = (SVGA3dCmdDXSetVertexBuffers *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_INDEX_BUFFER 1159 */
static NTSTATUS procCmdDXSetIndexBuffer(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetIndexBuffer *pCmd = (SVGA3dCmdDXSetIndexBuffer *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_TOPOLOGY 1160 */
static NTSTATUS procCmdDXSetTopology(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetTopology *pCmd = (SVGA3dCmdDXSetTopology *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_RENDERTARGETS 1161 */
static NTSTATUS procCmdDXSetRenderTargets(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetRenderTargets *pCmd = (SVGA3dCmdDXSetRenderTargets *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_BLEND_STATE 1162 */
static NTSTATUS procCmdDXSetBlendState(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetBlendState *pCmd = (SVGA3dCmdDXSetBlendState *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_DEPTHSTENCIL_STATE 1163 */
static NTSTATUS procCmdDXSetDepthStencilState(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetDepthStencilState *pCmd = (SVGA3dCmdDXSetDepthStencilState *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_RASTERIZER_STATE 1164 */
static NTSTATUS procCmdDXSetRasterizerState(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetRasterizerState *pCmd = (SVGA3dCmdDXSetRasterizerState *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_DEFINE_QUERY 1165 */
static NTSTATUS procCmdDXDefineQuery(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDefineQuery *pCmd = (SVGA3dCmdDXDefineQuery *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_DXQUERY, pCmd->queryId);
}


/* SVGA_3D_CMD_DX_DESTROY_QUERY 1166 */
static NTSTATUS procCmdDXDestroyQuery(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDestroyQuery *pCmd = (SVGA3dCmdDXDestroyQuery *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_DXQUERY, pCmd->queryId);
}


/* SVGA_3D_CMD_DX_BIND_QUERY 1167 */
static NTSTATUS procCmdDXBindQuery(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXBindQuery *pCmd = (SVGA3dCmdDXBindQuery *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_QUERY_OFFSET 1168 */
static NTSTATUS procCmdDXSetQueryOffset(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetQueryOffset *pCmd = (SVGA3dCmdDXSetQueryOffset *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_BEGIN_QUERY 1169 */
static NTSTATUS procCmdDXBeginQuery(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXBeginQuery *pCmd = (SVGA3dCmdDXBeginQuery *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_END_QUERY 1170 */
static NTSTATUS procCmdDXEndQuery(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXEndQuery *pCmd = (SVGA3dCmdDXEndQuery *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_READBACK_QUERY 1171 */
static NTSTATUS procCmdDXReadbackQuery(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXReadbackQuery *pCmd = (SVGA3dCmdDXReadbackQuery *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_PREDICATION 1172 */
static NTSTATUS procCmdDXSetPredication(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetPredication *pCmd = (SVGA3dCmdDXSetPredication *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_SOTARGETS 1173 */
static NTSTATUS procCmdDXSetSOTargets(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetSOTargets *pCmd = (SVGA3dCmdDXSetSOTargets *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_VIEWPORTS 1174 */
static NTSTATUS procCmdDXSetViewports(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetViewports *pCmd = (SVGA3dCmdDXSetViewports *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_SCISSORRECTS 1175 */
static NTSTATUS procCmdDXSetScissorRects(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetScissorRects *pCmd = (SVGA3dCmdDXSetScissorRects *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_CLEAR_RENDERTARGET_VIEW 1176 */
static NTSTATUS procCmdDXClearRenderTargetView(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXClearRenderTargetView *pCmd = (SVGA3dCmdDXClearRenderTargetView *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_CLEAR_DEPTHSTENCIL_VIEW 1177 */
static NTSTATUS procCmdDXClearDepthStencilView(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXClearDepthStencilView *pCmd = (SVGA3dCmdDXClearDepthStencilView *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_PRED_COPY_REGION 1178 */
static NTSTATUS procCmdDXPredCopyRegion(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXPredCopyRegion *pCmd = (SVGA3dCmdDXPredCopyRegion *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_PRED_COPY 1179 */
static NTSTATUS procCmdDXPredCopy(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXPredCopy *pCmd = (SVGA3dCmdDXPredCopy *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_PRESENTBLT 1180 */
static NTSTATUS procCmdDXPresentBlt(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXPresentBlt *pCmd = (SVGA3dCmdDXPresentBlt *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_GENMIPS 1181 */
static NTSTATUS procCmdDXGenMips(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXGenMips *pCmd = (SVGA3dCmdDXGenMips *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_UPDATE_SUBRESOURCE 1182 */
static NTSTATUS procCmdDXUpdateSubResource(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXUpdateSubResource *pCmd = (SVGA3dCmdDXUpdateSubResource *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_READBACK_SUBRESOURCE 1183 */
static NTSTATUS procCmdDXReadbackSubResource(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXReadbackSubResource *pCmd = (SVGA3dCmdDXReadbackSubResource *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_INVALIDATE_SUBRESOURCE 1184 */
static NTSTATUS procCmdDXInvalidateSubResource(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXInvalidateSubResource *pCmd = (SVGA3dCmdDXInvalidateSubResource *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_DEFINE_SHADERRESOURCE_VIEW 1185 */
static NTSTATUS procCmdDXDefineShaderResourceView(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDefineShaderResourceView *pCmd = (SVGA3dCmdDXDefineShaderResourceView *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_SRVIEW, pCmd->shaderResourceViewId);
}


/* SVGA_3D_CMD_DX_DESTROY_SHADERRESOURCE_VIEW 1186 */
static NTSTATUS procCmdDXDestroyShaderResourceView(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDestroyShaderResourceView *pCmd = (SVGA3dCmdDXDestroyShaderResourceView *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_SRVIEW, pCmd->shaderResourceViewId);
}


/* SVGA_3D_CMD_DX_DEFINE_RENDERTARGET_VIEW 1187 */
static NTSTATUS procCmdDXDefineRenderTargetView(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDefineRenderTargetView *pCmd = (SVGA3dCmdDXDefineRenderTargetView *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_RTVIEW, pCmd->renderTargetViewId);
}


/* SVGA_3D_CMD_DX_DESTROY_RENDERTARGET_VIEW 1188 */
static NTSTATUS procCmdDXDestroyRenderTargetView(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDestroyRenderTargetView *pCmd = (SVGA3dCmdDXDestroyRenderTargetView *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_RTVIEW, pCmd->renderTargetViewId);
}


/* SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_VIEW 1189 */
static NTSTATUS procCmdDXDefineDepthStencilView(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDefineDepthStencilView *pCmd = (SVGA3dCmdDXDefineDepthStencilView *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_DSVIEW, pCmd->depthStencilViewId);
}


/* SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_VIEW 1190 */
static NTSTATUS procCmdDXDestroyDepthStencilView(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDestroyDepthStencilView *pCmd = (SVGA3dCmdDXDestroyDepthStencilView *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_DSVIEW, pCmd->depthStencilViewId);
}


/* SVGA_3D_CMD_DX_DEFINE_ELEMENTLAYOUT 1191 */
static NTSTATUS procCmdDXDefineElementLayout(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDefineElementLayout *pCmd = (SVGA3dCmdDXDefineElementLayout *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_ELEMENTLAYOUT, pCmd->elementLayoutId);
}


/* SVGA_3D_CMD_DX_DESTROY_ELEMENTLAYOUT 1192 */
static NTSTATUS procCmdDXDestroyElementLayout(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDestroyElementLayout *pCmd = (SVGA3dCmdDXDestroyElementLayout *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_ELEMENTLAYOUT, pCmd->elementLayoutId);
}


/* SVGA_3D_CMD_DX_DEFINE_BLEND_STATE 1193 */
static NTSTATUS procCmdDXDefineBlendState(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDefineBlendState *pCmd = (SVGA3dCmdDXDefineBlendState *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_BLENDSTATE, pCmd->blendId);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_DESTROY_BLEND_STATE 1194 */
static NTSTATUS procCmdDXDestroyBlendState(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDestroyBlendState *pCmd = (SVGA3dCmdDXDestroyBlendState *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_BLENDSTATE, pCmd->blendId);
}


/* SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_STATE 1195 */
static NTSTATUS procCmdDXDefineDepthStencilState(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDefineDepthStencilState *pCmd = (SVGA3dCmdDXDefineDepthStencilState *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_DEPTHSTENCIL, pCmd->depthStencilId);
}


/* SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_STATE 1196 */
static NTSTATUS procCmdDXDestroyDepthStencilState(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDestroyDepthStencilState *pCmd = (SVGA3dCmdDXDestroyDepthStencilState *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_DEPTHSTENCIL, pCmd->depthStencilId);
}


/* SVGA_3D_CMD_DX_DEFINE_RASTERIZER_STATE 1197 */
static NTSTATUS procCmdDXDefineRasterizerState(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDefineRasterizerState *pCmd = (SVGA3dCmdDXDefineRasterizerState *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_RASTERIZERSTATE, pCmd->rasterizerId);
}


/* SVGA_3D_CMD_DX_DEFINE_RASTERIZER_STATE_V2 1288 */
static NTSTATUS procCmdDXDefineRasterizerState_v2(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDefineRasterizerState_v2 *pCmd = (SVGA3dCmdDXDefineRasterizerState_v2 *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_RASTERIZERSTATE, pCmd->rasterizerId);
}


/* SVGA_3D_CMD_DX_DESTROY_RASTERIZER_STATE 1198 */
static NTSTATUS procCmdDXDestroyRasterizerState(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDestroyRasterizerState *pCmd = (SVGA3dCmdDXDestroyRasterizerState *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_RASTERIZERSTATE, pCmd->rasterizerId);
}


/* SVGA_3D_CMD_DX_DEFINE_SAMPLER_STATE 1199 */
static NTSTATUS procCmdDXDefineSamplerState(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDefineSamplerState *pCmd = (SVGA3dCmdDXDefineSamplerState *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_SAMPLER, pCmd->samplerId);
}


/* SVGA_3D_CMD_DX_DESTROY_SAMPLER_STATE 1200 */
static NTSTATUS procCmdDXDestroySamplerState(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDestroySamplerState *pCmd = (SVGA3dCmdDXDestroySamplerState *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_SAMPLER, pCmd->samplerId);
}


/* SVGA_3D_CMD_DX_DEFINE_SHADER 1201 */
static NTSTATUS procCmdDXDefineShader(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDefineShader *pCmd = (SVGA3dCmdDXDefineShader *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_DXSHADER, pCmd->shaderId);
}


/* SVGA_3D_CMD_DX_DESTROY_SHADER 1202 */
static NTSTATUS procCmdDXDestroyShader(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDestroyShader *pCmd = (SVGA3dCmdDXDestroyShader *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_DXSHADER, pCmd->shaderId);
}


/* SVGA_3D_CMD_DX_BIND_SHADER 1203 */
static NTSTATUS procCmdDXBindShader(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXBindShader *pCmd = (SVGA3dCmdDXBindShader *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    pCmd->cid = pSvgaContext->u32Cid; /** @todo Or patch location? */
    RT_NOREF(pSvga);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_DEFINE_STREAMOUTPUT 1204 */
static NTSTATUS procCmdDXDefineStreamOutput(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDefineStreamOutput *pCmd = (SVGA3dCmdDXDefineStreamOutput *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_STREAMOUTPUT, pCmd->soid);
}


/* SVGA_3D_CMD_DX_DESTROY_STREAMOUTPUT 1205 */
static NTSTATUS procCmdDXDestroyStreamOutput(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDestroyStreamOutput *pCmd = (SVGA3dCmdDXDestroyStreamOutput *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_STREAMOUTPUT, pCmd->soid);
}


/* SVGA_3D_CMD_DX_SET_STREAMOUTPUT 1206 */
static NTSTATUS procCmdDXSetStreamOutput(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetStreamOutput *pCmd = (SVGA3dCmdDXSetStreamOutput *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_COTABLE 1207 */
static NTSTATUS procCmdDXSetCOTable(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetCOTable *pCmd = (SVGA3dCmdDXSetCOTable *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_READBACK_COTABLE 1208 */
static NTSTATUS procCmdDXReadbackCOTable(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXReadbackCOTable *pCmd = (SVGA3dCmdDXReadbackCOTable *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_BUFFER_COPY 1209 */
static NTSTATUS procCmdDXBufferCopy(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXBufferCopy *pCmd = (SVGA3dCmdDXBufferCopy *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_TRANSFER_FROM_BUFFER 1210 */
static NTSTATUS procCmdDXTransferFromBuffer(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXTransferFromBuffer *pCmd = (SVGA3dCmdDXTransferFromBuffer *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SURFACE_COPY_AND_READBACK 1211 */
static NTSTATUS procCmdDXSurfaceCopyAndReadback(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSurfaceCopyAndReadback *pCmd = (SVGA3dCmdDXSurfaceCopyAndReadback *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_MOVE_QUERY 1212 */
static NTSTATUS procCmdDXMoveQuery(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXMoveQuery *pCmd = (SVGA3dCmdDXMoveQuery *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_BIND_ALL_QUERY 1213 */
static NTSTATUS procCmdDXBindAllQuery(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXBindAllQuery *pCmd = (SVGA3dCmdDXBindAllQuery *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_READBACK_ALL_QUERY 1214 */
static NTSTATUS procCmdDXReadbackAllQuery(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXReadbackAllQuery *pCmd = (SVGA3dCmdDXReadbackAllQuery *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_PRED_TRANSFER_FROM_BUFFER 1215 */
static NTSTATUS procCmdDXPredTransferFromBuffer(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXPredTransferFromBuffer *pCmd = (SVGA3dCmdDXPredTransferFromBuffer *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_MOB_FENCE_64 1216 */
static NTSTATUS procCmdDXMobFence64(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXMobFence64 *pCmd = (SVGA3dCmdDXMobFence64 *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_BIND_ALL_SHADER 1217 */
static NTSTATUS procCmdDXBindAllShader(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXBindAllShader *pCmd = (SVGA3dCmdDXBindAllShader *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_HINT 1218 */
static NTSTATUS procCmdDXHint(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXHint *pCmd = (SVGA3dCmdDXHint *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_BUFFER_UPDATE 1219 */
static NTSTATUS procCmdDXBufferUpdate(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXBufferUpdate *pCmd = (SVGA3dCmdDXBufferUpdate *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_VS_CONSTANT_BUFFER_OFFSET 1220 */
static NTSTATUS procCmdDXSetVSConstantBufferOffset(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetVSConstantBufferOffset *pCmd = (SVGA3dCmdDXSetVSConstantBufferOffset *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_PS_CONSTANT_BUFFER_OFFSET 1221 */
static NTSTATUS procCmdDXSetPSConstantBufferOffset(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetPSConstantBufferOffset *pCmd = (SVGA3dCmdDXSetPSConstantBufferOffset *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_GS_CONSTANT_BUFFER_OFFSET 1222 */
static NTSTATUS procCmdDXSetGSConstantBufferOffset(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetGSConstantBufferOffset *pCmd = (SVGA3dCmdDXSetGSConstantBufferOffset *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_HS_CONSTANT_BUFFER_OFFSET 1223 */
static NTSTATUS procCmdDXSetHSConstantBufferOffset(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetHSConstantBufferOffset *pCmd = (SVGA3dCmdDXSetHSConstantBufferOffset *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_DS_CONSTANT_BUFFER_OFFSET 1224 */
static NTSTATUS procCmdDXSetDSConstantBufferOffset(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetDSConstantBufferOffset *pCmd = (SVGA3dCmdDXSetDSConstantBufferOffset *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_CS_CONSTANT_BUFFER_OFFSET 1225 */
static NTSTATUS procCmdDXSetCSConstantBufferOffset(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetCSConstantBufferOffset *pCmd = (SVGA3dCmdDXSetCSConstantBufferOffset *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_COND_BIND_ALL_SHADER 1226 */
static NTSTATUS procCmdDXCondBindAllShader(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXCondBindAllShader *pCmd = (SVGA3dCmdDXCondBindAllShader *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SCREEN_COPY 1227 */
static NTSTATUS procCmdScreenCopy(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdScreenCopy *pCmd = (SVGA3dCmdScreenCopy *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_GROW_OTABLE 1236 */
static NTSTATUS procCmdGrowOTable(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdGrowOTable *pCmd = (SVGA3dCmdGrowOTable *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_GROW_COTABLE 1237 */
static NTSTATUS procCmdDXGrowCOTable(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXGrowCOTable *pCmd = (SVGA3dCmdDXGrowCOTable *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_INTRA_SURFACE_COPY 1238 */
static NTSTATUS procCmdIntraSurfaceCopy(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdIntraSurfaceCopy *pCmd = (SVGA3dCmdIntraSurfaceCopy *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_RESOLVE_COPY 1240 */
static NTSTATUS procCmdDXResolveCopy(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXResolveCopy *pCmd = (SVGA3dCmdDXResolveCopy *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_PRED_RESOLVE_COPY 1241 */
static NTSTATUS procCmdDXPredResolveCopy(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXPredResolveCopy *pCmd = (SVGA3dCmdDXPredResolveCopy *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_PRED_CONVERT_REGION 1242 */
static NTSTATUS procCmdDXPredConvertRegion(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXPredConvertRegion *pCmd = (SVGA3dCmdDXPredConvertRegion *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_PRED_CONVERT 1243 */
static NTSTATUS procCmdDXPredConvert(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXPredConvert *pCmd = (SVGA3dCmdDXPredConvert *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_WHOLE_SURFACE_COPY 1244 */
static NTSTATUS procCmdWholeSurfaceCopy(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdWholeSurfaceCopy *pCmd = (SVGA3dCmdWholeSurfaceCopy *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_DEFINE_UA_VIEW 1245 */
static NTSTATUS procCmdDXDefineUAView(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDefineUAView *pCmd = (SVGA3dCmdDXDefineUAView *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_UAVIEW, pCmd->uaViewId);
}


/* SVGA_3D_CMD_DX_DESTROY_UA_VIEW 1246 */
static NTSTATUS procCmdDXDestroyUAView(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDestroyUAView *pCmd = (SVGA3dCmdDXDestroyUAView *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_UAVIEW, pCmd->uaViewId);
}


/* SVGA_3D_CMD_DX_CLEAR_UA_VIEW_UINT 1247 */
static NTSTATUS procCmdDXClearUAViewUint(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXClearUAViewUint *pCmd = (SVGA3dCmdDXClearUAViewUint *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_CLEAR_UA_VIEW_FLOAT 1248 */
static NTSTATUS procCmdDXClearUAViewFloat(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXClearUAViewFloat *pCmd = (SVGA3dCmdDXClearUAViewFloat *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_COPY_STRUCTURE_COUNT 1249 */
static NTSTATUS procCmdDXCopyStructureCount(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXCopyStructureCount *pCmd = (SVGA3dCmdDXCopyStructureCount *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_UA_VIEWS 1250 */
static NTSTATUS procCmdDXSetUAViews(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetUAViews *pCmd = (SVGA3dCmdDXSetUAViews *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_DRAW_INDEXED_INSTANCED_INDIRECT 1251 */
static NTSTATUS procCmdDXDrawIndexedInstancedIndirect(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDrawIndexedInstancedIndirect *pCmd = (SVGA3dCmdDXDrawIndexedInstancedIndirect *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_DRAW_INSTANCED_INDIRECT 1252 */
static NTSTATUS procCmdDXDrawInstancedIndirect(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDrawInstancedIndirect *pCmd = (SVGA3dCmdDXDrawInstancedIndirect *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_DISPATCH 1253 */
static NTSTATUS procCmdDXDispatch(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDispatch *pCmd = (SVGA3dCmdDXDispatch *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_DISPATCH_INDIRECT 1254 */
static NTSTATUS procCmdDXDispatchIndirect(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDispatchIndirect *pCmd = (SVGA3dCmdDXDispatchIndirect *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_WRITE_ZERO_SURFACE 1255 */
static NTSTATUS procCmdWriteZeroSurface(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdWriteZeroSurface *pCmd = (SVGA3dCmdWriteZeroSurface *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_HINT_ZERO_SURFACE 1256 */
static NTSTATUS procCmdHintZeroSurface(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdHintZeroSurface *pCmd = (SVGA3dCmdHintZeroSurface *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_TRANSFER_TO_BUFFER 1257 */
static NTSTATUS procCmdDXTransferToBuffer(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXTransferToBuffer *pCmd = (SVGA3dCmdDXTransferToBuffer *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_STRUCTURE_COUNT 1258 */
static NTSTATUS procCmdDXSetStructureCount(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetStructureCount *pCmd = (SVGA3dCmdDXSetStructureCount *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_LOGICOPS_BITBLT 1259 */
static NTSTATUS procCmdLogicOpsBitBlt(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdLogicOpsBitBlt *pCmd = (SVGA3dCmdLogicOpsBitBlt *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_LOGICOPS_TRANSBLT 1260 */
static NTSTATUS procCmdLogicOpsTransBlt(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdLogicOpsTransBlt *pCmd = (SVGA3dCmdLogicOpsTransBlt *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_LOGICOPS_STRETCHBLT 1261 */
static NTSTATUS procCmdLogicOpsStretchBlt(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdLogicOpsStretchBlt *pCmd = (SVGA3dCmdLogicOpsStretchBlt *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_LOGICOPS_COLORFILL 1262 */
static NTSTATUS procCmdLogicOpsColorFill(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdLogicOpsColorFill *pCmd = (SVGA3dCmdLogicOpsColorFill *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_LOGICOPS_ALPHABLEND 1263 */
static NTSTATUS procCmdLogicOpsAlphaBlend(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdLogicOpsAlphaBlend *pCmd = (SVGA3dCmdLogicOpsAlphaBlend *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_LOGICOPS_CLEARTYPEBLEND 1264 */
static NTSTATUS procCmdLogicOpsClearTypeBlend(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdLogicOpsClearTypeBlend *pCmd = (SVGA3dCmdLogicOpsClearTypeBlend *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_CS_UA_VIEWS 1268 */
static NTSTATUS procCmdDXSetCSUAViews(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetCSUAViews *pCmd = (SVGA3dCmdDXSetCSUAViews *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_SET_MIN_LOD 1269 */
static NTSTATUS procCmdDXSetMinLOD(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetMinLOD *pCmd = (SVGA3dCmdDXSetMinLOD *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_VIEW_V2 1272 */
static NTSTATUS procCmdDXDefineDepthStencilView_v2(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDefineDepthStencilView_v2 *pCmd = (SVGA3dCmdDXDefineDepthStencilView_v2 *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_DSVIEW, pCmd->depthStencilViewId);
}


/* SVGA_3D_CMD_DX_DEFINE_STREAMOUTPUT_WITH_MOB 1273 */
static NTSTATUS procCmdDXDefineStreamOutputWithMob(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXDefineStreamOutputWithMob *pCmd = (SVGA3dCmdDXDefineStreamOutputWithMob *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, SVGA_COTABLE_STREAMOUTPUT, pCmd->soid);
}


/* SVGA_3D_CMD_DX_SET_SHADER_IFACE 1274 */
static NTSTATUS procCmdDXSetShaderIface(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXSetShaderIface *pCmd = (SVGA3dCmdDXSetShaderIface *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_BIND_STREAMOUTPUT 1275 */
static NTSTATUS procCmdDXBindStreamOutput(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXBindStreamOutput *pCmd = (SVGA3dCmdDXBindStreamOutput *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_SURFACE_STRETCHBLT_NON_MS_TO_MS 1276 */
static NTSTATUS procCmdSurfaceStretchBltNonMSToMS(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdSurfaceStretchBltNonMSToMS *pCmd = (SVGA3dCmdSurfaceStretchBltNonMSToMS *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_DX_BIND_SHADER_IFACE 1277 */
static NTSTATUS procCmdDXBindShaderIface(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXBindShaderIface *pCmd = (SVGA3dCmdDXBindShaderIface *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* SVGA_3D_CMD_VB_DX_CLEAR_RENDERTARGET_VIEW_REGION 1083 */
static NTSTATUS procCmdVBDXClearRenderTargetViewRegion(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    SVGA3dCmdDXClearRenderTargetView *pCmd = (SVGA3dCmdDXClearRenderTargetView *)&pHeader[1];
    RT_NOREF(pSvga, pSvgaContext, pCmd);
    return STATUS_SUCCESS;
}


/* VBSVGA_3D_CMD_DX_DEFINE_VIDEO_PROCESSOR  VBSVGA_3D_CMD_DX_BASE + 0 */
static NTSTATUS procVBCmdDXDefineVideoProcessor(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    VBSVGA3dCmdDXDefineVideoProcessor *pCmd = (VBSVGA3dCmdDXDefineVideoProcessor *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, VBSVGA_COTABLE_VIDEOPROCESSOR, pCmd->videoProcessorId);
}


/* VBSVGA_3D_CMD_DX_DEFINE_VIDEO_DECODER_OUTPUT_VIEW  VBSVGA_3D_CMD_DX_BASE + 1 */
static NTSTATUS procVBCmdDXDefineVideoDecoderOutputView(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    VBSVGA3dCmdDXDefineVideoDecoderOutputView *pCmd = (VBSVGA3dCmdDXDefineVideoDecoderOutputView *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, VBSVGA_COTABLE_VDOV, pCmd->videoDecoderOutputViewId);
}


/* VBSVGA_3D_CMD_DX_DEFINE_VIDEO_DECODER  VBSVGA_3D_CMD_DX_BASE + 2 */
static NTSTATUS procVBCmdDXDefineVideoDecoder(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    VBSVGA3dCmdDXDefineVideoDecoder *pCmd = (VBSVGA3dCmdDXDefineVideoDecoder *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, VBSVGA_COTABLE_VIDEODECODER, pCmd->videoDecoderId);
}


/* VBSVGA_3D_CMD_DX_DEFINE_VIDEO_PROCESSOR_INPUT_VIEW  VBSVGA_3D_CMD_DX_BASE + 6 */
static NTSTATUS procVBCmdDXDefineVideoProcessorInputView(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    VBSVGA3dCmdDXDefineVideoProcessorInputView *pCmd = (VBSVGA3dCmdDXDefineVideoProcessorInputView *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, VBSVGA_COTABLE_VPIV, pCmd->videoProcessorInputViewId);
}


/* VBSVGA_3D_CMD_DX_DEFINE_VIDEO_PROCESSOR_OUTPUT_VIEW  VBSVGA_3D_CMD_DX_BASE + 7 */
static NTSTATUS procVBCmdDXDefineVideoProcessorOutputView(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    VBSVGA3dCmdDXDefineVideoProcessorOutputView *pCmd = (VBSVGA3dCmdDXDefineVideoProcessorOutputView *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, VBSVGA_COTABLE_VPOV, pCmd->videoProcessorOutputViewId);
}


/* VBSVGA_3D_CMD_DX_DESTROY_VIDEO_DECODER  VBSVGA_3D_CMD_DX_BASE + 9 */
static NTSTATUS procVBCmdDXDestroyVideoDecoder(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    VBSVGA3dCmdDXDestroyVideoDecoder *pCmd = (VBSVGA3dCmdDXDestroyVideoDecoder *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, VBSVGA_COTABLE_VIDEODECODER, pCmd->videoDecoderId);
}


/* VBSVGA_3D_CMD_DX_DESTROY_VIDEO_DECODER_OUTPUT_VIEW  VBSVGA_3D_CMD_DX_BASE + 10 */
static NTSTATUS procVBCmdDXDestroyVideoDecoderOutputView(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    VBSVGA3dCmdDXDestroyVideoDecoderOutputView *pCmd = (VBSVGA3dCmdDXDestroyVideoDecoderOutputView *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, VBSVGA_COTABLE_VDOV, pCmd->videoDecoderOutputViewId);
}


/* VBSVGA_3D_CMD_DX_DESTROY_VIDEO_PROCESSOR  VBSVGA_3D_CMD_DX_BASE + 11 */
static NTSTATUS procVBCmdDXDestroyVideoProcessor(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    VBSVGA3dCmdDXDestroyVideoProcessor *pCmd = (VBSVGA3dCmdDXDestroyVideoProcessor *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, VBSVGA_COTABLE_VIDEOPROCESSOR, pCmd->videoProcessorId);
}


/* VBSVGA_3D_CMD_DX_DESTROY_VIDEO_PROCESSOR_INPUT_VIEW  VBSVGA_3D_CMD_DX_BASE + 12 */
static NTSTATUS procVBCmdDXDestroyVideoProcessorInputView(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    VBSVGA3dCmdDXDestroyVideoProcessorInputView *pCmd = (VBSVGA3dCmdDXDestroyVideoProcessorInputView *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, VBSVGA_COTABLE_VPIV, pCmd->videoProcessorInputViewId);
}


/* VBSVGA_3D_CMD_DX_DESTROY_VIDEO_PROCESSOR_OUTPUT_VIEW  VBSVGA_3D_CMD_DX_BASE + 12 */
static NTSTATUS procVBCmdDXDestroyVideoProcessorOutputView(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    VBSVGA3dCmdDXDestroyVideoProcessorOutputView *pCmd = (VBSVGA3dCmdDXDestroyVideoProcessorOutputView *)&pHeader[1];
    DEBUG_VERIFYCMD_RETURN();
    return SvgaCOTNotifyId(pSvga, pSvgaContext, VBSVGA_COTABLE_VPOV, pCmd->videoProcessorOutputViewId);
}


/* Command ok. */
static NTSTATUS procCmdOK(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    RT_NOREF(pSvga, pSvgaContext, pHeader);
    return STATUS_SUCCESS;
}

/* Command invalid. */
static NTSTATUS procCmdInvalid(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader)
{
    RT_NOREF(pSvga, pSvgaContext, pHeader);
    return STATUS_ILLEGAL_INSTRUCTION;
}

typedef NTSTATUS FNPROCESSCOMMAND(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACONTEXT pSvgaContext, SVGA3dCmdHeader *pHeader);
typedef FNPROCESSCOMMAND *PFNPROCESSCOMMAND;

typedef struct SVGA3DCOMMANDDESC
{
    PFNPROCESSCOMMAND pfnProcessCommand;
} SVGA3DCOMMANDDESC;

static SVGA3DCOMMANDDESC const s_aCommandDesc[] =
{
    { procCmdDefineSurface },                       // SVGA_3D_CMD_SURFACE_DEFINE
    { procCmdDestroySurface },                      // SVGA_3D_CMD_SURFACE_DESTROY
    { procCmdSurfaceCopy },                         // SVGA_3D_CMD_SURFACE_COPY
    { procCmdSurfaceStretchBlt },                   // SVGA_3D_CMD_SURFACE_STRETCHBLT
    { procCmdSurfaceDMA },                          // SVGA_3D_CMD_SURFACE_DMA
    { procCmdDefineContext },                       // SVGA_3D_CMD_CONTEXT_DEFINE
    { procCmdDestroyContext },                      // SVGA_3D_CMD_CONTEXT_DESTROY
    { procCmdSetTransform },                        // SVGA_3D_CMD_SETTRANSFORM
    { procCmdSetZRange },                           // SVGA_3D_CMD_SETZRANGE
    { procCmdSetRenderState },                      // SVGA_3D_CMD_SETRENDERSTATE
    { procCmdSetRenderTarget },                     // SVGA_3D_CMD_SETRENDERTARGET
    { procCmdSetTextureState },                     // SVGA_3D_CMD_SETTEXTURESTATE
    { procCmdSetMaterial },                         // SVGA_3D_CMD_SETMATERIAL
    { procCmdSetLightData },                        // SVGA_3D_CMD_SETLIGHTDATA
    { procCmdSetLightEnabled },                     // SVGA_3D_CMD_SETLIGHTENABLED
    { procCmdSetViewport },                         // SVGA_3D_CMD_SETVIEWPORT
    { procCmdSetClipPlane },                        // SVGA_3D_CMD_SETCLIPPLANE
    { procCmdClear },                               // SVGA_3D_CMD_CLEAR
    { procCmdPresent },                             // SVGA_3D_CMD_PRESENT
    { procCmdDefineShader },                        // SVGA_3D_CMD_SHADER_DEFINE
    { procCmdDestroyShader },                       // SVGA_3D_CMD_SHADER_DESTROY
    { procCmdSetShader },                           // SVGA_3D_CMD_SET_SHADER
    { procCmdSetShaderConst },                      // SVGA_3D_CMD_SET_SHADER_CONST
    { procCmdDrawPrimitives },                      // SVGA_3D_CMD_DRAW_PRIMITIVES
    { procCmdSetScissorRect },                      // SVGA_3D_CMD_SETSCISSORRECT
    { procCmdBeginQuery },                          // SVGA_3D_CMD_BEGIN_QUERY
    { procCmdEndQuery },                            // SVGA_3D_CMD_END_QUERY
    { procCmdWaitForQuery },                        // SVGA_3D_CMD_WAIT_FOR_QUERY
    { procCmdInvalid },                             // SVGA_3D_CMD_PRESENT_READBACK
    { procCmdBlitSurfaceToScreen },                 // SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN
    { procCmdDefineSurface_v2 },                    // SVGA_3D_CMD_SURFACE_DEFINE_V2
    { procCmdGenerateMipmaps },                     // SVGA_3D_CMD_GENERATE_MIPMAPS
    { procCmdInvalid },                             // SVGA_3D_CMD_DEAD4
    { procCmdInvalid },                             // SVGA_3D_CMD_DEAD5
    { procCmdInvalid },                             // SVGA_3D_CMD_DEAD6
    { procCmdInvalid },                             // SVGA_3D_CMD_DEAD7
    { procCmdInvalid },                             // SVGA_3D_CMD_DEAD8
    { procCmdInvalid },                             // SVGA_3D_CMD_DEAD9
    { procCmdInvalid },                             // SVGA_3D_CMD_DEAD10
    { procCmdInvalid },                             // SVGA_3D_CMD_DEAD11
    { procCmdActivateSurface },                     // SVGA_3D_CMD_ACTIVATE_SURFACE
    { procCmdDeactivateSurface },                   // SVGA_3D_CMD_DEACTIVATE_SURFACE
    { procCmdScreenDMA },                           // SVGA_3D_CMD_SCREEN_DMA
    { procCmdVBDXClearRenderTargetViewRegion },     // SVGA_3D_CMD_VB_DX_CLEAR_RENDERTARGET_VIEW_REGION (SVGA_3D_CMD_DEAD1)
    { procCmdInvalid },                             // SVGA_3D_CMD_DEAD2
    { procCmdInvalid },                             // SVGA_3D_CMD_DEAD12
    { procCmdInvalid },                             // SVGA_3D_CMD_DEAD13
    { procCmdInvalid },                             // SVGA_3D_CMD_DEAD14
    { procCmdInvalid },                             // SVGA_3D_CMD_DEAD15
    { procCmdInvalid },                             // SVGA_3D_CMD_DEAD16
    { procCmdInvalid },                             // SVGA_3D_CMD_DEAD17
    { procCmdSetOTableBase },                       // SVGA_3D_CMD_SET_OTABLE_BASE
    { procCmdReadbackOTable },                      // SVGA_3D_CMD_READBACK_OTABLE
    { procCmdDefineGBMob },                         // SVGA_3D_CMD_DEFINE_GB_MOB
    { procCmdDestroyGBMob },                        // SVGA_3D_CMD_DESTROY_GB_MOB
    { procCmdInvalid },                             // SVGA_3D_CMD_DEAD3
    { procCmdUpdateGBMobMapping },                  // SVGA_3D_CMD_UPDATE_GB_MOB_MAPPING
    { procCmdInvalid },                             // SVGA_3D_CMD_DEFINE_GB_SURFACE
    { procCmdDestroyGBSurface },                    // SVGA_3D_CMD_DESTROY_GB_SURFACE
    { procCmdBindGBSurface },                       // SVGA_3D_CMD_BIND_GB_SURFACE
    { procCmdCondBindGBSurface },                   // SVGA_3D_CMD_COND_BIND_GB_SURFACE
    { procCmdUpdateGBImage },                       // SVGA_3D_CMD_UPDATE_GB_IMAGE
    { procCmdUpdateGBSurface },                     // SVGA_3D_CMD_UPDATE_GB_SURFACE
    { procCmdReadbackGBImage },                     // SVGA_3D_CMD_READBACK_GB_IMAGE
    { procCmdReadbackGBSurface },                   // SVGA_3D_CMD_READBACK_GB_SURFACE
    { procCmdInvalidateGBImage },                   // SVGA_3D_CMD_INVALIDATE_GB_IMAGE
    { procCmdInvalidateGBSurface },                 // SVGA_3D_CMD_INVALIDATE_GB_SURFACE
    { procCmdDefineGBContext },                     // SVGA_3D_CMD_DEFINE_GB_CONTEXT
    { procCmdDestroyGBContext },                    // SVGA_3D_CMD_DESTROY_GB_CONTEXT
    { procCmdBindGBContext },                       // SVGA_3D_CMD_BIND_GB_CONTEXT
    { procCmdReadbackGBContext },                   // SVGA_3D_CMD_READBACK_GB_CONTEXT
    { procCmdInvalidateGBContext },                 // SVGA_3D_CMD_INVALIDATE_GB_CONTEXT
    { procCmdDefineGBShader },                      // SVGA_3D_CMD_DEFINE_GB_SHADER
    { procCmdDestroyGBShader },                     // SVGA_3D_CMD_DESTROY_GB_SHADER
    { procCmdBindGBShader },                        // SVGA_3D_CMD_BIND_GB_SHADER
    { procCmdSetOTableBase64 },                     // SVGA_3D_CMD_SET_OTABLE_BASE64
    { procCmdBeginGBQuery },                        // SVGA_3D_CMD_BEGIN_GB_QUERY
    { procCmdEndGBQuery },                          // SVGA_3D_CMD_END_GB_QUERY
    { procCmdWaitForGBQuery },                      // SVGA_3D_CMD_WAIT_FOR_GB_QUERY
    { procCmdInvalid },                             // SVGA_3D_CMD_NOP
    { procCmdInvalid },                             // SVGA_3D_CMD_ENABLE_GART
    { procCmdInvalid },                             // SVGA_3D_CMD_DISABLE_GART
    { procCmdInvalid },                             // SVGA_3D_CMD_MAP_MOB_INTO_GART
    { procCmdInvalid },                             // SVGA_3D_CMD_UNMAP_GART_RANGE
    { procCmdDefineGBScreenTarget },                // SVGA_3D_CMD_DEFINE_GB_SCREENTARGET
    { procCmdDestroyGBScreenTarget },               // SVGA_3D_CMD_DESTROY_GB_SCREENTARGET
    { procCmdBindGBScreenTarget },                  // SVGA_3D_CMD_BIND_GB_SCREENTARGET
    { procCmdUpdateGBScreenTarget },                // SVGA_3D_CMD_UPDATE_GB_SCREENTARGET
    { procCmdReadbackGBImagePartial },              // SVGA_3D_CMD_READBACK_GB_IMAGE_PARTIAL
    { procCmdInvalidateGBImagePartial },            // SVGA_3D_CMD_INVALIDATE_GB_IMAGE_PARTIAL
    { procCmdSetGBShaderConstInline },              // SVGA_3D_CMD_SET_GB_SHADERCONSTS_INLINE
    { procCmdGBScreenDMA },                         // SVGA_3D_CMD_GB_SCREEN_DMA
    { procCmdBindGBSurfaceWithPitch },              // SVGA_3D_CMD_BIND_GB_SURFACE_WITH_PITCH
    { procCmdGBMobFence },                          // SVGA_3D_CMD_GB_MOB_FENCE
    { procCmdInvalid },                             // SVGA_3D_CMD_DEFINE_GB_SURFACE_V2
    { procCmdDefineGBMob64 },                       // SVGA_3D_CMD_DEFINE_GB_MOB64
    { procCmdRedefineGBMob64 },                     // SVGA_3D_CMD_REDEFINE_GB_MOB64
    { procCmdInvalid },                             // SVGA_3D_CMD_NOP_ERROR
    { procCmdSetVertexStreams },                    // SVGA_3D_CMD_SET_VERTEX_STREAMS
    { procCmdSetVertexDecls },                      // SVGA_3D_CMD_SET_VERTEX_DECLS
    { procCmdSetVertexDivisors },                   // SVGA_3D_CMD_SET_VERTEX_DIVISORS
    { procCmdDraw },                                // SVGA_3D_CMD_DRAW
    { procCmdDrawIndexed },                         // SVGA_3D_CMD_DRAW_INDEXED
    { procCmdDXDefineContext },                     // SVGA_3D_CMD_DX_DEFINE_CONTEXT
    { procCmdDXDestroyContext },                    // SVGA_3D_CMD_DX_DESTROY_CONTEXT
    { procCmdDXBindContext },                       // SVGA_3D_CMD_DX_BIND_CONTEXT
    { procCmdDXReadbackContext },                   // SVGA_3D_CMD_DX_READBACK_CONTEXT
    { procCmdDXInvalidateContext },                 // SVGA_3D_CMD_DX_INVALIDATE_CONTEXT
    { procCmdDXSetSingleConstantBuffer },           // SVGA_3D_CMD_DX_SET_SINGLE_CONSTANT_BUFFER
    { procCmdDXSetShaderResources },                // SVGA_3D_CMD_DX_SET_SHADER_RESOURCES
    { procCmdDXSetShader },                         // SVGA_3D_CMD_DX_SET_SHADER
    { procCmdDXSetSamplers },                       // SVGA_3D_CMD_DX_SET_SAMPLERS
    { procCmdDXDraw },                              // SVGA_3D_CMD_DX_DRAW
    { procCmdDXDrawIndexed },                       // SVGA_3D_CMD_DX_DRAW_INDEXED
    { procCmdDXDrawInstanced },                     // SVGA_3D_CMD_DX_DRAW_INSTANCED
    { procCmdDXDrawIndexedInstanced },              // SVGA_3D_CMD_DX_DRAW_INDEXED_INSTANCED
    { procCmdDXDrawAuto },                          // SVGA_3D_CMD_DX_DRAW_AUTO
    { procCmdDXSetInputLayout },                    // SVGA_3D_CMD_DX_SET_INPUT_LAYOUT
    { procCmdDXSetVertexBuffers },                  // SVGA_3D_CMD_DX_SET_VERTEX_BUFFERS
    { procCmdDXSetIndexBuffer },                    // SVGA_3D_CMD_DX_SET_INDEX_BUFFER
    { procCmdDXSetTopology },                       // SVGA_3D_CMD_DX_SET_TOPOLOGY
    { procCmdDXSetRenderTargets },                  // SVGA_3D_CMD_DX_SET_RENDERTARGETS
    { procCmdDXSetBlendState },                     // SVGA_3D_CMD_DX_SET_BLEND_STATE
    { procCmdDXSetDepthStencilState },              // SVGA_3D_CMD_DX_SET_DEPTHSTENCIL_STATE
    { procCmdDXSetRasterizerState },                // SVGA_3D_CMD_DX_SET_RASTERIZER_STATE
    { procCmdDXDefineQuery },                       // SVGA_3D_CMD_DX_DEFINE_QUERY
    { procCmdDXDestroyQuery },                      // SVGA_3D_CMD_DX_DESTROY_QUERY
    { procCmdDXBindQuery },                         // SVGA_3D_CMD_DX_BIND_QUERY
    { procCmdDXSetQueryOffset },                    // SVGA_3D_CMD_DX_SET_QUERY_OFFSET
    { procCmdDXBeginQuery },                        // SVGA_3D_CMD_DX_BEGIN_QUERY
    { procCmdDXEndQuery },                          // SVGA_3D_CMD_DX_END_QUERY
    { procCmdDXReadbackQuery },                     // SVGA_3D_CMD_DX_READBACK_QUERY
    { procCmdDXSetPredication },                    // SVGA_3D_CMD_DX_SET_PREDICATION
    { procCmdDXSetSOTargets },                      // SVGA_3D_CMD_DX_SET_SOTARGETS
    { procCmdDXSetViewports },                      // SVGA_3D_CMD_DX_SET_VIEWPORTS
    { procCmdDXSetScissorRects },                   // SVGA_3D_CMD_DX_SET_SCISSORRECTS
    { procCmdDXClearRenderTargetView },             // SVGA_3D_CMD_DX_CLEAR_RENDERTARGET_VIEW
    { procCmdDXClearDepthStencilView },             // SVGA_3D_CMD_DX_CLEAR_DEPTHSTENCIL_VIEW
    { procCmdDXPredCopyRegion },                    // SVGA_3D_CMD_DX_PRED_COPY_REGION
    { procCmdDXPredCopy },                          // SVGA_3D_CMD_DX_PRED_COPY
    { procCmdDXPresentBlt },                        // SVGA_3D_CMD_DX_PRESENTBLT
    { procCmdDXGenMips },                           // SVGA_3D_CMD_DX_GENMIPS
    { procCmdDXUpdateSubResource },                 // SVGA_3D_CMD_DX_UPDATE_SUBRESOURCE
    { procCmdDXReadbackSubResource },               // SVGA_3D_CMD_DX_READBACK_SUBRESOURCE
    { procCmdDXInvalidateSubResource },             // SVGA_3D_CMD_DX_INVALIDATE_SUBRESOURCE
    { procCmdDXDefineShaderResourceView },          // SVGA_3D_CMD_DX_DEFINE_SHADERRESOURCE_VIEW
    { procCmdDXDestroyShaderResourceView },         // SVGA_3D_CMD_DX_DESTROY_SHADERRESOURCE_VIEW
    { procCmdDXDefineRenderTargetView },            // SVGA_3D_CMD_DX_DEFINE_RENDERTARGET_VIEW
    { procCmdDXDestroyRenderTargetView },           // SVGA_3D_CMD_DX_DESTROY_RENDERTARGET_VIEW
    { procCmdDXDefineDepthStencilView },            // SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_VIEW
    { procCmdDXDestroyDepthStencilView },           // SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_VIEW
    { procCmdDXDefineElementLayout },               // SVGA_3D_CMD_DX_DEFINE_ELEMENTLAYOUT
    { procCmdDXDestroyElementLayout },              // SVGA_3D_CMD_DX_DESTROY_ELEMENTLAYOUT
    { procCmdDXDefineBlendState },                  // SVGA_3D_CMD_DX_DEFINE_BLEND_STATE
    { procCmdDXDestroyBlendState },                 // SVGA_3D_CMD_DX_DESTROY_BLEND_STATE
    { procCmdDXDefineDepthStencilState },           // SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_STATE
    { procCmdDXDestroyDepthStencilState },          // SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_STATE
    { procCmdDXDefineRasterizerState },             // SVGA_3D_CMD_DX_DEFINE_RASTERIZER_STATE
    { procCmdDXDestroyRasterizerState },            // SVGA_3D_CMD_DX_DESTROY_RASTERIZER_STATE
    { procCmdDXDefineSamplerState },                // SVGA_3D_CMD_DX_DEFINE_SAMPLER_STATE
    { procCmdDXDestroySamplerState },               // SVGA_3D_CMD_DX_DESTROY_SAMPLER_STATE
    { procCmdDXDefineShader },                      // SVGA_3D_CMD_DX_DEFINE_SHADER
    { procCmdDXDestroyShader },                     // SVGA_3D_CMD_DX_DESTROY_SHADER
    { procCmdDXBindShader },                        // SVGA_3D_CMD_DX_BIND_SHADER
    { procCmdDXDefineStreamOutput },                // SVGA_3D_CMD_DX_DEFINE_STREAMOUTPUT
    { procCmdDXDestroyStreamOutput },               // SVGA_3D_CMD_DX_DESTROY_STREAMOUTPUT
    { procCmdDXSetStreamOutput },                   // SVGA_3D_CMD_DX_SET_STREAMOUTPUT
    { procCmdDXSetCOTable },                        // SVGA_3D_CMD_DX_SET_COTABLE
    { procCmdDXReadbackCOTable },                   // SVGA_3D_CMD_DX_READBACK_COTABLE
    { procCmdDXBufferCopy },                        // SVGA_3D_CMD_DX_BUFFER_COPY
    { procCmdDXTransferFromBuffer },                // SVGA_3D_CMD_DX_TRANSFER_FROM_BUFFER
    { procCmdDXSurfaceCopyAndReadback },            // SVGA_3D_CMD_DX_SURFACE_COPY_AND_READBACK
    { procCmdDXMoveQuery },                         // SVGA_3D_CMD_DX_MOVE_QUERY
    { procCmdDXBindAllQuery },                      // SVGA_3D_CMD_DX_BIND_ALL_QUERY
    { procCmdDXReadbackAllQuery },                  // SVGA_3D_CMD_DX_READBACK_ALL_QUERY
    { procCmdDXPredTransferFromBuffer },            // SVGA_3D_CMD_DX_PRED_TRANSFER_FROM_BUFFER
    { procCmdDXMobFence64 },                        // SVGA_3D_CMD_DX_MOB_FENCE_64
    { procCmdDXBindAllShader },                     // SVGA_3D_CMD_DX_BIND_ALL_SHADER
    { procCmdDXHint },                              // SVGA_3D_CMD_DX_HINT
    { procCmdDXBufferUpdate },                      // SVGA_3D_CMD_DX_BUFFER_UPDATE
    { procCmdDXSetVSConstantBufferOffset },         // SVGA_3D_CMD_DX_SET_VS_CONSTANT_BUFFER_OFFSET
    { procCmdDXSetPSConstantBufferOffset },         // SVGA_3D_CMD_DX_SET_PS_CONSTANT_BUFFER_OFFSET
    { procCmdDXSetGSConstantBufferOffset },         // SVGA_3D_CMD_DX_SET_GS_CONSTANT_BUFFER_OFFSET
    { procCmdDXSetHSConstantBufferOffset },         // SVGA_3D_CMD_DX_SET_HS_CONSTANT_BUFFER_OFFSET
    { procCmdDXSetDSConstantBufferOffset },         // SVGA_3D_CMD_DX_SET_DS_CONSTANT_BUFFER_OFFSET
    { procCmdDXSetCSConstantBufferOffset },         // SVGA_3D_CMD_DX_SET_CS_CONSTANT_BUFFER_OFFSET
    { procCmdDXCondBindAllShader },                 // SVGA_3D_CMD_DX_COND_BIND_ALL_SHADER
    { procCmdScreenCopy },                          // SVGA_3D_CMD_SCREEN_COPY
    { procCmdInvalid },                             // SVGA_3D_CMD_RESERVED1
    { procCmdInvalid },                             // SVGA_3D_CMD_RESERVED2
    { procCmdInvalid },                             // SVGA_3D_CMD_RESERVED3
    { procCmdInvalid },                             // SVGA_3D_CMD_RESERVED4
    { procCmdInvalid },                             // SVGA_3D_CMD_RESERVED5
    { procCmdInvalid },                             // SVGA_3D_CMD_RESERVED6
    { procCmdInvalid },                             // SVGA_3D_CMD_RESERVED7
    { procCmdInvalid },                             // SVGA_3D_CMD_RESERVED8
    { procCmdGrowOTable },                          // SVGA_3D_CMD_GROW_OTABLE
    { procCmdDXGrowCOTable },                       // SVGA_3D_CMD_DX_GROW_COTABLE
    { procCmdIntraSurfaceCopy },                    // SVGA_3D_CMD_INTRA_SURFACE_COPY
    { procCmdInvalid },                             // SVGA_3D_CMD_DEFINE_GB_SURFACE_V3
    { procCmdDXResolveCopy },                       // SVGA_3D_CMD_DX_RESOLVE_COPY
    { procCmdDXPredResolveCopy },                   // SVGA_3D_CMD_DX_PRED_RESOLVE_COPY
    { procCmdDXPredConvertRegion },                 // SVGA_3D_CMD_DX_PRED_CONVERT_REGION
    { procCmdDXPredConvert },                       // SVGA_3D_CMD_DX_PRED_CONVERT
    { procCmdWholeSurfaceCopy },                    // SVGA_3D_CMD_WHOLE_SURFACE_COPY
    { procCmdDXDefineUAView },                      // SVGA_3D_CMD_DX_DEFINE_UA_VIEW
    { procCmdDXDestroyUAView },                     // SVGA_3D_CMD_DX_DESTROY_UA_VIEW
    { procCmdDXClearUAViewUint },                   // SVGA_3D_CMD_DX_CLEAR_UA_VIEW_UINT
    { procCmdDXClearUAViewFloat },                  // SVGA_3D_CMD_DX_CLEAR_UA_VIEW_FLOAT
    { procCmdDXCopyStructureCount },                // SVGA_3D_CMD_DX_COPY_STRUCTURE_COUNT
    { procCmdDXSetUAViews },                        // SVGA_3D_CMD_DX_SET_UA_VIEWS
    { procCmdDXDrawIndexedInstancedIndirect },      // SVGA_3D_CMD_DX_DRAW_INDEXED_INSTANCED_INDIRECT
    { procCmdDXDrawInstancedIndirect },             // SVGA_3D_CMD_DX_DRAW_INSTANCED_INDIRECT
    { procCmdDXDispatch },                          // SVGA_3D_CMD_DX_DISPATCH
    { procCmdDXDispatchIndirect },                  // SVGA_3D_CMD_DX_DISPATCH_INDIRECT
    { procCmdWriteZeroSurface },                    // SVGA_3D_CMD_WRITE_ZERO_SURFACE
    { procCmdHintZeroSurface },                     // SVGA_3D_CMD_HINT_ZERO_SURFACE
    { procCmdDXTransferToBuffer },                  // SVGA_3D_CMD_DX_TRANSFER_TO_BUFFER
    { procCmdDXSetStructureCount },                 // SVGA_3D_CMD_DX_SET_STRUCTURE_COUNT
    { procCmdLogicOpsBitBlt },                      // SVGA_3D_CMD_LOGICOPS_BITBLT
    { procCmdLogicOpsTransBlt },                    // SVGA_3D_CMD_LOGICOPS_TRANSBLT
    { procCmdLogicOpsStretchBlt },                  // SVGA_3D_CMD_LOGICOPS_STRETCHBLT
    { procCmdLogicOpsColorFill },                   // SVGA_3D_CMD_LOGICOPS_COLORFILL
    { procCmdLogicOpsAlphaBlend },                  // SVGA_3D_CMD_LOGICOPS_ALPHABLEND
    { procCmdLogicOpsClearTypeBlend },              // SVGA_3D_CMD_LOGICOPS_CLEARTYPEBLEND
    { procCmdInvalid },                             // SVGA_3D_CMD_RESERVED2_1
    { procCmdInvalid },                             // SVGA_3D_CMD_RESERVED2_2
    { procCmdInvalid },                             // SVGA_3D_CMD_DEFINE_GB_SURFACE_V4
    { procCmdDXSetCSUAViews },                      // SVGA_3D_CMD_DX_SET_CS_UA_VIEWS
    { procCmdDXSetMinLOD },                         // SVGA_3D_CMD_DX_SET_MIN_LOD
    { procCmdInvalid },                             // SVGA_3D_CMD_RESERVED2_3
    { procCmdInvalid },                             // SVGA_3D_CMD_RESERVED2_4
    { procCmdDXDefineDepthStencilView_v2 },         // SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_VIEW_V2
    { procCmdDXDefineStreamOutputWithMob },         // SVGA_3D_CMD_DX_DEFINE_STREAMOUTPUT_WITH_MOB
    { procCmdDXSetShaderIface },                    // SVGA_3D_CMD_DX_SET_SHADER_IFACE
    { procCmdDXBindStreamOutput },                  // SVGA_3D_CMD_DX_BIND_STREAMOUTPUT
    { procCmdSurfaceStretchBltNonMSToMS },          // SVGA_3D_CMD_SURFACE_STRETCHBLT_NON_MS_TO_MS
    { procCmdDXBindShaderIface },                   // SVGA_3D_CMD_DX_BIND_SHADER_IFACE
    { procCmdInvalid },                             // SVGA_3D_CMD_UPDATE_GB_SCREENTARGET_MOVE = 1278,
    { procCmdInvalid },                             // 1279,
    { procCmdInvalid },                             // 1280,
    { procCmdInvalid },                             // SVGA_3D_CMD_DX_PRED_STAGING_COPY = 1281,
    { procCmdInvalid },                             // SVGA_3D_CMD_DX_STAGING_COPY = 1282,
    { procCmdInvalid },                             // SVGA_3D_CMD_DX_PRED_STAGING_COPY_REGION = 1283,
    { procCmdInvalid },                             // SVGA_3D_CMD_DX_SET_VERTEX_BUFFERS_V2 = 1284,
    { procCmdInvalid },                             // SVGA_3D_CMD_DX_SET_INDEX_BUFFER_V2 = 1285,
    { procCmdInvalid },                             // SVGA_3D_CMD_DX_SET_VERTEX_BUFFERS_OFFSET_AND_SIZE = 1286,
    { procCmdInvalid },                             // SVGA_3D_CMD_DX_SET_INDEX_BUFFER_OFFSET_AND_SIZE = 1287,
    { procCmdDXDefineRasterizerState_v2 },          // SVGA_3D_CMD_DX_DEFINE_RASTERIZER_STATE_V2
    { procCmdInvalid },                             // SVGA_3D_CMD_DX_PRED_STAGING_CONVERT_REGION = 1289,
    { procCmdInvalid },                             // SVGA_3D_CMD_DX_PRED_STAGING_CONVERT = 1290,
    { procCmdInvalid },                             // SVGA_3D_CMD_DX_STAGING_BUFFER_COPY = 1291,
    { procCmdInvalid },                             // 1292,
    { procCmdInvalid },                             // 1293,
    { procCmdInvalid },                             // 1294,
    { procCmdInvalid },                             // 1295,
    { procCmdInvalid },                             // 1296,
    { procCmdInvalid },                             // 1297,
    { procCmdInvalid },                             // 1298,
    { procCmdInvalid },                             // 1299,
    { procCmdInvalid },                             // 1300,
    { procCmdInvalid },                             // 1301,
    { procCmdInvalid },                             // 1302,

    /* VirtualBox commands */
    { procVBCmdDXDefineVideoProcessor },            // VBSVGA_3D_CMD_DX_DEFINE_VIDEO_PROCESSOR
    { procVBCmdDXDefineVideoDecoderOutputView },    // VBSVGA_3D_CMD_DX_DEFINE_VIDEO_DECODER_OUTPUT_VIEW
    { procVBCmdDXDefineVideoDecoder },              // VBSVGA_3D_CMD_DX_DEFINE_VIDEO_DECODER
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_DECODER_BEGIN_FRAME
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_DECODER_SUBMIT_BUFFERS
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_DECODER_END_FRAME
    { procVBCmdDXDefineVideoProcessorInputView },   // VBSVGA_3D_CMD_DX_DEFINE_VIDEO_PROCESSOR_INPUT_VIEW
    { procVBCmdDXDefineVideoProcessorOutputView },  // VBSVGA_3D_CMD_DX_DEFINE_VIDEO_PROCESSOR_OUTPUT_VIEW
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_BLT
    { procVBCmdDXDestroyVideoDecoder },             // VBSVGA_3D_CMD_DX_DESTROY_VIDEO_DECODER
    { procVBCmdDXDestroyVideoDecoderOutputView },   // VBSVGA_3D_CMD_DX_DESTROY_VIDEO_DECODER_OUTPUT_VIEW
    { procVBCmdDXDestroyVideoProcessor },           // VBSVGA_3D_CMD_DX_DESTROY_VIDEO_PROCESSOR
    { procVBCmdDXDestroyVideoProcessorInputView },  // VBSVGA_3D_CMD_DX_DESTROY_VIDEO_PROCESSOR_INPUT_VIEW
    { procVBCmdDXDestroyVideoProcessorOutputView }, // VBSVGA_3D_CMD_DX_DESTROY_VIDEO_PROCESSOR_OUTPUT_VIEW
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_TARGET_RECT
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_BACKGROUND_COLOR
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_COLOR_SPACE
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_ALPHA_FILL_MODE
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_CONSTRICTION
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_OUTPUT_STEREO_MODE
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_FRAME_FORMAT
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_COLOR_SPACE
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_OUTPUT_RATE
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_SOURCE_RECT
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_DEST_RECT
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_ALPHA
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_PALETTE
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_PIXEL_ASPECT_RATIO
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_LUMA_KEY
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_STEREO_FORMAT
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_AUTO_PROCESSING_MODE
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_FILTER
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_VIDEO_PROCESSOR_SET_STREAM_ROTATION
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_GET_VIDEO_CAPABILITY
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_CLEAR_RTV
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_CLEAR_UAV
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_CLEAR_VDOV
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_CLEAR_VPIV
    { procCmdOK },                                  // VBSVGA_3D_CMD_DX_CLEAR_VPOV
};
AssertCompile(RT_ELEMENTS(s_aCommandDesc) == VBSVGA_NUM_COMMANDS);

NTSTATUS SvgaRenderCommandsD3D(PVBOXWDDM_EXT_VMSVGA pSvga,
                               PVMSVGACONTEXT pSvgaContext,
                               void *pvTarget,
                               uint32_t cbTarget,
                               const void *pvSource,
                               uint32_t cbSource,
                               uint32_t *pu32TargetLength,
                               uint32_t *pu32ProcessedLength)
{
    /* All commands consist of 32 bit dwords. */
    AssertReturn(pSvgaContext, STATUS_INVALID_PARAMETER);
    AssertReturn(cbSource % sizeof(uint32_t) == 0, STATUS_ILLEGAL_INSTRUCTION);

    NTSTATUS Status = STATUS_SUCCESS;

    const uint8_t *pu8Src    = (uint8_t *)pvSource;
    const uint8_t *pu8SrcEnd = (uint8_t *)pvSource + cbSource;
    uint8_t *pu8Dst          = (uint8_t *)pvTarget;
    uint8_t *pu8DstEnd       = (uint8_t *)pvTarget + cbTarget;

    while (pu8SrcEnd > pu8Src)
    {
        const uint32_t cbSrcLeft = pu8SrcEnd - pu8Src;
        AssertBreakStmt(cbSrcLeft >= sizeof(uint32_t), Status = STATUS_ILLEGAL_INSTRUCTION);

        /* Get the command id and command length. */
        const uint32_t u32CmdId = *(uint32_t *)pu8Src;
        uint32_t cbCmd = 0;

        /* It is not expected that any of common SVGA commands will be in the command buffer
         * because the user mode driver does not use them.
         */
        uint32_t idxCmd;
        if (SVGA_3D_CMD_BASE <= u32CmdId && u32CmdId < SVGA_3D_CMD_MAX)
            idxCmd = u32CmdId - SVGA_3D_CMD_BASE;
        else if (VBSVGA_3D_CMD_BASE <= u32CmdId && u32CmdId < VBSVGA_3D_CMD_MAX)
            idxCmd = u32CmdId - VBSVGA_3D_CMD_BASE + (SVGA_3D_CMD_MAX - SVGA_3D_CMD_BASE);
        else
            AssertFailedBreakStmt(Status = STATUS_ILLEGAL_INSTRUCTION);

        AssertBreakStmt(idxCmd < RT_ELEMENTS(s_aCommandDesc), Status = STATUS_ILLEGAL_INSTRUCTION);

        /* A 3D command must have a header. */
        AssertBreakStmt(cbSrcLeft >= sizeof(SVGA3dCmdHeader), Status = STATUS_ILLEGAL_INSTRUCTION);

        SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)pu8Src;
        cbCmd = sizeof(SVGA3dCmdHeader) + pHeader->size;
        AssertBreakStmt(cbCmd % sizeof(uint32_t) == 0, Status = STATUS_ILLEGAL_INSTRUCTION);
        AssertBreakStmt(cbSrcLeft >= cbCmd, Status = STATUS_ILLEGAL_INSTRUCTION);

        const uint32_t cbDstLeft = pu8DstEnd - pu8Dst;
        AssertBreakStmt(cbCmd <= cbDstLeft, Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER);

        memcpy(pu8Dst, pu8Src, cbCmd);

        /* Verify the command in dst place and update it if necessary. */
        PFNPROCESSCOMMAND pfnProcessCommand = s_aCommandDesc[idxCmd].pfnProcessCommand;
        AssertBreakStmt(pfnProcessCommand, Status = STATUS_ILLEGAL_INSTRUCTION);
        Status = pfnProcessCommand(pSvga, pSvgaContext, (SVGA3dCmdHeader *)pu8Dst);

        if (Status != STATUS_SUCCESS)
        {
            Assert(Status == STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER);
            break;
        }

        pu8Src += cbCmd;
        pu8Dst += cbCmd;
    }

    if (   Status == STATUS_SUCCESS
        || Status == STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER)
    {
        *pu32TargetLength = pu8Dst - (uint8_t *)pvTarget;
        *pu32ProcessedLength = pu8Src - (uint8_t *)pvSource;
    }

    return Status;
}


#ifdef DEBUG
NTSTATUS SvgaDebugCommandsD3D(PVBOXWDDM_EXT_VMSVGA pSvga,
                              PVMSVGACONTEXT pSvgaContext,
                              const void *pvSource,
                              uint32_t cbSource)
{
    /* All commands consist of 32 bit dwords. */
    AssertReturn(pSvgaContext, STATUS_INVALID_PARAMETER);
    AssertReturn(cbSource % sizeof(uint32_t) == 0, STATUS_ILLEGAL_INSTRUCTION);

    NTSTATUS Status = STATUS_SUCCESS;

    const uint8_t *pu8Src    = (uint8_t *)pvSource;
    const uint8_t *pu8SrcEnd = (uint8_t *)pvSource + cbSource;

    while (pu8SrcEnd > pu8Src)
    {
        const uint32_t cbSrcLeft = pu8SrcEnd - pu8Src;
        AssertBreakStmt(cbSrcLeft >= sizeof(uint32_t), Status = STATUS_ILLEGAL_INSTRUCTION);

        /* Get the command id and command length. */
        const uint32_t u32CmdId = *(uint32_t *)pu8Src;
        uint32_t cbCmd = 0;

        /* It is not expected that any of common SVGA commands will be in the command buffer
         * because the SVGA gallium driver does not use them.
         */
        uint32_t idxCmd;
        if (SVGA_3D_CMD_BASE <= u32CmdId && u32CmdId < SVGA_3D_CMD_MAX)
            idxCmd = u32CmdId - SVGA_3D_CMD_BASE;
        else if (VBSVGA_3D_CMD_BASE <= u32CmdId && u32CmdId < VBSVGA_3D_CMD_MAX)
            idxCmd = u32CmdId - VBSVGA_3D_CMD_BASE + (SVGA_3D_CMD_MAX - SVGA_3D_CMD_BASE);
        else
            AssertFailedBreakStmt(Status = STATUS_ILLEGAL_INSTRUCTION);

        AssertBreakStmt(idxCmd < RT_ELEMENTS(s_aCommandDesc), Status = STATUS_ILLEGAL_INSTRUCTION);

        /* A 3D command must have a header. */
        AssertBreakStmt(cbSrcLeft >= sizeof(SVGA3dCmdHeader), Status = STATUS_ILLEGAL_INSTRUCTION);

        SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)pu8Src;
        cbCmd = sizeof(SVGA3dCmdHeader) + pHeader->size;
        AssertBreakStmt(cbCmd % sizeof(uint32_t) == 0, Status = STATUS_ILLEGAL_INSTRUCTION);
        AssertBreakStmt(cbSrcLeft >= cbCmd, Status = STATUS_ILLEGAL_INSTRUCTION);

        /* Verify the command in src place. */
        PFNPROCESSCOMMAND pfnProcessCommand = s_aCommandDesc[idxCmd].pfnProcessCommand;
        AssertBreakStmt(pfnProcessCommand, Status = STATUS_ILLEGAL_INSTRUCTION);
        pSvgaContext->fDebugVerifyCommands = true;
        Status = pfnProcessCommand(pSvga, pSvgaContext, pHeader);
        pSvgaContext->fDebugVerifyCommands = false;
        if (Status != STATUS_SUCCESS)
        {
            Assert(Status == STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER);
            break;
        }

        pu8Src += cbCmd;
    }

    return Status;
}
#endif /* DEBUG */
