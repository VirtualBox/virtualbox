/* $Id: VBoxDispMini.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Display driver, helper functions which interacts with our miniport driver
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

#include "VBoxDisp.h"
#include "VBoxDispMini.h"
#include <iprt/asm.h>

/* Returns if given video mode is supported by display driver */
static BOOL VBoxDispVideoModeSupported(const PVIDEO_MODE_INFORMATION pMode)
{
    if ((pMode->NumberOfPlanes==1)
        && (pMode->AttributeFlags & VIDEO_MODE_GRAPHICS)
        && !(pMode->AttributeFlags & VIDEO_MODE_BANKED)
        && (pMode->BitsPerPlane==8 || pMode->BitsPerPlane==16 || pMode->BitsPerPlane==24 || pMode->BitsPerPlane==32))
    {
        return TRUE;
    }
    return FALSE;
}

/* Returns list video modes supported by both miniport and display driver.
 * Note: caller is resposible to free up ppModesTable.
 */
int VBoxDispMPGetVideoModes(HANDLE hDriver, PVIDEO_MODE_INFORMATION *ppModesTable, ULONG *pcModes)
{
    DWORD dwrc;
    VIDEO_NUM_MODES numModes;
    ULONG cbReturned, i, j, cSupportedModes;
    PVIDEO_MODE_INFORMATION pMiniportModes, pMode;

    LOGF_ENTER();

    /* Get number of video modes supported by miniport */
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES, NULL, 0,
                              &numModes, sizeof(VIDEO_NUM_MODES), &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    if (numModes.ModeInformationLength != sizeof(VIDEO_MODE_INFORMATION))
    {
        WARN(("sizeof(VIDEO_MODE_INFORMATION) differs for miniport and display drivers. "
              "Check that both are compiled with same ddk version!"));
    }

    /* Allocate temp buffer */
    pMiniportModes = (PVIDEO_MODE_INFORMATION)
                     EngAllocMem(0, numModes.NumModes*numModes.ModeInformationLength, MEM_ALLOC_TAG);

    if (!pMiniportModes)
    {
        WARN(("not enough memory!"));
        return VERR_NO_MEMORY;
    }

    /* Get video modes supported by miniport */
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_QUERY_AVAIL_MODES, NULL, 0,
                              pMiniportModes, numModes.NumModes*numModes.ModeInformationLength, &cbReturned);
    if (dwrc != NO_ERROR)
    {
        EngFreeMem(pMiniportModes);
        VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    }

    /* Check which of miniport modes are supprted by display driver.
     * Note: size of VIDEO_MODE_INFORMATION is returned by miniport driver in numModes.ModeInformationLength,
     *       it might be different from the one we have here.
     */
    cSupportedModes = 0;
    pMode = pMiniportModes;
    for (i=0; i<numModes.NumModes; ++i)
    {
        /*sanity check*/
        if (pMode->Length != sizeof(VIDEO_MODE_INFORMATION))
        {
            WARN(("Unexpected mode len %i expected %i!", pMode->Length, sizeof(VIDEO_MODE_INFORMATION)));
        }

        if (VBoxDispVideoModeSupported(pMode))
        {
            cSupportedModes++;
        }
        else
        {
            pMode->Length = 0;
        }

        pMode = (PVIDEO_MODE_INFORMATION) (((PUCHAR)pMode)+numModes.ModeInformationLength);
    }
    *pcModes = cSupportedModes;

    if (0==cSupportedModes)
    {
        WARN(("0 video modes supported!"));
        EngFreeMem(pMiniportModes);
        return VERR_NOT_SUPPORTED;
    }

    /* Allocate and zero output buffer */
    *ppModesTable = (PVIDEO_MODE_INFORMATION)
                    EngAllocMem(FL_ZERO_MEMORY, cSupportedModes*sizeof(VIDEO_MODE_INFORMATION), MEM_ALLOC_TAG);

    if (!*ppModesTable)
    {
        WARN(("not enough memory!"));
        EngFreeMem(pMiniportModes);
        return VERR_NO_MEMORY;
    }

    /* Copy supported modes to output buffer */
    pMode = pMiniportModes;
    for (j=0, i=0; i<numModes.NumModes; ++i)
    {
        if (pMode->Length != 0)
        {
            memcpy(&(*ppModesTable)[j], pMode, numModes.ModeInformationLength);
            ++j;
        }

        pMode = (PVIDEO_MODE_INFORMATION) (((PUCHAR)pMode)+numModes.ModeInformationLength);
    }
    Assert(j==cSupportedModes);

    /* Free temp buffer */
    EngFreeMem(pMiniportModes);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

