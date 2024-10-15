/* $Id: VBoxMPCommon.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Miniport common utils
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

#include "VBoxMPCommon.h"
#include <VBoxVideoVBE.h>

int VBoxMPCmnMapAdapterMemory(PVBOXMP_COMMON pCommon, void **ppv, uint32_t ulOffset, uint32_t ulSize)
{
    PVBOXMP_DEVEXT pPEXT = VBoxCommonToPrimaryExt(pCommon);

    LOGF(("0x%08X[0x%X]", ulOffset, ulSize));

    if (!ulSize)
    {
        WARN(("Illegal length 0!"));
        return ERROR_INVALID_PARAMETER;
    }

    PHYSICAL_ADDRESS FrameBuffer;
    FrameBuffer.QuadPart = VBoxCommonFromDeviceExt(pPEXT)->phVRAM.QuadPart + ulOffset;

    PVOID VideoRamBase = NULL;
    ULONG VideoRamLength = ulSize;
    VP_STATUS Status;
#ifndef VBOX_WITH_WDDM
    ULONG inIoSpace = 0;

    Status = VideoPortMapMemory(pPEXT, FrameBuffer, &VideoRamLength, &inIoSpace, &VideoRamBase);
#else
    NTSTATUS ntStatus = pPEXT->u.primary.DxgkInterface.DxgkCbMapMemory(pPEXT->u.primary.DxgkInterface.DeviceHandle,
            FrameBuffer,
            VideoRamLength,
            FALSE, /* IN BOOLEAN InIoSpace */
            FALSE, /* IN BOOLEAN MapToUserMode */
            MmNonCached, /* IN MEMORY_CACHING_TYPE CacheType */
            &VideoRamBase /*OUT PVOID *VirtualAddress*/
            );
    Assert(ntStatus == STATUS_SUCCESS);
    /* this is what VideoPortMapMemory returns according to the docs */
    Status = ntStatus == STATUS_SUCCESS ? NO_ERROR : ERROR_INVALID_PARAMETER;
#endif

    if (Status == NO_ERROR)
    {
        *ppv = VideoRamBase;
    }

    LOGF(("rc = %d", Status));

    return (Status==NO_ERROR) ? VINF_SUCCESS:VERR_INVALID_PARAMETER;
}

void VBoxMPCmnUnmapAdapterMemory(PVBOXMP_COMMON pCommon, void **ppv)
{
    LOGF_ENTER();

    PVBOXMP_DEVEXT pPEXT = VBoxCommonToPrimaryExt(pCommon);

    if (*ppv)
    {
#ifndef VBOX_WITH_WDDM
        VP_STATUS Status;
        Status = VideoPortUnmapMemory(pPEXT, *ppv, NULL);
        VBOXMP_WARN_VPS(Status);
#else
        NTSTATUS ntStatus;
        ntStatus = pPEXT->u.primary.DxgkInterface.DxgkCbUnmapMemory(pPEXT->u.primary.DxgkInterface.DeviceHandle, *ppv);
        Assert(ntStatus == STATUS_SUCCESS);
#endif
    }

    *ppv = NULL;

    LOGF_LEAVE();
}

bool VBoxMPCmnSyncToVideoIRQ(PVBOXMP_COMMON pCommon, PFNVIDEOIRQSYNC pfnSync, void *pvUser)
{
    PVBOXMP_DEVEXT pPEXT = VBoxCommonToPrimaryExt(pCommon);
    PMINIPORT_SYNCHRONIZE_ROUTINE pfnSyncMiniport = (PMINIPORT_SYNCHRONIZE_ROUTINE) pfnSync;

#ifndef VBOX_WITH_WDDM
    return !!VideoPortSynchronizeExecution(pPEXT, VpMediumPriority, pfnSyncMiniport, pvUser);
#else
    BOOLEAN fRet;
    DXGKCB_SYNCHRONIZE_EXECUTION pfnDxgkCbSync = pPEXT->u.primary.DxgkInterface.DxgkCbSynchronizeExecution;
    HANDLE hDev = pPEXT->u.primary.DxgkInterface.DeviceHandle;
    NTSTATUS ntStatus = pfnDxgkCbSync(hDev, pfnSyncMiniport, pvUser, 0, &fRet);
    AssertReturn(ntStatus == STATUS_SUCCESS, false);
    return !!fRet;
#endif
}

bool VBoxMPCmnUpdatePointerShape(PVBOXMP_COMMON pCommon, PVIDEO_POINTER_ATTRIBUTES pAttrs, uint32_t cbLength)
{
    const uint32_t fFlags = pAttrs->Enable & 0x0000FFFF;
    const uint32_t cHotX = (pAttrs->Enable >> 16) & 0xFF;
    const uint32_t cHotY = (pAttrs->Enable >> 24) & 0xFF;
    const uint32_t cWidth = pAttrs->Width;
    const uint32_t cHeight = pAttrs->Height;
    uint8_t *pPixels = &pAttrs->Pixels[0];

    int rc = VBoxHGSMIUpdatePointerShape(&pCommon->guestCtx,
                                         fFlags, cHotX, cHotY,
                                         cWidth, cHeight, pPixels,
                                         cbLength - sizeof(VIDEO_POINTER_ATTRIBUTES));
    return RT_SUCCESS(rc);
}

/**
 * Reports the current mouse cursor position to the host and optionally retrieves the current position from the host.
 *
 * @returns VBox status code.
 * @param   pCommon             VBoxMP common context to use.
 * @param   pPos                Mouse position to report to the host.
 * @param   pxHost              X position reported from the host. Optional and can be NULL.
 * @param   pyHost              Y position reported from the host. Optional and can be NULL.
 */
int VBoxMPCmnReportCursorPositionEx(PVBOXMP_COMMON pCommon, PVIDEO_POINTER_POSITION pPos, uint32_t *pxHost, uint32_t *pyHost)
{
    return VBoxHGSMIReportCursorPosition(&pCommon->guestCtx, true /* fReport */, pPos->Column, pPos->Row, pxHost, pyHost);
}

/**
 * Reports the current mouse cursor position to the host.
 *
 * @returns VBox status code.
 * @param   pCommon             VBoxMP common context to use.
 * @param   pPos                Mouse position to report to the host.
 */
int VBoxMPCmnReportCursorPosition(PVBOXMP_COMMON pCommon, PVIDEO_POINTER_POSITION pPos)
{
    return VBoxMPCmnReportCursorPositionEx(pCommon, pPos, NULL /* pxHost */, NULL /* pyHost */);
}

