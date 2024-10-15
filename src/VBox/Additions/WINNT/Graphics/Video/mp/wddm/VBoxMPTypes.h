/* $Id: VBoxMPTypes.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox WDDM Miniport driver
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPTypes_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPTypes_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

typedef struct _VBOXMP_DEVEXT *PVBOXMP_DEVEXT;
typedef struct VBOXWDDM_CONTEXT *PVBOXWDDM_CONTEXT;
typedef struct VBOXWDDM_ALLOCATION *PVBOXWDDM_ALLOCATION;

#include "common/wddm/VBoxMPIf.h"
#include "VBoxMPMisc.h"
#include "VBoxMPCm.h"
#include "VBoxMPVdma.h"
#include "VBoxMPShgsmi.h"
#include "VBoxMPVbva.h"
#include "VBoxMPSa.h"
#include "VBoxMPVModes.h"

#ifdef DEBUG_sunlover
#define DEBUG_BREAKPOINT_TEST() do { ASMBreakpoint(); } while (0)
#else
#define DEBUG_BREAKPOINT_TEST() do { } while (0)
#endif

#if 0
#include <iprt/avl.h>
#endif

#define VBOXWDDM_DEFAULT_REFRESH_RATE 60

#ifndef VBOX_WITH_VMSVGA
/* one page size */
#define VBOXWDDM_C_DMA_BUFFER_SIZE         0x1000
#define VBOXWDDM_C_DMA_PRIVATEDATA_SIZE    0x4000
#else
/* A small buffer fills quickly by commands. A large buffer does not allow effective preemption.
 * 16K is an aritrary value, which allows to batch enough commands, but not too many.
 */
#define VBOXWDDM_C_DMA_BUFFER_SIZE         0x4000
/* Private data is rather small, so one page is more than enough. */
#define VBOXWDDM_C_DMA_PRIVATEDATA_SIZE    0x1000
#endif
#define VBOXWDDM_C_ALLOC_LIST_SIZE         0xc00
#define VBOXWDDM_C_PATH_LOCATION_LIST_SIZE 0xc00

#ifndef VBOX_WITH_VMSVGA
#define VBOXWDDM_C_POINTER_MAX_WIDTH  64
#define VBOXWDDM_C_POINTER_MAX_HEIGHT 64
#else
#define VBOXWDDM_C_POINTER_MAX_WIDTH  256
#define VBOXWDDM_C_POINTER_MAX_HEIGHT 256
#define VBOXWDDM_C_POINTER_MAX_WIDTH_LEGACY  64
#define VBOXWDDM_C_POINTER_MAX_HEIGHT_LEGACY 64
#endif

#define VBOXWDDM_DUMMY_DMABUFFER_SIZE 4

#define VBOXWDDM_POINTER_ATTRIBUTES_SIZE VBOXWDDM_ROUNDBOUND( \
         VBOXWDDM_ROUNDBOUND( sizeof (VIDEO_POINTER_ATTRIBUTES), 4 ) + \
         VBOXWDDM_ROUNDBOUND(VBOXWDDM_C_POINTER_MAX_WIDTH * VBOXWDDM_C_POINTER_MAX_HEIGHT * 4, 4) + \
         VBOXWDDM_ROUNDBOUND((VBOXWDDM_C_POINTER_MAX_WIDTH * VBOXWDDM_C_POINTER_MAX_HEIGHT + 7) >> 3, 4) \
          , 8)

typedef struct _VBOXWDDM_POINTER_INFO
{
    uint32_t xPos;
    uint32_t yPos;
    union
    {
        VIDEO_POINTER_ATTRIBUTES data;
        char buffer[VBOXWDDM_POINTER_ATTRIBUTES_SIZE];
    } Attributes;
} VBOXWDDM_POINTER_INFO, *PVBOXWDDM_POINTER_INFO;

typedef struct _VBOXWDDM_GLOBAL_POINTER_INFO
{
    /** Last updated X position. */
    uint32_t iLastPosX;
    /** Last updated Y position. */
    uint32_t iLastPosY;
    uint32_t iLastReportedScreen;
} VBOXWDDM_GLOBAL_POINTER_INFO, *PVBOXWDDM_GLOBAL_POINTER_INFO;

#ifdef VBOX_WITH_VIDEOHWACCEL
typedef struct VBOXWDDM_VHWA
{
    VBOXVHWA_INFO Settings;
    volatile uint32_t cOverlaysCreated;
} VBOXWDDM_VHWA;
#endif

typedef struct VBOXWDDM_ADDR
{
    /* if SegmentId == NULL - the sysmem data is presented with pvMem */
    UINT SegmentId;
    union {
        VBOXVIDEOOFFSET offVram;
        void * pvMem;
    };
} VBOXWDDM_ADDR, *PVBOXWDDM_ADDR;

typedef struct VBOXWDDM_ALLOC_DATA
{
    VBOXWDDM_SURFACE_DESC SurfDesc;
    VBOXWDDM_ADDR Addr;
    uint32_t hostID;
    uint32_t cHostIDRefs;
} VBOXWDDM_ALLOC_DATA, *PVBOXWDDM_ALLOC_DATA;