/* Query miniport for mouse pointer caps */
int VBoxDispMPGetPointerCaps(HANDLE hDriver, PVIDEO_POINTER_CAPABILITIES pCaps)
{
    DWORD dwrc;
    ULONG cbReturned;

    LOGF_ENTER();

    memset(pCaps, 0, sizeof(VIDEO_POINTER_CAPABILITIES));
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_QUERY_POINTER_CAPABILITIES, NULL, 0,
                              pCaps, sizeof(VIDEO_POINTER_CAPABILITIES), &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    VBOX_WARN_IOCTLCB_RETRC("IOCTL_VIDEO_QUERY_POINTER_CAPABILITIES", cbReturned, sizeof(VIDEO_POINTER_CAPABILITIES), VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

/* Set device mode */
int VBoxDispMPSetCurrentMode(HANDLE hDriver, ULONG ulMode)
{
    DWORD dwrc;
    ULONG cbReturned;
    VIDEO_MODE mode;
    LOGF_ENTER();

    mode.RequestedMode = ulMode;
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_SET_CURRENT_MODE, &mode, sizeof(VIDEO_MODE), NULL, 0, &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

/* Map device framebuffer and VRAM to our virtual address space */
int VBoxDispMPMapMemory(PVBOXDISPDEV pDev, PVIDEO_MEMORY_INFORMATION pMemInfo)
{
    DWORD dwrc;
    ULONG cbReturned;
    VIDEO_MEMORY vMem;
    VIDEO_MEMORY_INFORMATION vMemInfo;
    LOGF_ENTER();

    Assert(!pDev->memInfo.FrameBufferBase && !pDev->memInfo.VideoRamBase);

    vMem.RequestedVirtualAddress = NULL;
    dwrc = EngDeviceIoControl(pDev->hDriver, IOCTL_VIDEO_MAP_VIDEO_MEMORY, &vMem, sizeof(vMem), &vMemInfo, sizeof(vMemInfo), &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    VBOX_WARN_IOCTLCB_RETRC("IOCTL_VIDEO_MAP_VIDEO_MEMORY", cbReturned, sizeof(vMemInfo), VERR_DEV_IO_ERROR);

    if (vMemInfo.FrameBufferBase != vMemInfo.VideoRamBase)
    {
        WARN(("FrameBufferBase!=VideoRamBase."));
        return VERR_GENERAL_FAILURE;
    }

    /* Check if we can access mapped memory */
    uint32_t magic = (*(ULONG *)vMemInfo.FrameBufferBase == 0xDEADF00D) ? 0xBAADF00D : 0xDEADF00D;

    ASMAtomicWriteU32((uint32_t *)vMemInfo.FrameBufferBase, magic);
    if (ASMAtomicReadU32((uint32_t *)vMemInfo.FrameBufferBase) != magic)
    {
        WARN(("can't write to framebuffer memory!"));
        return VERR_GENERAL_FAILURE;
    }

    memcpy(pMemInfo, &vMemInfo, sizeof(vMemInfo));

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int VBoxDispMPUnmapMemory(PVBOXDISPDEV pDev)
{
    DWORD dwrc;
    ULONG cbReturned;
    VIDEO_MEMORY vMem;
    LOGF_ENTER();

    vMem.RequestedVirtualAddress = pDev->memInfo.VideoRamBase;
    dwrc = EngDeviceIoControl(pDev->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY, &vMem, sizeof(vMem), NULL, 0, &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    memset(&pDev->memInfo, 0, sizeof(VIDEO_MEMORY_INFORMATION));

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int VBoxDispMPQueryHGSMIInfo(HANDLE hDriver, QUERYHGSMIRESULT *pInfo)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    memset(pInfo, 0, sizeof(QUERYHGSMIRESULT));
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_QUERY_HGSMI_INFO, NULL, 0,
                              pInfo, sizeof(QUERYHGSMIRESULT), &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    VBOX_WARN_IOCTLCB_RETRC("IOCTL_VIDEO_QUERY_HGSMI_INFO", cbReturned, sizeof(QUERYHGSMIRESULT), VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int VBoxDispMPQueryHGSMICallbacks(HANDLE hDriver, HGSMIQUERYCALLBACKS *pCallbacks)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    memset(pCallbacks, 0, sizeof(HGSMIQUERYCALLBACKS));
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_HGSMI_QUERY_CALLBACKS, NULL, 0,
                              pCallbacks, sizeof(HGSMIQUERYCALLBACKS), &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    VBOX_WARN_IOCTLCB_RETRC("IOCTL_VIDEO_HGSMI_QUERY_CALLBACKS", cbReturned, sizeof(HGSMIQUERYCALLBACKS), VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int VBoxDispMPHGSMIQueryPortProcs(HANDLE hDriver, HGSMIQUERYCPORTPROCS *pPortProcs)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    memset(pPortProcs, 0, sizeof(HGSMIQUERYCPORTPROCS));
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_HGSMI_QUERY_PORTPROCS, NULL, 0,
                              pPortProcs, sizeof(HGSMIQUERYCPORTPROCS), &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    VBOX_WARN_IOCTLCB_RETRC("IOCTL_VIDEO_HGSMI_QUERY_PORTPROCS", cbReturned, sizeof(HGSMIQUERYCPORTPROCS), VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

#ifdef VBOX_WITH_VIDEOHWACCEL
int VBoxDispMPVHWAQueryInfo(HANDLE hDriver, VHWAQUERYINFO *pInfo)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    memset(pInfo, 0, sizeof(VHWAQUERYINFO));
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_VHWA_QUERY_INFO, NULL, 0,
                              pInfo, sizeof(VHWAQUERYINFO), &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    VBOX_WARN_IOCTLCB_RETRC("IOCTL_VIDEO_VHWA_QUERY_INFO", cbReturned, sizeof(VHWAQUERYINFO), VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}
#endif

int VBoxDispMPSetColorRegisters(HANDLE hDriver, PVIDEO_CLUT pClut, DWORD cbClut)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_SET_COLOR_REGISTERS, pClut, cbClut, NULL, 0, &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int VBoxDispMPDisablePointer(HANDLE hDriver)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_DISABLE_POINTER, NULL, 0, NULL, 0, &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int VBoxDispMPSetPointerPosition(HANDLE hDriver, PVIDEO_POINTER_POSITION pPos)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_SET_POINTER_POSITION, pPos, sizeof(VIDEO_POINTER_POSITION),
                              NULL, 0, &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int VBoxDispMPSetPointerAttrs(PVBOXDISPDEV pDev)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    Assert(pDev->pointer.pAttrs);

    dwrc = EngDeviceIoControl(pDev->hDriver, IOCTL_VIDEO_SET_POINTER_ATTR, pDev->pointer.pAttrs, pDev->pointer.cbAttrs,
                              NULL, 0, &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int VBoxDispMPSetVisibleRegion(HANDLE hDriver, PRTRECT pRects, DWORD cRects)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_VBOX_SETVISIBLEREGION, pRects, cRects*sizeof(RTRECT),
                              NULL, 0, &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int VBoxDispMPResetDevice(HANDLE hDriver)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_RESET_DEVICE, NULL, 0, NULL, 0, &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int VBoxDispMPShareVideoMemory(HANDLE hDriver, PVIDEO_SHARE_MEMORY pSMem, PVIDEO_SHARE_MEMORY_INFORMATION pSMemInfo)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_SHARE_VIDEO_MEMORY, pSMem, sizeof(VIDEO_SHARE_MEMORY),
                              pSMemInfo, sizeof(VIDEO_SHARE_MEMORY_INFORMATION), &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    VBOX_WARN_IOCTLCB_RETRC("IOCTL_VIDEO_SHARE_VIDEO_MEMORY", cbReturned,
                            sizeof(VIDEO_SHARE_MEMORY_INFORMATION), VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int VBoxDispMPUnshareVideoMemory(HANDLE hDriver, PVIDEO_SHARE_MEMORY pSMem)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_UNSHARE_VIDEO_MEMORY, pSMem, sizeof(VIDEO_SHARE_MEMORY),
                              NULL, 0, &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int VBoxDispMPQueryRegistryFlags(HANDLE hDriver, ULONG *pulFlags)
{
    DWORD dwrc;
    ULONG cbReturned;
    ULONG ulInfoLevel;
    LOGF_ENTER();

    *pulFlags = 0;
    ulInfoLevel = VBOXVIDEO_INFO_LEVEL_REGISTRY_FLAGS;
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_QUERY_VBOXVIDEO_INFO, &ulInfoLevel, sizeof(DWORD),
                              pulFlags, sizeof(DWORD), &cbReturned);
    VBOX_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    VBOX_WARN_IOCTLCB_RETRC("IOCTL_VIDEO_QUERY_INFO", cbReturned, sizeof(DWORD), VERR_DEV_IO_ERROR);

    if (*pulFlags != 0)
        LogRel(("VBoxDisp: video flags 0x%08X\n", *pulFlags));

    LOGF_LEAVE();
    return VINF_SUCCESS;
}
