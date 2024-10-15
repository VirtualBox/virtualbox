/* $Id: VBoxGaTypes.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium driver interface.
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

#ifndef GA_INCLUDED_3D_WIN_VBoxGaTypes_h
#define GA_INCLUDED_3D_WIN_VBoxGaTypes_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

#pragma pack(1) /* VMSVGA structures are '__packed'. */
#include <svga3d_caps.h>
#include <svga3d_reg.h>
#pragma pack()

#ifdef __cplusplus
extern "C" {
#endif

#define GA_MAX_SURFACE_FACES 6
#define GA_MAX_MIP_LEVELS 24

typedef struct GASURFCREATE
{
    uint32_t flags;  /* SVGA3dSurfaceFlags */
    uint32_t format; /* SVGA3dSurfaceFormat */
    uint32_t usage;  /* SVGA_SURFACE_USAGE_* */
    uint32_t mip_levels[GA_MAX_SURFACE_FACES];
} GASURFCREATE;

typedef struct GASURFSIZE
{
    uint32_t cWidth;
    uint32_t cHeight;
    uint32_t cDepth;
    uint32_t u32Reserved;
} GASURFSIZE;

#define GA_FENCE_STATUS_NULL      0 /* Fence not found */
#define GA_FENCE_STATUS_IDLE      1
#define GA_FENCE_STATUS_SUBMITTED 2
#define GA_FENCE_STATUS_SIGNALED  3

typedef struct GAFENCEQUERY
{
    /* IN: The miniport's handle of the fence.
     * Assigned by the miniport. Not DXGK fence id!
     */
    uint32_t u32FenceHandle;

    /* OUT: The miniport's sequence number associated with the command buffer.
     */
    uint32_t u32SubmittedSeqNo;

    /* OUT: The miniport's sequence number associated with the last command buffer completed on host.
     */
    uint32_t u32ProcessedSeqNo;

    /* OUT: GA_FENCE_STATUS_*. */
    uint32_t u32FenceStatus;
} GAFENCEQUERY;

typedef struct SVGAGBSURFCREATE
{
    /* Surface data. */
    struct
    {
        SVGA3dSurfaceAllFlags flags;
        SVGA3dSurfaceFormat format;
        unsigned usage;
        SVGA3dSize size;
        uint32_t numFaces;
        uint32_t numMipLevels;
        unsigned sampleCount;
        SVGA3dMSPattern multisamplePattern;
        SVGA3dMSQualityLevel qualityLevel;
    } s;
    uint32_t gmrid; /* In/Out: Backing GMR. */
    uint32_t cbGB; /* Out: Size of backing memory. */
    uint64_t u64UserAddress; /* Out: R3 mapping of the backing memory. */
    uint32_t u32Sid; /* Out: Surface id. */
} SVGAGBSURFCREATE, *PSVGAGBSURFCREATE;

#ifdef __cplusplus
}
#endif

#endif /* !GA_INCLUDED_3D_WIN_VBoxGaTypes_h */

