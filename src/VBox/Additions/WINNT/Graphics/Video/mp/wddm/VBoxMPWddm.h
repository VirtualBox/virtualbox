/* $Id: VBoxMPWddm.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPWddm_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPWddm_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#define VBOX_WDDM_DRIVERNAME L"VBoxWddm"

#include "common/VBoxMPUtils.h"
#include "common/VBoxMPDevExt.h"
#include "../../common/VBoxVideoTools.h"

//#define VBOXWDDM_DEBUG_VIDPN

#define VBOXWDDM_CFG_DRV_DEFAULT                        0
#define VBOXWDDM_CFG_DRV_SECONDARY_TARGETS_CONNECTED    1

#define VBOXWDDM_CFG_DRVTARGET_CONNECTED                1

#define VBOXWDDM_CFG_LOG_UM_BACKDOOR 0x00000001
#define VBOXWDDM_CFG_LOG_UM_DBGPRINT 0x00000002
#define VBOXWDDM_CFG_STR_LOG_UM L"VBoxLogUm"
#define VBOXWDDM_CFG_STR_RATE L"RefreshRate"

#define VBOXWDDM_REG_DRV_FLAGS_NAME L"VBoxFlags"
#define VBOXWDDM_REG_DRV_DISPFLAGS_PREFIX L"VBoxDispFlags"

#define VBOXWDDM_REG_DRVKEY_PREFIX L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Class\\"

#define VBOXWDDM_REG_DISPLAYSETTINGSVIDEOKEY L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video\\"
#define VBOXWDDM_REG_DISPLAYSETTINGSVIDEOKEY_SUBKEY L"\\Video"


#define VBOXWDDM_REG_DISPLAYSETTINGSKEY_PREFIX_VISTA L"\\Registry\\Machine\\System\\CurrentControlSet\\Hardware Profiles\\Current\\System\\CurrentControlSet\\Control\\VIDEO\\"
#define VBOXWDDM_REG_DISPLAYSETTINGSKEY_PREFIX_WIN7 L"\\Registry\\Machine\\System\\CurrentControlSet\\Hardware Profiles\\UnitedVideo\\CONTROL\\VIDEO\\"
#define VBOXWDDM_REG_DISPLAYSETTINGSKEY_PREFIX_WIN10_17763 L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\UnitedVideo\\CONTROL\\VIDEO\\"

#define VBOXWDDM_REG_DISPLAYSETTINGS_ATTACH_RELX L"Attach.RelativeX"
#define VBOXWDDM_REG_DISPLAYSETTINGS_ATTACH_RELY L"Attach.RelativeY"
#define VBOXWDDM_REG_DISPLAYSETTINGS_ATTACH_DESKTOP L"Attach.ToDesktop"

extern DWORD g_VBoxLogUm;
extern DWORD g_RefreshRate;

RT_C_DECLS_BEGIN
NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath);
RT_C_DECLS_END

PVOID vboxWddmMemAlloc(IN SIZE_T cbSize);
PVOID vboxWddmMemAllocZero(IN SIZE_T cbSize);
VOID vboxWddmMemFree(PVOID pvMem);

NTSTATUS vboxWddmCallIsr(PVBOXMP_DEVEXT pDevExt);

DECLINLINE(PVBOXWDDM_RESOURCE) vboxWddmResourceForAlloc(PVBOXWDDM_ALLOCATION pAlloc)
{
#if 0
    if(pAlloc->iIndex == VBOXWDDM_ALLOCATIONINDEX_VOID)
        return NULL;
    PVBOXWDDM_RESOURCE pRc = (PVBOXWDDM_RESOURCE)(((uint8_t*)pAlloc) - RT_OFFSETOF(VBOXWDDM_RESOURCE, aAllocations[pAlloc->iIndex]));
    return pRc;
#else
    return pAlloc->pResource;
#endif
}

VOID vboxWddmAllocationDestroy(PVBOXWDDM_ALLOCATION pAllocation);

DECLINLINE(BOOLEAN) vboxWddmAddrSetVram(PVBOXWDDM_ADDR pAddr, UINT SegmentId, VBOXVIDEOOFFSET offVram)
{
    if (pAddr->SegmentId == SegmentId && pAddr->offVram == offVram)
        return FALSE;

    pAddr->SegmentId = SegmentId;
    pAddr->offVram = offVram;
    return TRUE;
}

DECLINLINE(bool) vboxWddmAddrVramEqual(const VBOXWDDM_ADDR *pAddr1, const VBOXWDDM_ADDR *pAddr2)
{
    return pAddr1->SegmentId == pAddr2->SegmentId && pAddr1->offVram == pAddr2->offVram;
}

DECLINLINE(VBOXVIDEOOFFSET) vboxWddmVramAddrToOffset(PVBOXMP_DEVEXT pDevExt, PHYSICAL_ADDRESS Addr)
{
    PVBOXMP_COMMON pCommon = VBoxCommonFromDeviceExt(pDevExt);
    AssertRelease(pCommon->phVRAM.QuadPart <= Addr.QuadPart);
    return (VBOXVIDEOOFFSET)Addr.QuadPart - pCommon->phVRAM.QuadPart;
}

DECLINLINE(VOID) vboxWddmAssignPrimary(PVBOXWDDM_SOURCE pSource, PVBOXWDDM_ALLOCATION pAllocation,
                                       D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId)
{
    RT_NOREF(srcId);

    /* vboxWddmAssignPrimary can not be run in reentrant order, so safely do a direct unlocked check here */
    if (pSource->pPrimaryAllocation == pAllocation)
        return;

    if (pSource->pPrimaryAllocation)
    {
        PVBOXWDDM_ALLOCATION pOldAlloc = pSource->pPrimaryAllocation;
        /* clear the visibility info fo the current primary */
        pOldAlloc->bVisible = FALSE;
        pOldAlloc->bAssigned = FALSE;
        Assert(pOldAlloc->AllocData.SurfDesc.VidPnSourceId == srcId);
        pOldAlloc->CurVidPnSourceId = -1;
    }

    if (pAllocation)
    {
        Assert(pAllocation->AllocData.SurfDesc.VidPnSourceId == srcId);
        pAllocation->bAssigned = TRUE;
        pAllocation->bVisible = pSource->bVisible;
        pAllocation->CurVidPnSourceId = srcId;

        if (pSource->AllocData.hostID != pAllocation->AllocData.hostID)
        {
            pSource->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_LOCATION; /* force guest->host notification */
            pSource->AllocData.hostID = pAllocation->AllocData.hostID;
        }

        if (!vboxWddmAddrVramEqual(&pSource->AllocData.Addr, &pAllocation->AllocData.Addr))
        {
            if (!pAllocation->AllocData.hostID)
                pSource->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_LOCATION; /* force guest->host notification */

            pSource->AllocData.Addr = pAllocation->AllocData.Addr;
        }
    }
    else
    {
        pSource->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_LOCATION; /* force guest->host notification */
        /*ensure we do not refer to the deleted host id */
        pSource->AllocData.hostID = 0;
    }

    KIRQL OldIrql;
    KeAcquireSpinLock(&pSource->AllocationLock, &OldIrql);
    pSource->pPrimaryAllocation = pAllocation;
    KeReleaseSpinLock(&pSource->AllocationLock, OldIrql);
}

