/* $Id: VBoxMPDevExt.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Miniport device extension header
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_common_VBoxMPDevExt_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_common_VBoxMPDevExt_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxMPUtils.h"
#include <VBoxVideoGuest.h>
#include <HGSMIHostCmd.h>

#ifdef VBOX_XPDM_MINIPORT
# include <iprt/nt/miniport.h>
# include <ntddvdeo.h>
# include <iprt/nt/video.h>
# include "common/xpdm/VBoxVideoPortAPI.h"
#endif

#ifdef VBOX_WDDM_MINIPORT
extern DWORD g_VBoxDisplayOnly;
# include "wddm/VBoxMPTypes.h"
#endif

#ifdef VBOX_WDDM_MINIPORT
typedef struct VBOXWDDM_HWRESOURCES
{
    PHYSICAL_ADDRESS phVRAM;
    ULONG cbVRAM;
    ULONG ulApertureSize;
#ifdef VBOX_WITH_VMSVGA
    PHYSICAL_ADDRESS phFIFO;
    ULONG cbFIFO;
    PHYSICAL_ADDRESS phIO;
    ULONG cbIO;
#endif
} VBOXWDDM_HWRESOURCES, *PVBOXWDDM_HWRESOURCES;

#ifdef VBOX_WITH_VMSVGA
typedef struct VBOXWDDM_EXT_GA *PVBOXWDDM_EXT_GA;
#endif

#endif /* VBOX_WDDM_MINIPORT */

#define VBOXMP_MAX_VIDEO_MODES 128
typedef struct VBOXMP_COMMON
{
    int cDisplays;                      /* Number of displays. */

    uint32_t cbVRAM;                    /* The VRAM size. */

    PHYSICAL_ADDRESS phVRAM;            /* Physical VRAM base. */

    ULONG ulApertureSize;               /* Size of the LFB aperture (>= VRAM size). */

    uint32_t cbMiniportHeap;            /* The size of reserved VRAM for miniport driver heap.
                                         * It is at offset:
                                         *   cbAdapterMemorySize - VBOX_VIDEO_ADAPTER_INFORMATION_SIZE - cbMiniportHeap
                                         */
    void *pvMiniportHeap;               /* The pointer to the miniport heap VRAM.
                                         * This is mapped by miniport separately.
                                         */
    void *pvAdapterInformation;         /* The pointer to the last 4K of VRAM.
                                         * This is mapped by miniport separately.
                                         */

    /** Whether HGSMI is enabled. */
    bool bHGSMI;
    /** Context information needed to receive commands from the host. */
    HGSMIHOSTCOMMANDCONTEXT hostCtx;
    /** Context information needed to submit commands to the host. */
    HGSMIGUESTCOMMANDCONTEXT guestCtx;

    BOOLEAN fAnyX;                      /* Unrestricted horizontal resolution flag. */
    uint16_t u16SupportedScreenFlags;   /* VBVA_SCREEN_F_* flags supported by the host. */
} VBOXMP_COMMON, *PVBOXMP_COMMON;

