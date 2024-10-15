/* $Id: VBoxMPIOCTL.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Miniport IOCTL handlers
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

#include "VBoxMPInternal.h"
#include "common/VBoxMPCommon.h"
#include <VBoxVideoVBE.h>
#include <VBox/VBoxGuestLib.h>
#include <VBoxVideo.h>

/* Note: in/out parameters passed to VBoxDrvStartIO point to the same memory location.
 * That means we can't read anything from the input one after first write to the output.
 * Defines below are somewhat silly way to catch possible misuse at compile time.
 */
#define VBOXMPIOCTL_HIDE(_var) \
    {                          \
        PVOID (_var);          \
        (VOID)(_var)

#define VBOXMPIOCTL_UNHIDE()   \
    }

#ifndef DOXYGEN_RUNNING
# if RT_MSC_PREREQ(RT_MSC_VER_VC140)
/* VBoxMPIOCTL.cpp(80): warning C4457: declaration of 'pRequestedAddress' hides function parameter (caused by VBOXMPIOCTL_HIDE) */
#  pragma warning(disable:4457 )
# endif
#endif

/* Called for IOCTL_VIDEO_RESET_DEVICE.
 * Reset device to a state it comes at system boot time.
 * @todo It doesn't do anythyng at the moment, but it looks like the same as VBoxDrvResetHW.
 */
BOOLEAN VBoxMPResetDevice(PVBOXMP_DEVEXT pExt, PSTATUS_BLOCK pStatus)
{
    RT_NOREF(pStatus);
    LOGF_ENTER();

    if (pExt->iDevice>0)
    {
        LOG(("skipping non-primary display %d", pExt->iDevice));
        return TRUE;
    }

#if 0
   /* Don't disable the extended video mode. This would only switch the video mode
    * to <current width> x <current height> x 0 bpp which is not what we want. And
    * even worse, it causes an disturbing additional mode switch */
   VideoPortWritePortUshort((PUSHORT)VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ENABLE);
   VideoPortWritePortUshort((PUSHORT)VBE_DISPI_IOPORT_DATA, VBE_DISPI_DISABLED);
#endif

    /* Tell the host that we no longer support graphics in the additions */
    /** @todo VBoxMPSetGraphicsCap(FALSE); */

    LOGF_LEAVE();
    return TRUE;
}

/* Called for IOCTL_VIDEO_MAP_VIDEO_MEMORY.
 * Maps FrameBuffer and video RAM to a caller's virtual adress space.
 */
BOOLEAN VBoxMPMapVideoMemory(PVBOXMP_DEVEXT pExt, PVIDEO_MEMORY pRequestedAddress,
                             PVIDEO_MEMORY_INFORMATION pMapInfo, PSTATUS_BLOCK pStatus)
{
    PHYSICAL_ADDRESS framebuffer;
    ULONG inIoSpace = 0;

    LOGF(("framebuffer offset %#x", pExt->ulFrameBufferOffset));

    framebuffer.QuadPart = VBoxCommonFromDeviceExt(pExt)->phVRAM.QuadPart + pExt->ulFrameBufferOffset;

    pMapInfo->VideoRamBase = pRequestedAddress->RequestedVirtualAddress;
    VBOXMPIOCTL_HIDE(pRequestedAddress);
    pMapInfo->VideoRamLength = pExt->pPrimary->u.primary.ulMaxFrameBufferSize;

    pStatus->Status = VideoPortMapMemory(pExt, framebuffer, &pMapInfo->VideoRamLength,
                                         &inIoSpace, &pMapInfo->VideoRamBase);

    if (NO_ERROR == pStatus->Status)
    {
        pMapInfo->FrameBufferBase = (PUCHAR)pMapInfo->VideoRamBase;
        pMapInfo->FrameBufferLength =
            VBoxMPXpdmCurrentVideoMode(pExt)->VisScreenHeight *
            VBoxMPXpdmCurrentVideoMode(pExt)->ScreenStride;

        pStatus->Information = sizeof(VIDEO_MEMORY_INFORMATION);

        /* Save the new framebuffer size */
        pExt->ulFrameBufferSize = pMapInfo->FrameBufferLength;
        HGSMIAreaInitialize(&pExt->areaDisplay, pMapInfo->FrameBufferBase,
                            pMapInfo->FrameBufferLength, pExt->ulFrameBufferOffset);
    }

    VBOXMPIOCTL_UNHIDE();
    LOGF_LEAVE();
    return NO_ERROR == pStatus->Status;
}

