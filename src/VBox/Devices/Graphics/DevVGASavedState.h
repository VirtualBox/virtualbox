/* $Id: DevVGASavedState.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevVGA - Saved state versions.
 *
 * @remarks HGSMI needs this but doesn't want to deal with DevVGA.h, thus this
 *          dedicated header.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Graphics_DevVGASavedState_h
#define VBOX_INCLUDED_SRC_Graphics_DevVGASavedState_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** Creates an eyecatching marker in the VGA saved state ("<uSub>Marker\n"). */
#define VGA_SAVED_STATE_MAKE_MARKER(uSub) (UINT64_C(0x0a72656b72614d30) + (uint64_t)(uSub))

/** Puts a marker. Status code is not checked. */
#define VGA_SAVED_STATE_PUT_MARKER(pSSM, uSub) \
    do { pHlp->pfnSSMPutU64(pSSM, VGA_SAVED_STATE_MAKE_MARKER(uSub)); } while (0)

/** Retrieves a VGA saved state marker and checks that it matches, if it
 *  doesn't assert/LogRel and return. */
#define VGA_SAVED_STATE_GET_MARKER_RETURN_ON_MISMATCH(pSSM, uVersion, uSub) \
    do { \
        if (uVersion >= VGA_SAVEDSTATE_VERSION_MARKERS) \
        { \
            uint64_t uMarker; \
            int rcMarker = pHlp->pfnSSMGetU64(pSSM, &uMarker); \
            AssertLogRelRCReturn(rcMarker, rcMarker); \
            AssertLogRelMsgReturn(uMarker == VGA_SAVED_STATE_MAKE_MARKER(uSub), \
                                  ("Bad VGA marker: expected %llx, got %llx\n", VGA_SAVED_STATE_MAKE_MARKER(uSub), uMarker), \
                                  VERR_SSM_DATA_UNIT_FORMAT_CHANGED); \
        } \
    } while (0)

#define VGA_SAVEDSTATE_VERSION                   29
#define VGA_SAVEDSTATE_VERSION_VMSVGA_VIDEO      29 /* Video decoding and processing (new COTables). */
#define VGA_SAVEDSTATE_VERSION_VMSVGA_SVGA3      28 /* SVGA 3 interface. */
#define VGA_SAVEDSTATE_VERSION_VMSVGA_REG_CAP2   27 /* SVGA_REG_CAP2. */
#define VGA_SAVEDSTATE_VERSION_VMSVGA_DX_SFLAGS  26 /* SVGA3dSurfaceAllFlags. */
#define VGA_SAVEDSTATE_VERSION_VMSVGA_DX_CMDBUF  25 /* Command buffers capability is not tied to VGPU10 setting. */
#define VGA_SAVEDSTATE_VERSION_VMSVGA_DX         24 /* VGPU10. */
#define VGA_SAVEDSTATE_VERSION_VMSVGA_MIPLEVELS  23 /* Surface struct with number of miplevels. */
#define VGA_SAVEDSTATE_VERSION_VMSVGA_CURSOR     22 /* Legacy cursor registers. */
#define VGA_SAVEDSTATE_VERSION_VMSVGA_SCREENS    21 /* Screen objects. */
#define VGA_SAVEDSTATE_VERSION_VMSVGA            20 /* Multiple updates and fixes for VMSVGA saved state. */
#define VGA_SAVEDSTATE_VERSION_VMSVGA_TEX_STAGES 19
#define VGA_SAVEDSTATE_VERSION_VMSVGA_GMR_COUNT  18
#define VGA_SAVEDSTATE_VERSION_VMSVGA_VGA_FB_FIX 17
#define VGA_SAVEDSTATE_VERSION_MARKERS           16
#define VGA_SAVEDSTATE_VERSION_MODE_HINTS        15
#define VGA_SAVEDSTATE_VERSION_FIXED_PENDVHWA    14
#define VGA_SAVEDSTATE_VERSION_3D                13
#define VGA_SAVEDSTATE_VERSION_HGSMIMA           12 /* HGSMI memory allocator. */
#define VGA_SAVEDSTATE_VERSION_VMSVGA_2D         10 /* <- internal build with 2d state only */
#define VGA_SAVEDSTATE_VERSION_WITH_PENDVHWA     10
#define VGA_SAVEDSTATE_VERSION_INV_GCMDFIFO       8 /* <- states upto and including this version may contain invalid completed Guest Commands fifo entries */
#define VGA_SAVEDSTATE_VERSION_INV_VHEIGHT        8 /* <- states upto and including this version may contain invalid vbe_regs[VBE_DISPI_INDEX_VIRT_HEIGHT] value */
#define VGA_SAVEDSTATE_VERSION_WDDM               7
#define VGA_SAVEDSTATE_VERSION_PRE_WDDM           6
#define VGA_SAVEDSTATE_VERSION_HOST_HEAP          5
#define VGA_SAVEDSTATE_VERSION_WITH_CONFIG        4
#define VGA_SAVEDSTATE_VERSION_HGSMI              3
#define VGA_SAVEDSTATE_VERSION_PRE_HGSMI          2
#define VGA_SAVEDSTATE_VERSION_ANCIENT            1

#endif /* !VBOX_INCLUDED_SRC_Graphics_DevVGASavedState_h */