#define VBOXWDDM_HGSYNC_F_SYNCED_DIMENSIONS 0x01
#define VBOXWDDM_HGSYNC_F_SYNCED_LOCATION   0x02
#define VBOXWDDM_HGSYNC_F_SYNCED_VISIBILITY 0x04
#define VBOXWDDM_HGSYNC_F_SYNCED_TOPOLOGY   0x08
#define VBOXWDDM_HGSYNC_F_SYNCED_ALL        (VBOXWDDM_HGSYNC_F_SYNCED_DIMENSIONS | VBOXWDDM_HGSYNC_F_SYNCED_LOCATION | VBOXWDDM_HGSYNC_F_SYNCED_VISIBILITY | VBOXWDDM_HGSYNC_F_SYNCED_TOPOLOGY)
#define VBOXWDDM_HGSYNC_F_CHANGED_LOCATION_ONLY        (VBOXWDDM_HGSYNC_F_SYNCED_ALL & ~VBOXWDDM_HGSYNC_F_SYNCED_LOCATION)
#define VBOXWDDM_HGSYNC_F_CHANGED_TOPOLOGY_ONLY        (VBOXWDDM_HGSYNC_F_SYNCED_ALL & ~VBOXWDDM_HGSYNC_F_SYNCED_TOPOLOGY)

typedef struct VBOXWDDM_SOURCE
{
    struct VBOXWDDM_ALLOCATION * pPrimaryAllocation;
    VBOXWDDM_ALLOC_DATA AllocData;
    uint8_t u8SyncState;
    BOOLEAN fTargetsReported;
    BOOLEAN bVisible;
    BOOLEAN bBlankedByPowerOff;
    VBOXVBVAINFO Vbva;
#ifdef VBOX_WITH_VIDEOHWACCEL
    /* @todo: in our case this seems more like a target property,
     * but keep it here for now */
    VBOXWDDM_VHWA Vhwa;
    volatile uint32_t cOverlays;
    LIST_ENTRY OverlayList;
    KSPIN_LOCK OverlayListLock;
#endif
    KSPIN_LOCK AllocationLock;
    POINT VScreenPos;
    VBOXWDDM_POINTER_INFO PointerInfo;
    uint32_t cTargets;
    VBOXCMDVBVA_SCREENMAP_DECL(uint32_t, aTargetMap);
} VBOXWDDM_SOURCE, *PVBOXWDDM_SOURCE;

typedef struct VBOXWDDM_TARGET
{
    RTRECTSIZE Size;
    uint32_t u32Id;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    /* since there coul be multiple state changes on auto-resize,
     * we pend notifying host to avoid flickering */
    uint8_t u8SyncState;
    bool fConnected;
    bool fConfigured;
    bool fBlankedByPowerOff;

    /* Whether the host has disabled the virtual screen. */
    /** @todo This should be merged with fConnected. */
    bool fDisabled;
} VBOXWDDM_TARGET, *PVBOXWDDM_TARGET;

/* allocation */
//#define VBOXWDDM_ALLOCATIONINDEX_VOID (~0U)
typedef struct VBOXWDDM_ALLOCATION
{
    VBOXWDDM_ALLOC_TYPE enmType;
    D3DDDI_RESOURCEFLAGS fRcFlags;
#ifdef VBOX_WITH_VIDEOHWACCEL
    VBOXVHWA_SURFHANDLE hHostHandle;
#endif
    BOOLEAN fDeleted;
    BOOLEAN bVisible;
    BOOLEAN bAssigned;
#ifdef DEBUG
    /* current for shared rc handling assumes that once resource has no opens, it can not be openned agaion */
    BOOLEAN fAssumedDeletion;
#endif
    VBOXWDDM_ALLOC_DATA AllocData;
    struct VBOXWDDM_RESOURCE *pResource;
    /* to return to the Runtime on DxgkDdiCreateAllocation */
    DXGK_ALLOCATIONUSAGEHINT UsageHint;
    uint32_t iIndex;
    uint32_t cOpens;
    KSPIN_LOCK OpenLock;
    LIST_ENTRY OpenList;
    /* helps tracking when to release wine shared resource */
    uint32_t cShRcRefs;
    HANDLE hSharedHandle;
#if 0
    AVLPVNODECORE ShRcTreeEntry;
#endif
    VBOXUHGSMI_BUFFER_TYPE_FLAGS fUhgsmiType;
    int                         CurVidPnSourceId;              /* Source index if used for a source, otherwise -1. */
#ifdef VBOX_WITH_VMSVGA3D_DX
    /* Direct3D driver data for .enmType == VBOXWDDM_ALLOC_TYPE_D3D. */
    struct
    {
        VBOXDXALLOCATIONDESC    desc;
        uint32_t                sid;                        /* For surfaces. */
        uint32_t                mobid;                      /* For surfaces and shaders. */
        uint32_t                SegmentId;                  /* Segment of the allocation. */
        union
        {
            PMDL                pMDL;                       /* Guest backing for aperture segment 2. */
            struct
            {
                struct VMSVGAMOB *pMob;                     /* Mob for the pages (including RTR0MEMOBJ). */
            } gb; /** @todo remove the struct */
        };
    } dx;
#endif /* VBOX_WITH_VMSVGA3D_DX */
} VBOXWDDM_ALLOCATION, *PVBOXWDDM_ALLOCATION;