typedef struct _VBOXMP_DEVEXT
{
   struct _VBOXMP_DEVEXT *pNext;               /* Next extension in the DualView extension list.
                                                * The primary extension is the first one.
                                                */
#ifdef VBOX_XPDM_MINIPORT
   struct _VBOXMP_DEVEXT *pPrimary;            /* Pointer to the primary device extension. */

   ULONG iDevice;                              /* Device index: 0 for primary, otherwise a secondary device. */
   /* Standart video modes list.
    * Additional space is reserved for a custom video mode for this guest monitor.
    * The custom video mode index is alternating for each mode set and 2 indexes are needed for the custom mode.
    */
   VIDEO_MODE_INFORMATION aVideoModes[VBOXMP_MAX_VIDEO_MODES + 2];
   /* Number of available video modes, set by VBoxMPCmnBuildVideoModesTable. */
   uint32_t cVideoModes;
   ULONG CurrentMode;                          /* Saved information about video modes */
   ULONG CurrentModeWidth;
   ULONG CurrentModeHeight;
   ULONG CurrentModeBPP;

   ULONG ulFrameBufferOffset;                  /* The framebuffer position in the VRAM. */
   ULONG ulFrameBufferSize;                    /* The size of the current framebuffer. */

   uint8_t  iInvocationCounter;
   uint32_t Prev_xres;
   uint32_t Prev_yres;
   uint32_t Prev_bpp;
#endif /*VBOX_XPDM_MINIPORT*/

#ifdef VBOX_WDDM_MINIPORT
   PDEVICE_OBJECT pPDO;
   UNICODE_STRING RegKeyName;
   UNICODE_STRING VideoGuid;

   uint8_t * pvVisibleVram;

   VBOXVIDEOCM_MGR CmMgr;
   VBOXVIDEOCM_MGR SeamlessCtxMgr;
   /* hgsmi allocation manager */
   VBOXVIDEOCM_ALLOC_MGR AllocMgr;
   /* mutex for context list operations */
   VBOXVDMADDI_NODE aNodes[VBOXWDDM_NUM_NODES];
   LIST_ENTRY DpcCmdQueue;
   KSPIN_LOCK ContextLock;
   KSPIN_LOCK SynchLock;
   volatile uint32_t cContexts3D;
   volatile uint32_t cContexts2D;
   volatile uint32_t cContextsDispIfResize;
   volatile uint32_t cUnlockedVBVADisabled;

   volatile uint32_t fCompletingCommands;

   DWORD dwDrvCfgFlags;

   BOOLEAN f3DEnabled;
   BOOLEAN fCmdVbvaEnabled;
   BOOLEAN fComplexTopologiesEnabled;

   VBOXWDDM_GLOBAL_POINTER_INFO PointerInfo;

   VBOXVTLIST CtlList;
   VBOXVTLIST DmaCmdList;
#ifdef VBOX_WITH_VIDEOHWACCEL
   VBOXVTLIST VhwaCmdList;
#endif
   BOOLEAN bNotifyDxDpc;

   BOOLEAN fDisableTargetUpdate;



   BOOL bVSyncTimerEnabled;
   volatile uint32_t fVSyncInVBlank;
   volatile LARGE_INTEGER VSyncTime;
   KTIMER VSyncTimer;
   KDPC VSyncDpc;

#if 0
   FAST_MUTEX ShRcTreeMutex;
   AVLPVTREE ShRcTree;
#endif

   VBOXWDDM_SOURCE aSources[VBOX_VIDEO_MAX_SCREENS];
   VBOXWDDM_TARGET aTargets[VBOX_VIDEO_MAX_SCREENS];
#endif /*VBOX_WDDM_MINIPORT*/

   union {
       /* Information that is only relevant to the primary device or is the same for all devices. */
       struct {

           void *pvReqFlush;                   /* Pointer to preallocated generic request structure for
                                                * VMMDevReq_VideoAccelFlush. Allocated when VBVA status
                                                * is changed. Deallocated on HwReset.
                                                */
           ULONG ulVbvaEnabled;                /* Indicates that VBVA mode is enabled. */
           ULONG ulMaxFrameBufferSize;         /* The size of the VRAM allocated for the a single framebuffer. */
           BOOLEAN fMouseHidden;               /* Has the mouse cursor been hidden by the guest? */
           VBOXMP_COMMON commonInfo;
#ifdef VBOX_XPDM_MINIPORT
           /* Video Port API dynamically picked up at runtime for binary backwards compatibility with older NT versions */
           VBOXVIDEOPORTPROCS VideoPortProcs;
#endif

#ifdef VBOX_WDDM_MINIPORT
           VBOXVDMAINFO Vdma;
           UINT uLastCompletedPagingBufferCmdFenceId; /* Legacy */
# ifdef VBOXVDMA_WITH_VBVA
           VBOXVBVAINFO Vbva;
# endif
           D3DKMDT_HVIDPN hCommittedVidPn;      /* committed VidPn handle */
           DXGKRNL_INTERFACE DxgkInterface;     /* Display Port handle and callbacks */
#endif
       } primary;

       /* Secondary device information. */
       struct {
           BOOLEAN bEnabled;                   /* Device enabled flag */
       } secondary;
   } u;

   HGSMIAREA areaDisplay;                      /* Entire VRAM chunk for this display device. */

#ifdef VBOX_WDDM_MINIPORT
   VBOXVIDEO_HWTYPE enmHwType;
   VBOXWDDM_HWRESOURCES HwResources;
#endif

#ifdef VBOX_WITH_VMSVGA
   PVBOXWDDM_EXT_GA pGa;                       /* Pointer to Gallium backend data. */
#endif

   ULONG cbVRAMCpuVisible;                     /* How much video memory is available for the CPU visible segment. */
} VBOXMP_DEVEXT, *PVBOXMP_DEVEXT;

DECLINLINE(PVBOXMP_DEVEXT) VBoxCommonToPrimaryExt(PVBOXMP_COMMON pCommon)
{
    return RT_FROM_MEMBER(pCommon, VBOXMP_DEVEXT, u.primary.commonInfo);
}

DECLINLINE(PVBOXMP_COMMON) VBoxCommonFromDeviceExt(PVBOXMP_DEVEXT pExt)
{
#ifdef VBOX_XPDM_MINIPORT
    return &pExt->pPrimary->u.primary.commonInfo;
#else
    return &pExt->u.primary.commonInfo;
#endif
}

#ifdef VBOX_WDDM_MINIPORT
DECLINLINE(ULONG) vboxWddmVramCpuVisibleSize(PVBOXMP_DEVEXT pDevExt)
{
    return pDevExt->cbVRAMCpuVisible;
}

DECLINLINE(ULONG) vboxWddmVramCpuVisibleSegmentSize(PVBOXMP_DEVEXT pDevExt)
{
    return vboxWddmVramCpuVisibleSize(pDevExt);
}

/* 128 MB */
DECLINLINE(ULONG) vboxWddmVramCpuInvisibleSegmentSize(PVBOXMP_DEVEXT pDevExt)
{
    RT_NOREF(pDevExt);
    return 128 * 1024 * 1024;
}

#ifdef VBOXWDDM_RENDER_FROM_SHADOW

DECLINLINE(bool) vboxWddmCmpSurfDescsBase(VBOXWDDM_SURFACE_DESC *pDesc1, VBOXWDDM_SURFACE_DESC *pDesc2)
{
    if (pDesc1->width != pDesc2->width)
        return false;
    if (pDesc1->height != pDesc2->height)
        return false;
    if (pDesc1->format != pDesc2->format)
        return false;
    if (pDesc1->bpp != pDesc2->bpp)
        return false;
    if (pDesc1->pitch != pDesc2->pitch)
        return false;
    return true;
}

#endif
#endif /*VBOX_WDDM_MINIPORT*/

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_common_VBoxMPDevExt_h */