/* Called for IOCTL_VIDEO_UNMAP_VIDEO_MEMORY.
 * Unmaps previously mapped FrameBuffer and video RAM from caller's virtual adress space.
 */
BOOLEAN VBoxMPUnmapVideoMemory(PVBOXMP_DEVEXT pExt, PVIDEO_MEMORY VideoMemory, PSTATUS_BLOCK pStatus)
{
    LOGF_ENTER();

    HGSMIAreaClear(&pExt->areaDisplay);
    pStatus->Status = VideoPortUnmapMemory(pExt, VideoMemory->RequestedVirtualAddress, NULL);

    LOGF_LEAVE();
    return TRUE;
}

/* Called for IOCTL_VIDEO_SHARE_VIDEO_MEMORY.
 * Maps FrameBuffer as a linear frame buffer to a caller's virtual adress space. (obsolete).
 */
BOOLEAN VBoxMPShareVideoMemory(PVBOXMP_DEVEXT pExt, PVIDEO_SHARE_MEMORY pShareMem,
                               PVIDEO_SHARE_MEMORY_INFORMATION pShareMemInfo, PSTATUS_BLOCK pStatus)
{
    PHYSICAL_ADDRESS shareAddress;
    ULONG inIoSpace = 0;
    ULONG offset, size;
    PVOID virtualAddress;
    ULONG ulMaxFBSize;

    LOGF_ENTER();

    ulMaxFBSize = pExt->pPrimary->u.primary.ulMaxFrameBufferSize;
    offset = pShareMem->ViewOffset;
    size = pShareMem->ViewSize;
    virtualAddress = pShareMem->ProcessHandle;
    VBOXMPIOCTL_HIDE(pShareMem);

    if ((offset>ulMaxFBSize) || ((offset+size)>ulMaxFBSize))
    {
        WARN(("share failed offset:size(%#x:%#x) > %#x fb size.", offset, size, ulMaxFBSize));
        pStatus->Status = ERROR_INVALID_PARAMETER;
        return FALSE;
    }

    shareAddress.QuadPart = VBoxCommonFromDeviceExt(pExt)->phVRAM.QuadPart + pExt->ulFrameBufferOffset;

    pStatus->Status = VideoPortMapMemory(pExt, shareAddress, &size, &inIoSpace, &virtualAddress);

    if (NO_ERROR == pStatus->Status)
    {
        pShareMemInfo->SharedViewOffset = offset;
        pShareMemInfo->SharedViewSize = size;
        pShareMemInfo->VirtualAddress = virtualAddress;

        pStatus->Information = sizeof(VIDEO_SHARE_MEMORY_INFORMATION);
    }

    VBOXMPIOCTL_UNHIDE();
    LOGF_LEAVE();
    return NO_ERROR == pStatus->Status;
}

/* Called for IOCTL_VIDEO_UNSHARE_VIDEO_MEMORY.
 * Unmaps framebuffer previously mapped with IOCTL_VIDEO_SHARE_VIDEO_MEMORY.
 */
BOOLEAN VBoxMPUnshareVideoMemory(PVBOXMP_DEVEXT pExt, PVIDEO_SHARE_MEMORY pMem, PSTATUS_BLOCK pStatus)
{
    LOGF_ENTER();

    pStatus->Status = VideoPortUnmapMemory(pExt, pMem->RequestedVirtualAddress, pMem->ProcessHandle);

    LOGF_LEAVE();
    return TRUE;
}

/* Called for IOCTL_VIDEO_SET_CURRENT_MODE.
 * Sets adapter video mode.
 */