typedef struct VBOXWDDM_RESOURCE
{
    VBOXWDDMDISP_RESOURCE_FLAGS fFlags;
    volatile uint32_t cRefs;
    VBOXWDDM_RC_DESC RcDesc;
    BOOLEAN fDeleted;
    uint32_t cAllocations;
    VBOXWDDM_ALLOCATION aAllocations[1];
} VBOXWDDM_RESOURCE, *PVBOXWDDM_RESOURCE;

typedef struct VBOXWDDM_OVERLAY
{
    LIST_ENTRY ListEntry;
    PVBOXMP_DEVEXT pDevExt;
    PVBOXWDDM_RESOURCE pResource;
    PVBOXWDDM_ALLOCATION pCurentAlloc;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    RECT DstRect;
} VBOXWDDM_OVERLAY, *PVBOXWDDM_OVERLAY;

typedef enum
{
    VBOXWDDM_DEVICE_TYPE_UNDEFINED = 0,
    VBOXWDDM_DEVICE_TYPE_SYSTEM
} VBOXWDDM_DEVICE_TYPE;

typedef struct VBOXWDDM_DEVICE
{
    PVBOXMP_DEVEXT pAdapter; /* Adapder info */
    HANDLE hDevice; /* handle passed to CreateDevice */
    VBOXWDDM_DEVICE_TYPE enmType; /* device creation flags passed to DxgkDdiCreateDevice, not sure we need it */
} VBOXWDDM_DEVICE, *PVBOXWDDM_DEVICE;

typedef enum
{
    VBOXWDDM_OBJSTATE_TYPE_UNKNOWN = 0,
    VBOXWDDM_OBJSTATE_TYPE_INITIALIZED,
    VBOXWDDM_OBJSTATE_TYPE_TERMINATED
} VBOXWDDM_OBJSTATE_TYPE;

#define VBOXWDDM_INVALID_COORD ((LONG)((~0UL) >> 1))

#ifdef VBOX_WITH_VMSVGA
struct VMSVGACONTEXT;
#endif

typedef struct VBOXWDDM_CONTEXT
{
    struct VBOXWDDM_DEVICE * pDevice;
    HANDLE hContext;
    VBOXWDDM_CONTEXT_TYPE enmType;
    UINT  NodeOrdinal;
    UINT  EngineAffinity;
    BOOLEAN fRenderFromShadowDisabled;
    VBOXVIDEOCM_CTX CmContext;
    VBOXVIDEOCM_ALLOC_CONTEXT AllocContext;
#ifdef VBOX_WITH_VMSVGA
    struct VMSVGACONTEXT *pSvgaContext;
#endif
} VBOXWDDM_CONTEXT, *PVBOXWDDM_CONTEXT;

typedef struct VBOXWDDM_OPENALLOCATION
{
    LIST_ENTRY ListEntry;
    D3DKMT_HANDLE  hAllocation;
    PVBOXWDDM_ALLOCATION pAllocation;
    PVBOXWDDM_DEVICE pDevice;
    uint32_t cShRcRefs;
    uint32_t cOpens;
    uint32_t cHostIDRefs;
} VBOXWDDM_OPENALLOCATION, *PVBOXWDDM_OPENALLOCATION;

#define VBOX_VMODES_MAX_COUNT 128

typedef struct VBOX_VMODES
{
    uint32_t cTargets;
    CR_SORTARRAY aTargets[VBOX_VIDEO_MAX_SCREENS];
} VBOX_VMODES;

typedef struct VBOXWDDM_VMODES
{
    VBOX_VMODES Modes;
    /* note that we not use array indices to indentify modes, because indices may change due to element removal */
    uint64_t aTransientResolutions[VBOX_VIDEO_MAX_SCREENS];
    uint64_t aPendingRemoveCurResolutions[VBOX_VIDEO_MAX_SCREENS];
} VBOXWDDM_VMODES;

typedef struct VBOXVDMADDI_CMD_QUEUE
{
    volatile uint32_t cQueuedCmds;
    LIST_ENTRY CmdQueue;
} VBOXVDMADDI_CMD_QUEUE, *PVBOXVDMADDI_CMD_QUEUE;

typedef struct VBOXVDMADDI_NODE
{
    VBOXVDMADDI_CMD_QUEUE CmdQueue;
    UINT uLastCompletedFenceId;
} VBOXVDMADDI_NODE, *PVBOXVDMADDI_NODE;

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPTypes_h */