DECLINLINE(VBOXVIDEOOFFSET) vboxWddmAddrFramOffset(const VBOXWDDM_ADDR *pAddr)
{
    return (pAddr->offVram != VBOXVIDEOOFFSET_VOID && pAddr->SegmentId) ?
            (pAddr->SegmentId == 1 ? pAddr->offVram : 0)
            : VBOXVIDEOOFFSET_VOID;
}

DECLINLINE(int) vboxWddmScreenInfoInit(VBVAINFOSCREEN RT_UNTRUSTED_VOLATILE_HOST *pScreen,
                                       const VBOXWDDM_ALLOC_DATA *pAllocData, const POINT * pVScreenPos, uint16_t fFlags)
{
    VBOXVIDEOOFFSET offVram = vboxWddmAddrFramOffset(&pAllocData->Addr);
    if (offVram == VBOXVIDEOOFFSET_VOID && !(fFlags & (VBVA_SCREEN_F_DISABLED | VBVA_SCREEN_F_BLANK2)))
    {
        WARN(("offVram == VBOXVIDEOOFFSET_VOID"));
        return VERR_INVALID_PARAMETER;
    }

    pScreen->u32ViewIndex    = pAllocData->SurfDesc.VidPnSourceId;
    pScreen->i32OriginX      = pVScreenPos->x;
    pScreen->i32OriginY      = pVScreenPos->y;
    pScreen->u32StartOffset  = (uint32_t)offVram;
    pScreen->u32LineSize     = pAllocData->SurfDesc.pitch;
    pScreen->u32Width        = pAllocData->SurfDesc.width;
    pScreen->u32Height       = pAllocData->SurfDesc.height;
    pScreen->u16BitsPerPixel = (uint16_t)pAllocData->SurfDesc.bpp;
    pScreen->u16Flags        = fFlags;

    return VINF_SUCCESS;
}

bool vboxWddmGhDisplayCheckSetInfoFromSource(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_SOURCE pSource);

#define VBOXWDDM_IS_DISPLAYONLY() (g_VBoxDisplayOnly)

# define VBOXWDDM_IS_FB_ALLOCATION(_pDevExt, _pAlloc) ((_pAlloc)->bAssigned)

# define VBOXWDDM_FB_ALLOCATION(_pDevExt, _pSrc) ((_pSrc)->pPrimaryAllocation)

#define VBOXWDDM_CTXLOCK_INIT(_p) do { \
        KeInitializeSpinLock(&(_p)->ContextLock); \
    } while (0)
#define VBOXWDDM_CTXLOCK_DATA KIRQL _ctxLockOldIrql;
#define VBOXWDDM_CTXLOCK_LOCK(_p) do { \
        KeAcquireSpinLock(&(_p)->ContextLock, &_ctxLockOldIrql); \
    } while (0)
#define VBOXWDDM_CTXLOCK_UNLOCK(_p) do { \
        KeReleaseSpinLock(&(_p)->ContextLock, _ctxLockOldIrql); \
    } while (0)

DECLINLINE(PVBOXWDDM_ALLOCATION) vboxWddmGetAllocationFromAllocList(DXGK_ALLOCATIONLIST *pAllocList)
{
    PVBOXWDDM_OPENALLOCATION pOa = (PVBOXWDDM_OPENALLOCATION)pAllocList->hDeviceSpecificAllocation;
    return pOa->pAllocation;
}

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPWddm_h */