BOOLEAN VBoxMPSetCurrentMode(PVBOXMP_DEVEXT pExt, PVIDEO_MODE pMode, PSTATUS_BLOCK pStatus)
{
    ULONG RequestedMode;
    VIDEO_MODE_INFORMATION *pModeInfo;

    LOGF(("mode=%#x", pMode->RequestedMode));

    /* Get requested mode info */
    RequestedMode = pMode->RequestedMode & ~(VIDEO_MODE_NO_ZERO_MEMORY|VIDEO_MODE_MAP_MEM_LINEAR);
    if (RequestedMode!=pMode->RequestedMode)
    {
        WARN(("ignoring set VIDEO_MODE_NO_ZERO_MEMORY or VIDEO_MODE_MAP_MEM_LINEAR"));
    }

    pModeInfo = VBoxMPCmnGetVideoModeInfo(pExt, RequestedMode-1);
    if (!pModeInfo)
    {
        pStatus->Status = ERROR_INVALID_PARAMETER;
        return FALSE;
    }

    LOG(("screen [%d] mode %d width %d, height %d, bpp %d",
         pExt->iDevice, pModeInfo->ModeIndex, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight, pModeInfo->BitsPerPlane));

    /* Update device info */
    pExt->CurrentMode       = RequestedMode;
    pExt->CurrentModeWidth  = pModeInfo->VisScreenWidth;
    pExt->CurrentModeHeight = pModeInfo->VisScreenHeight;
    pExt->CurrentModeBPP    = pModeInfo->BitsPerPlane;

    if (pExt->iDevice>0)
    {
        LOG(("skipping non-primary display %d", pExt->iDevice));
        return TRUE;
    }

    /* Perform actual mode switch */
    VBoxVideoSetModeRegisters((USHORT)pModeInfo->VisScreenWidth, (USHORT)pModeInfo->VisScreenHeight,
                              (USHORT)pModeInfo->VisScreenWidth, (USHORT)pModeInfo->BitsPerPlane, 0, 0, 0);

    /** @todo read back from port to check if mode switch was successful */

    LOGF_LEAVE();
    return TRUE;
}

/* Called for IOCTL_VIDEO_QUERY_CURRENT_MODE.
 * Returns information about current video mode.
 */
BOOLEAN VBoxMPQueryCurrentMode(PVBOXMP_DEVEXT pExt, PVIDEO_MODE_INFORMATION pModeInfo, PSTATUS_BLOCK pStatus)
{
    LOGF_ENTER();

    pStatus->Information = sizeof(VIDEO_MODE_INFORMATION);

    VideoPortMoveMemory(pModeInfo, VBoxMPXpdmCurrentVideoMode(pExt), sizeof(VIDEO_MODE_INFORMATION));

    LOGF_LEAVE();
    return TRUE;
}

/* Called for IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES.
 * Returns count of supported video modes and structure size in bytes,
 * used by the following IOCTL_VIDEO_QUERY_AVAIL_MODES.
 */
BOOLEAN VBoxMPQueryNumAvailModes(PVBOXMP_DEVEXT pExt, PVIDEO_NUM_MODES pNumModes, PSTATUS_BLOCK pStatus)
{
    LOGF_ENTER();

    VBoxMPXpdmBuildVideoModesTable(pExt);

    pNumModes->NumModes = VBoxMPXpdmGetVideoModesCount(pExt);
    pNumModes->ModeInformationLength = sizeof(VIDEO_MODE_INFORMATION);
    pStatus->Information = sizeof(VIDEO_NUM_MODES);

    LOGF_LEAVE();
    return TRUE;
}

/* Called for IOCTL_VIDEO_QUERY_AVAIL_MODES.
 * Returns information about supported video modes.
 */
BOOLEAN VBoxMPQueryAvailModes(PVBOXMP_DEVEXT pExt, PVIDEO_MODE_INFORMATION pModes, PSTATUS_BLOCK pStatus)
{
    LOGF_ENTER();

    ULONG ulSize = VBoxMPXpdmGetVideoModesCount(pExt)*sizeof(VIDEO_MODE_INFORMATION);
    pStatus->Information = ulSize;
    VideoPortMoveMemory(pModes, VBoxMPCmnGetVideoModeInfo(pExt, 0), ulSize);

    LOGF_LEAVE();
    return TRUE;
}

/* Called for IOCTL_VIDEO_SET_COLOR_REGISTERS.
 * Sets adapter's color registers.
 */
BOOLEAN VBoxMPSetColorRegisters(PVBOXMP_DEVEXT pExt, PVIDEO_CLUT pClut, PSTATUS_BLOCK pStatus)
{
    RT_NOREF(pExt);
    LONG entry;

    LOGF_ENTER();

    if (pClut->FirstEntry+pClut->NumEntries > 256)
    {
        pStatus->Status = ERROR_INVALID_PARAMETER;
        return FALSE;
    }

    for (entry=pClut->FirstEntry; entry<pClut->FirstEntry+pClut->NumEntries; ++entry)
    {
      VBVO_PORT_WRITE_U8(VBE_DISPI_IOPORT_DAC_WRITE_INDEX, (UCHAR)entry);
      VBVO_PORT_WRITE_U8(VBE_DISPI_IOPORT_DAC_DATA, pClut->LookupTable[entry].RgbArray.Red);
      VBVO_PORT_WRITE_U8(VBE_DISPI_IOPORT_DAC_DATA, pClut->LookupTable[entry].RgbArray.Green);
      VBVO_PORT_WRITE_U8(VBE_DISPI_IOPORT_DAC_DATA, pClut->LookupTable[entry].RgbArray.Blue);
    }

    LOGF_LEAVE();
    return TRUE;
}

/* Called for IOCTL_VIDEO_SET_POINTER_ATTR.
 * Sets pointer attributes.
 */
BOOLEAN VBoxMPSetPointerAttr(PVBOXMP_DEVEXT pExt, PVIDEO_POINTER_ATTRIBUTES pPointerAttrs, uint32_t cbLen, PSTATUS_BLOCK pStatus)
{
    BOOLEAN fRc;

    LOGF_ENTER();

    if (VBoxQueryHostWantsAbsolute())
    {
        fRc = VBoxMPCmnUpdatePointerShape(VBoxCommonFromDeviceExt(pExt), pPointerAttrs, cbLen);
    }
    else
    {
        LOG(("Fallback to sw pointer."));
        fRc = FALSE;
    }

    if (!fRc)
    {
        pStatus->Status = ERROR_INVALID_FUNCTION;
    }

    LOGF_LEAVE();
    return fRc;
}

/**
 * Called by IOCTL_VIDEO_SET_POINTER_POSITION to report the current mouse cursor position to the host.
 *
 * @returns \c TRUE on success, \c FALSE on failure.
 * @param   pExt                Device extension to use.
 * @param   pPos                Mouse pointer position to report.
 * @param   pStatus             Status to return.
 */
BOOLEAN VBoxMPReportCursorPosition(PVBOXMP_DEVEXT pExt, PVIDEO_POINTER_POSITION pPos, PSTATUS_BLOCK pStatus)
{
    LOGF_ENTER();

    BOOLEAN fRc;
    if (VBoxQueryHostWantsAbsolute())
    {
        fRc = RT_SUCCESS(VBoxMPCmnReportCursorPosition(VBoxCommonFromDeviceExt(pExt), pPos));
    }
    else
    {
        LOG(("Fallback to sw pointer."));
        fRc = FALSE;
    }

    if (!fRc)
        pStatus->Status = ERROR_INVALID_FUNCTION;

    LOGF_LEAVE();
    return fRc;
}

/* Called for IOCTL_VIDEO_ENABLE_POINTER/IOCTL_VIDEO_DISABLE_POINTER.
 * Hides pointer or makes it visible depending on bEnable value passed.
 */
BOOLEAN VBoxMPEnablePointer(PVBOXMP_DEVEXT pExt, BOOLEAN bEnable, PSTATUS_BLOCK pStatus)
{
    BOOLEAN fRc = TRUE;
    LOGF_ENTER();

    if (VBoxQueryHostWantsAbsolute())
    {
        /* Check if it's not shown already. */
        if (bEnable == pExt->pPrimary->u.primary.fMouseHidden)
        {
            VIDEO_POINTER_ATTRIBUTES attrs;

            /* Visible and No Shape means show the pointer, 0 means hide pointer.
             * It's enough to init only this field.
             */
            attrs.Enable = bEnable ? VBOX_MOUSE_POINTER_VISIBLE:0;


            /* Pass info to the host. */
            fRc = VBoxMPCmnUpdatePointerShape(VBoxCommonFromDeviceExt(pExt), &attrs, sizeof(attrs));

            if (fRc)
            {
                /* Update device state. */
                pExt->pPrimary->u.primary.fMouseHidden = !bEnable;
            }
        }
    }
    else
    {
        fRc = FALSE;
    }

    if (!fRc)
    {
        pStatus->Status = ERROR_INVALID_FUNCTION;
    }

    LOGF_LEAVE();
    return fRc;
}

/* Called for IOCTL_VIDEO_QUERY_POINTER_POSITION.
 * Query pointer position.
 */
BOOLEAN VBoxMPQueryPointerPosition(PVBOXMP_DEVEXT pExt, PVIDEO_POINTER_POSITION pPos, PSTATUS_BLOCK pStatus)
{
    uint16_t PosX, PosY;
    BOOLEAN fRc = TRUE;
    LOGF_ENTER();

    if (VBoxQueryPointerPos(&PosX, &PosY))
    {
        PVIDEO_MODE_INFORMATION pMode = VBoxMPXpdmCurrentVideoMode(pExt);
        /* map from 0xFFFF to the current resolution */
        pPos->Column = (SHORT)(PosX / (0xFFFF / pMode->VisScreenWidth));
        pPos->Row    = (SHORT)(PosY / (0xFFFF / pMode->VisScreenHeight));

        pStatus->Information = sizeof(VIDEO_POINTER_POSITION);
    }
    else
    {
        pStatus->Status = ERROR_INVALID_FUNCTION;
        fRc = FALSE;
    }

    LOGF_LEAVE();
    return fRc;
}

/* Called for IOCTL_VIDEO_QUERY_POINTER_CAPABILITIES.
 * Query supported hardware pointer feaures.
 * Note: we always return all caps we could ever support,
 * related functions will return errors if host doesn't accept pointer integration
 * and force display driver to enter software fallback codepath.
 */
BOOLEAN VBoxMPQueryPointerCapabilities(PVBOXMP_DEVEXT pExt, PVIDEO_POINTER_CAPABILITIES pCaps, PSTATUS_BLOCK pStatus)
{
    RT_NOREF(pExt);
    LOGF_ENTER();

    pStatus->Information = sizeof(VIDEO_POINTER_CAPABILITIES);

    pCaps->Flags = VIDEO_MODE_ASYNC_POINTER | VIDEO_MODE_COLOR_POINTER | VIDEO_MODE_MONO_POINTER;
    /* Up to 64x64 shapes */
    pCaps->MaxWidth  = 64;
    pCaps->MaxHeight = 64;
    /* Not used by our display driver */
    pCaps->HWPtrBitmapStart = ~(ULONG)0;
    pCaps->HWPtrBitmapEnd   = ~(ULONG)0;

    LOGF_LEAVE();
    return TRUE;
}

/* Called for IOCTL_VIDEO_VBVA_ENABLE.
 * Display driver is ready to switch to VBVA operation mode.
 */
BOOLEAN VBoxMPVBVAEnable(PVBOXMP_DEVEXT pExt, BOOLEAN bEnable, VBVAENABLERESULT *pResult, PSTATUS_BLOCK pStatus)
{
    int rc;
    BOOLEAN fRc = TRUE;
    LOGF_ENTER();

    rc = VBoxVbvaEnable(pExt, bEnable, pResult);

    if (RT_SUCCESS(rc))
    {
        pStatus->Information = sizeof(VBVAENABLERESULT);
    }
    else
    {
        pStatus->Status = ERROR_INVALID_FUNCTION;
        fRc = FALSE;
    }

    LOGF_LEAVE();
    return fRc;
}

/* Called for IOCTL_VIDEO_VBOX_SETVISIBLEREGION.
 * Sends visible regions information to the host.
 */
BOOLEAN VBoxMPSetVisibleRegion(uint32_t cRects, RTRECT *pRects, PSTATUS_BLOCK pStatus)
{
    int rc;
    BOOLEAN fRc = FALSE;
    LOGF_ENTER();

    VMMDevVideoSetVisibleRegion *req = NULL;
    rc = VbglR0GRAlloc((VMMDevRequestHeader **)&req, sizeof(VMMDevVideoSetVisibleRegion) + (cRects-1)*sizeof(RTRECT),
                      VMMDevReq_VideoSetVisibleRegion);

    if (RT_SUCCESS(rc))
    {
        req->cRect = cRects;
        memcpy(&req->Rect, pRects, cRects*sizeof(RTRECT));
        rc = VbglR0GRPerform(&req->header);

        if (RT_SUCCESS(rc))
        {
            fRc=TRUE;
        }

        VbglR0GRFree(&req->header);
    }
    else
    {
        WARN(("VbglR0GRAlloc rc = %#xrc", rc));
    }

    if (!fRc)
    {
        pStatus->Status = ERROR_INVALID_FUNCTION;
    }

    LOGF_LEAVE();
    return fRc;
}

/* Called for IOCTL_VIDEO_HGSMI_QUERY_PORTPROCS.
 * Returns video port api function pointers.
 */
BOOLEAN VBoxMPHGSMIQueryPortProcs(PVBOXMP_DEVEXT pExt, HGSMIQUERYCPORTPROCS *pProcs, PSTATUS_BLOCK pStatus)
{
    BOOLEAN fRc = TRUE;
    LOGF_ENTER();

    if (VBoxCommonFromDeviceExt(pExt)->bHGSMI)
    {
        pProcs->pContext = pExt->pPrimary;
        pProcs->VideoPortProcs = pExt->pPrimary->u.primary.VideoPortProcs;

        pStatus->Information = sizeof(HGSMIQUERYCPORTPROCS);
    }
    else
    {
        pStatus->Status = ERROR_INVALID_FUNCTION;
        fRc=FALSE;
    }

    LOGF_LEAVE();
    return fRc;
}

/* Called for IOCTL_VIDEO_HGSMI_QUERY_CALLBACKS.
 * Returns HGSMI related callbacks.
 */
BOOLEAN VBoxMPHGSMIQueryCallbacks(PVBOXMP_DEVEXT pExt, HGSMIQUERYCALLBACKS *pCallbacks, PSTATUS_BLOCK pStatus)
{
    BOOLEAN fRc = TRUE;
    LOGF_ENTER();

    if (VBoxCommonFromDeviceExt(pExt)->bHGSMI)
    {
        pCallbacks->hContext = VBoxCommonFromDeviceExt(pExt);
        pCallbacks->pfnCompletionHandler = VBoxMPHGSMIHostCmdCompleteCB;
        pCallbacks->pfnRequestCommandsHandler = VBoxMPHGSMIHostCmdRequestCB;

        pStatus->Information = sizeof(HGSMIQUERYCALLBACKS);
    }
    else
    {
        pStatus->Status = ERROR_INVALID_FUNCTION;
        fRc=FALSE;
    }


    LOGF_LEAVE();
    return fRc;
}

/* Called for IOCTL_VIDEO_QUERY_HGSMI_INFO.
 * Returns hgsmi info for this adapter.
 */
BOOLEAN VBoxMPQueryHgsmiInfo(PVBOXMP_DEVEXT pExt, QUERYHGSMIRESULT *pResult, PSTATUS_BLOCK pStatus)
{
    BOOLEAN fRc = TRUE;
    LOGF_ENTER();

    if (VBoxCommonFromDeviceExt(pExt)->bHGSMI)
    {
        pResult->iDevice = pExt->iDevice;
        pResult->ulFlags = 0;
        pResult->areaDisplay = pExt->areaDisplay;
        pResult->u32DisplayInfoSize   = VBVA_DISPLAY_INFORMATION_SIZE;
        pResult->u32MinVBVABufferSize = VBVA_MIN_BUFFER_SIZE;
        pResult->IOPortGuestCommand = VBoxCommonFromDeviceExt(pExt)->guestCtx.port;

        pStatus->Information = sizeof(QUERYHGSMIRESULT);
    }
    else
    {
        pStatus->Status = ERROR_INVALID_FUNCTION;
        fRc=FALSE;
    }

    LOGF_LEAVE();
    return fRc;
}

/* Called for IOCTL_VIDEO_HGSMI_HANDLER_ENABLE.
 * Enables HGSMI miniport channel.
 */
BOOLEAN VBoxMPHgsmiHandlerEnable(PVBOXMP_DEVEXT pExt, HGSMIHANDLERENABLE *pChannel, PSTATUS_BLOCK pStatus)
{
    BOOLEAN fRc = TRUE;
    LOGF_ENTER();

    if (VBoxCommonFromDeviceExt(pExt)->bHGSMI)
    {
        int rc = VBoxVbvaChannelDisplayEnable(VBoxCommonFromDeviceExt(pExt), pExt->iDevice, pChannel->u8Channel);
        if (RT_FAILURE(rc))
        {
            pStatus->Status = ERROR_INVALID_NAME;
            fRc=FALSE;
        }
    }
    else
    {
        pStatus->Status = ERROR_INVALID_FUNCTION;
        fRc=FALSE;
    }

    LOGF_LEAVE();
    return fRc;
}

#ifdef VBOX_WITH_VIDEOHWACCEL
/* Called for IOCTL_VIDEO_VHWA_QUERY_INFO.
 * Returns framebuffer offset.
 */
BOOLEAN VBoxMPVhwaQueryInfo(PVBOXMP_DEVEXT pExt, VHWAQUERYINFO *pInfo, PSTATUS_BLOCK pStatus)
{
    BOOLEAN fRc = TRUE;
    LOGF_ENTER();

    if (VBoxCommonFromDeviceExt(pExt)->bHGSMI)
    {
        pInfo->offVramBase = (ULONG_PTR)pExt->ulFrameBufferOffset;

        pStatus->Information = sizeof (VHWAQUERYINFO);
    }
    else
    {
        pStatus->Status = ERROR_INVALID_FUNCTION;
        fRc=FALSE;
    }

    LOGF_LEAVE();
    return fRc;
}
#endif

BOOLEAN VBoxMPQueryRegistryFlags(PVBOXMP_DEVEXT pExt, ULONG *pulFlags, PSTATUS_BLOCK pStatus)
{
    BOOLEAN fRc = TRUE;
    LOGF_ENTER();

    VBOXMPCMNREGISTRY Registry;

    int rc = VBoxMPCmnRegInit(pExt, &Registry);
    VBOXMP_WARN_VPS_NOBP(rc);

    if (rc == NO_ERROR)
    {
        uint32_t u32Flags = 0;
        rc = VBoxMPCmnRegQueryDword(Registry, L"VBoxVideoFlags", &u32Flags);
        VBOXMP_WARN_VPS_NOBP(rc);
        if (rc != NO_ERROR)
        {
            u32Flags = 0;
        }

        LOG(("Registry flags 0x%08X", u32Flags));
        *pulFlags = u32Flags;
        pStatus->Information = sizeof(ULONG);
    }

    rc = VBoxMPCmnRegFini(Registry);
    VBOXMP_WARN_VPS_NOBP(rc);

    LOGF_LEAVE();
    return fRc;
}
