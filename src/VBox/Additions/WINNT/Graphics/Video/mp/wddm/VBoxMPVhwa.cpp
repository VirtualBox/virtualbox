/* $Id: VBoxMPVhwa.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#include "VBoxMPWddm.h"
#include "VBoxMPVhwa.h"

#include <iprt/semaphore.h>
#include <iprt/asm.h>

#define VBOXVHWA_PRIMARY_ALLOCATION(_pSrc) ((_pSrc)->pPrimaryAllocation)

#define VBOXVHWA_COPY_RECT(a_pDst, a_pSrc) do { \
        (a_pDst)->left    = (a_pSrc)->left; \
        (a_pDst)->top     = (a_pSrc)->top; \
        (a_pDst)->right   = (a_pSrc)->right; \
        (a_pDst)->bottom  = (a_pSrc)->bottom; \
    } while(0)


DECLINLINE(void) vboxVhwaHdrInit(VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pHdr,
                                 D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId, VBOXVHWACMD_TYPE enmCmd)
{
    memset((void *)pHdr, 0, sizeof(VBOXVHWACMD));
    pHdr->iDisplay = srcId;
    pHdr->rc = VERR_GENERAL_FAILURE;
    pHdr->enmCmd = enmCmd;
    pHdr->cRefs = 1;
}

DECLINLINE(void) vbvaVhwaCommandRelease(PVBOXMP_DEVEXT pDevExt, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    uint32_t cRefs = ASMAtomicDecU32(&pCmd->cRefs);
    Assert(cRefs < UINT32_MAX / 2);
    if(!cRefs)
    {
        VBoxHGSMIBufferFree(&VBoxCommonFromDeviceExt(pDevExt)->guestCtx, pCmd);
    }
}

DECLINLINE(void) vbvaVhwaCommandRetain(VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    ASMAtomicIncU32(&pCmd->cRefs);
}

/* do not wait for completion */
void vboxVhwaCommandSubmitAsynch(PVBOXMP_DEVEXT pDevExt, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd,
                                 PFNVBOXVHWACMDCOMPLETION pfnCompletion, void *pContext)
{
    pCmd->GuestVBVAReserved1 = (uintptr_t)pfnCompletion;
    pCmd->GuestVBVAReserved2 = (uintptr_t)pContext;
    vbvaVhwaCommandRetain(pCmd);

    VBoxHGSMIBufferSubmit(&VBoxCommonFromDeviceExt(pDevExt)->guestCtx, pCmd);

    uint32_t const fFlags = pCmd->Flags;
    if(   !(fFlags & VBOXVHWACMD_FLAG_HG_ASYNCH)
       || (   (fFlags & VBOXVHWACMD_FLAG_GH_ASYNCH_NOCOMPLETION)
           && (fFlags & VBOXVHWACMD_FLAG_HG_ASYNCH_RETURNED) ) )
    {
        /* the command is completed */
        pfnCompletion(pDevExt, pCmd, pContext);
    }

    vbvaVhwaCommandRelease(pDevExt, pCmd);
}

/** @callback_method_impl{FNVBOXVHWACMDCOMPLETION} */
static DECLCALLBACK(void)
vboxVhwaCompletionSetEvent(PVBOXMP_DEVEXT pDevExt, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd, void *pvContext)
{
    RT_NOREF(pDevExt, pCmd);
    RTSemEventSignal((RTSEMEVENT)pvContext);
}

void vboxVhwaCommandSubmitAsynchByEvent(PVBOXMP_DEVEXT pDevExt, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd, RTSEMEVENT hEvent)
{
    vboxVhwaCommandSubmitAsynch(pDevExt, pCmd, vboxVhwaCompletionSetEvent, hEvent);
}

void vboxVhwaCommandCheckCompletion(PVBOXMP_DEVEXT pDevExt)
{
    NTSTATUS Status = vboxWddmCallIsr(pDevExt);
    AssertNtStatusSuccess(Status);
}

VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *vboxVhwaCommandCreate(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId,
                                                              VBOXVHWACMD_TYPE enmCmd, VBOXVHWACMD_LENGTH cbCmd)
{
    vboxVhwaCommandCheckCompletion(pDevExt);
    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pHdr;
    pHdr = (VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *)VBoxHGSMIBufferAlloc(&VBoxCommonFromDeviceExt(pDevExt)->guestCtx,
                                                                          cbCmd + VBOXVHWACMD_HEADSIZE(),
                                                                          HGSMI_CH_VBVA,
                                                                          VBVA_VHWA_CMD);
    Assert(pHdr);
    if (!pHdr)
        LOGREL(("VBoxHGSMIBufferAlloc failed"));
    else
        vboxVhwaHdrInit(pHdr, srcId, enmCmd);

    return pHdr;
}

void vboxVhwaCommandFree(PVBOXMP_DEVEXT pDevExt, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    vbvaVhwaCommandRelease(pDevExt, pCmd);
}

int vboxVhwaCommandSubmit(PVBOXMP_DEVEXT pDevExt, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    RTSEMEVENT hEvent;
    int rc = RTSemEventCreate(&hEvent);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        pCmd->Flags |= VBOXVHWACMD_FLAG_GH_ASYNCH_IRQ;
        vboxVhwaCommandSubmitAsynchByEvent(pDevExt, pCmd, hEvent);
        rc = RTSemEventWait(hEvent, RT_INDEFINITE_WAIT);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
            RTSemEventDestroy(hEvent);
    }
    return rc;
}

/** @callback_method_impl{FNVBOXVHWACMDCOMPLETION} */
static DECLCALLBACK(void)
vboxVhwaCompletionFreeCmd(PVBOXMP_DEVEXT pDevExt, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd, void *pvContext)
{
    RT_NOREF(pvContext);
    vboxVhwaCommandFree(pDevExt, pCmd);
}

void vboxVhwaCompletionListProcess(PVBOXMP_DEVEXT pDevExt, VBOXVTLIST *pList)
{
    PVBOXVTLIST_ENTRY pNext, pCur;
    for (pCur = pList->pFirst; pCur; pCur = pNext)
    {
        /* need to save next since the command may be released in a pfnCallback and thus its data might be invalid */
        pNext = pCur->pNext;
        VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd = VBOXVHWA_LISTENTRY2CMD(pCur);
        PFNVBOXVHWACMDCOMPLETION pfnCallback = (PFNVBOXVHWACMDCOMPLETION)pCmd->GuestVBVAReserved1;
        void *pvCallback = (void*)pCmd->GuestVBVAReserved2;
        pfnCallback(pDevExt, pCmd, pvCallback);
    }
}


void vboxVhwaCommandSubmitAsynchAndComplete(PVBOXMP_DEVEXT pDevExt, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    pCmd->Flags |= VBOXVHWACMD_FLAG_GH_ASYNCH_NOCOMPLETION;

    vboxVhwaCommandSubmitAsynch(pDevExt, pCmd, vboxVhwaCompletionFreeCmd, NULL);
}

static void vboxVhwaFreeHostInfo1(PVBOXMP_DEVEXT pDevExt, VBOXVHWACMD_QUERYINFO1 RT_UNTRUSTED_VOLATILE_HOST *pInfo)
{
    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd = VBOXVHWACMD_HEAD(pInfo);
    vboxVhwaCommandFree(pDevExt, pCmd);
}

static void vboxVhwaFreeHostInfo2(PVBOXMP_DEVEXT pDevExt, VBOXVHWACMD_QUERYINFO2 RT_UNTRUSTED_VOLATILE_HOST *pInfo)
{
    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd = VBOXVHWACMD_HEAD(pInfo);
    vboxVhwaCommandFree(pDevExt, pCmd);
}

static VBOXVHWACMD_QUERYINFO1 RT_UNTRUSTED_VOLATILE_HOST *
vboxVhwaQueryHostInfo1(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId)
{
    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd = vboxVhwaCommandCreate(pDevExt, srcId, VBOXVHWACMD_TYPE_QUERY_INFO1,
                                                                         sizeof(VBOXVHWACMD_QUERYINFO1));
    AssertReturnStmt(pCmd, LOGREL(("vboxVhwaCommandCreate failed")), NULL);

    VBOXVHWACMD_QUERYINFO1 RT_UNTRUSTED_VOLATILE_HOST *pInfo1 = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_QUERYINFO1);
    pInfo1->u.in.guestVersion.maj = VBOXVHWA_VERSION_MAJ;
    pInfo1->u.in.guestVersion.min = VBOXVHWA_VERSION_MIN;
    pInfo1->u.in.guestVersion.bld = VBOXVHWA_VERSION_BLD;
    pInfo1->u.in.guestVersion.reserved = VBOXVHWA_VERSION_RSV;

    int rc = vboxVhwaCommandSubmit(pDevExt, pCmd);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
        if (RT_SUCCESS(pCmd->rc))
            return VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_QUERYINFO1);

    vboxVhwaCommandFree(pDevExt, pCmd);
    return NULL;
}

static VBOXVHWACMD_QUERYINFO2 RT_UNTRUSTED_VOLATILE_HOST *
vboxVhwaQueryHostInfo2(PVBOXMP_DEVEXT pDevExt,  D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId, uint32_t numFourCC)
{
    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd = vboxVhwaCommandCreate(pDevExt, srcId, VBOXVHWACMD_TYPE_QUERY_INFO2,
                                                                         VBOXVHWAINFO2_SIZE(numFourCC));
    AssertReturnStmt(pCmd, LOGREL(("vboxVhwaCommandCreate failed")), NULL);

    VBOXVHWACMD_QUERYINFO2 RT_UNTRUSTED_VOLATILE_HOST *pInfo2 = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_QUERYINFO2);
    pInfo2->numFourCC = numFourCC;

    int rc = vboxVhwaCommandSubmit(pDevExt, pCmd);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        AssertRC(pCmd->rc);
        if(RT_SUCCESS(pCmd->rc))
            if(pInfo2->numFourCC == numFourCC)
                return pInfo2;
    }

    vboxVhwaCommandFree(pDevExt, pCmd);
    return NULL;
}

int vboxVhwaEnable(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId)
{
    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd = vboxVhwaCommandCreate(pDevExt, srcId, VBOXVHWACMD_TYPE_ENABLE, 0);
    AssertReturnStmt(pCmd, LOGREL(("vboxVhwaCommandCreate failed")), VERR_GENERAL_FAILURE);

    int rc = vboxVhwaCommandSubmit(pDevExt, pCmd);
    AssertRC(rc);
    if(RT_SUCCESS(rc))
    {
        AssertRC(pCmd->rc);
        if(RT_SUCCESS(pCmd->rc))
            rc = VINF_SUCCESS;
        else
            rc = pCmd->rc;
    }

    vboxVhwaCommandFree(pDevExt, pCmd);
    return rc;
}

int vboxVhwaDisable(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId)
{
    vboxVhwaCommandCheckCompletion(pDevExt);

    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd  = vboxVhwaCommandCreate(pDevExt, srcId, VBOXVHWACMD_TYPE_DISABLE, 0);
    AssertReturnStmt(pCmd, LOGREL(("vboxVhwaCommandCreate failed")), VERR_GENERAL_FAILURE);

    int rc = vboxVhwaCommandSubmit(pDevExt, pCmd);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        if(RT_SUCCESS(pCmd->rc))
            rc = VINF_SUCCESS;
        else
            rc = pCmd->rc;
    }

    vboxVhwaCommandFree(pDevExt, pCmd);
    return rc;
}

DECLINLINE(VOID) vboxVhwaHlpOverlayListInit(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    PVBOXWDDM_SOURCE pSource = &pDevExt->aSources[VidPnSourceId];
    pSource->cOverlays = 0;
    InitializeListHead(&pSource->OverlayList);
    KeInitializeSpinLock(&pSource->OverlayListLock);
}

static void vboxVhwaInitSrc(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId)
{
    Assert(srcId < (D3DDDI_VIDEO_PRESENT_SOURCE_ID)VBoxCommonFromDeviceExt(pDevExt)->cDisplays);
    VBOXVHWA_INFO *pSettings = &pDevExt->aSources[srcId].Vhwa.Settings;
    memset (pSettings, 0, sizeof (VBOXVHWA_INFO));

    vboxVhwaHlpOverlayListInit(pDevExt, srcId);

    VBOXVHWACMD_QUERYINFO1 RT_UNTRUSTED_VOLATILE_HOST *pInfo1 = vboxVhwaQueryHostInfo1(pDevExt, srcId);
    if (pInfo1)
    {
        if ((pInfo1->u.out.cfgFlags & VBOXVHWA_CFG_ENABLED)
                && pInfo1->u.out.numOverlays)
        {
            if ((pInfo1->u.out.caps & VBOXVHWA_CAPS_OVERLAY)
                    && (pInfo1->u.out.caps & VBOXVHWA_CAPS_OVERLAYSTRETCH)
                    && (pInfo1->u.out.surfaceCaps & VBOXVHWA_SCAPS_OVERLAY)
                    && (pInfo1->u.out.surfaceCaps & VBOXVHWA_SCAPS_FLIP)
                    && (pInfo1->u.out.surfaceCaps & VBOXVHWA_SCAPS_LOCALVIDMEM)
                    && pInfo1->u.out.numOverlays)
            {
                pSettings->fFlags |= VBOXVHWA_F_ENABLED;

                if (pInfo1->u.out.caps & VBOXVHWA_CAPS_COLORKEY)
                {
                    if (pInfo1->u.out.colorKeyCaps & VBOXVHWA_CKEYCAPS_SRCOVERLAY)
                    {
                        pSettings->fFlags |= VBOXVHWA_F_CKEY_SRC;
                        /** @todo VBOXVHWA_CKEYCAPS_SRCOVERLAYONEACTIVE ? */
                    }

                    if (pInfo1->u.out.colorKeyCaps & VBOXVHWA_CKEYCAPS_DESTOVERLAY)
                    {
                        pSettings->fFlags |= VBOXVHWA_F_CKEY_DST;
                        /** @todo VBOXVHWA_CKEYCAPS_DESTOVERLAYONEACTIVE ? */
                    }
                }

                pSettings->cOverlaysSupported = pInfo1->u.out.numOverlays;

                pSettings->cFormats = 0;

                pSettings->aFormats[pSettings->cFormats] = D3DDDIFMT_X8R8G8B8;
                ++pSettings->cFormats;

                if (pInfo1->u.out.numFourCC
                        && (pInfo1->u.out.caps & VBOXVHWA_CAPS_OVERLAYFOURCC))
                {
                    VBOXVHWACMD_QUERYINFO2 RT_UNTRUSTED_VOLATILE_HOST *pInfo2 =
                        vboxVhwaQueryHostInfo2(pDevExt, srcId, pInfo1->u.out.numFourCC);
                    if (pInfo2)
                    {
                        for (uint32_t i = 0; i < pInfo2->numFourCC; ++i)
                        {
                            pSettings->aFormats[pSettings->cFormats] = (D3DDDIFORMAT)pInfo2->FourCC[i];
                            ++pSettings->cFormats;
                        }
                        vboxVhwaFreeHostInfo2(pDevExt, pInfo2);
                    }
                }
            }
        }
        vboxVhwaFreeHostInfo1(pDevExt, pInfo1);
    }
}

void vboxVhwaInit(PVBOXMP_DEVEXT pDevExt)
{
    for (int i = 0; i < VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        vboxVhwaInitSrc(pDevExt, (D3DDDI_VIDEO_PRESENT_SOURCE_ID)i);
    }
}

void vboxVhwaFree(PVBOXMP_DEVEXT pDevExt)
{
    /* we do not allocate/map anything, just issue a Disable command
     * to ensure all pending commands are flushed */
    for (int i = 0; i < VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        vboxVhwaDisable(pDevExt, i);
    }
}

static int vboxVhwaHlpTranslateFormat(VBOXVHWA_PIXELFORMAT RT_UNTRUSTED_VOLATILE_HOST *pFormat, D3DDDIFORMAT enmFormat)
{
    pFormat->Reserved = 0;
    switch (enmFormat)
    {
        case D3DDDIFMT_A8R8G8B8:
        case D3DDDIFMT_X8R8G8B8:
            pFormat->flags = VBOXVHWA_PF_RGB;
            pFormat->c.rgbBitCount = 32;
            pFormat->m1.rgbRBitMask = 0xff0000;
            pFormat->m2.rgbGBitMask = 0xff00;
            pFormat->m3.rgbBBitMask = 0xff;
            /* always zero for now */
            pFormat->m4.rgbABitMask = 0;
            return VINF_SUCCESS;
        case D3DDDIFMT_R8G8B8:
            pFormat->flags = VBOXVHWA_PF_RGB;
            pFormat->c.rgbBitCount = 24;
            pFormat->m1.rgbRBitMask = 0xff0000;
            pFormat->m2.rgbGBitMask = 0xff00;
            pFormat->m3.rgbBBitMask = 0xff;
            /* always zero for now */
            pFormat->m4.rgbABitMask = 0;
            return VINF_SUCCESS;
        case D3DDDIFMT_R5G6B5:
            pFormat->flags = VBOXVHWA_PF_RGB;
            pFormat->c.rgbBitCount = 16;
            pFormat->m1.rgbRBitMask = 0xf800;
            pFormat->m2.rgbGBitMask = 0x7e0;
            pFormat->m3.rgbBBitMask = 0x1f;
            /* always zero for now */
            pFormat->m4.rgbABitMask = 0;
            return VINF_SUCCESS;
        case D3DDDIFMT_P8:
        case D3DDDIFMT_A8:
        case D3DDDIFMT_X1R5G5B5:
        case D3DDDIFMT_A1R5G5B5:
        case D3DDDIFMT_A4R4G4B4:
        case D3DDDIFMT_R3G3B2:
        case D3DDDIFMT_A8R3G3B2:
        case D3DDDIFMT_X4R4G4B4:
        case D3DDDIFMT_A2B10G10R10:
        case D3DDDIFMT_A8B8G8R8:
        case D3DDDIFMT_X8B8G8R8:
        case D3DDDIFMT_G16R16:
        case D3DDDIFMT_A2R10G10B10:
        case D3DDDIFMT_A16B16G16R16:
        case D3DDDIFMT_A8P8:
        default:
        {
            uint32_t fourcc = vboxWddmFormatToFourcc(enmFormat);
            Assert(fourcc);
            if (fourcc)
            {
                pFormat->flags = VBOXVHWA_PF_FOURCC;
                pFormat->fourCC = fourcc;
                return VINF_SUCCESS;
            }
            return VERR_NOT_SUPPORTED;
        }
    }
}

int vboxVhwaHlpDestroySurface(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_ALLOCATION pSurf,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    Assert(pSurf->hHostHandle);
    if (!pSurf->hHostHandle)
        return VERR_INVALID_STATE;

    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd = vboxVhwaCommandCreate(pDevExt, VidPnSourceId, VBOXVHWACMD_TYPE_SURF_DESTROY,
                                                                         sizeof(VBOXVHWACMD_SURF_DESTROY));
    Assert(pCmd);
    if (pCmd)
    {
        VBOXVHWACMD_SURF_DESTROY RT_UNTRUSTED_VOLATILE_HOST *pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_DESTROY);

        memset((void *)pBody, 0, sizeof(VBOXVHWACMD_SURF_DESTROY));

        pBody->u.in.hSurf = pSurf->hHostHandle;

        /* we're not interested in completion, just send the command */
        vboxVhwaCommandSubmitAsynchAndComplete(pDevExt, pCmd);

        pSurf->hHostHandle = VBOXVHWA_SURFHANDLE_INVALID;

        return VINF_SUCCESS;
    }

    return VERR_OUT_OF_RESOURCES;
}

int vboxVhwaHlpPopulateSurInfo(VBOXVHWA_SURFACEDESC RT_UNTRUSTED_VOLATILE_HOST *pInfo, PVBOXWDDM_ALLOCATION pSurf,
                               uint32_t fFlags, uint32_t cBackBuffers, uint32_t fSCaps,
                               D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    RT_NOREF(VidPnSourceId);
    memset((void *)pInfo, 0, sizeof(VBOXVHWA_SURFACEDESC));

#if 0
    /**
     * The following breaks 2D accelerated video playback because this method is called just after the surface was created
     * and most its members are still 0.
     *
     * @todo: Not 100% sure this is the correct way. It looks like the SegmentId specifies where the  memory
     *        for the surface is stored (VRAM vs. system memory) but because this method is only used
     *        to query some parameters (using VBOXVHWACMD_SURF_GETINFO) and this command doesn't access any surface memory
     *        on the host it should be safe.
     */
    if (pSurf->AllocData.Addr.SegmentId != 1)
    {
        WARN(("invalid segment id!"));
        return VERR_INVALID_PARAMETER;
    }
#endif

    pInfo->height = pSurf->AllocData.SurfDesc.height;
    pInfo->width = pSurf->AllocData.SurfDesc.width;
    pInfo->flags |= VBOXVHWA_SD_HEIGHT | VBOXVHWA_SD_WIDTH;
    if (fFlags & VBOXVHWA_SD_PITCH)
    {
        pInfo->pitch = pSurf->AllocData.SurfDesc.pitch;
        pInfo->flags |= VBOXVHWA_SD_PITCH;
        pInfo->sizeX = pSurf->AllocData.SurfDesc.cbSize;
        pInfo->sizeY = 1;
    }

    if (cBackBuffers)
    {
        pInfo->cBackBuffers = cBackBuffers;
        pInfo->flags |= VBOXVHWA_SD_BACKBUFFERCOUNT;
    }
    else
        pInfo->cBackBuffers = 0;
    pInfo->Reserved = 0;
        /** @todo color keys */
//                        pInfo->DstOverlayCK;
//                        pInfo->DstBltCK;
//                        pInfo->SrcOverlayCK;
//                        pInfo->SrcBltCK;
    int rc = vboxVhwaHlpTranslateFormat(&pInfo->PixelFormat, pSurf->AllocData.SurfDesc.format);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        pInfo->flags |= VBOXVHWA_SD_PIXELFORMAT;
        pInfo->surfCaps = fSCaps;
        pInfo->flags |= VBOXVHWA_SD_CAPS;
        pInfo->offSurface = pSurf->AllocData.Addr.offVram;
    }

    return rc;
}

int vboxVhwaHlpCheckApplySurfInfo(PVBOXWDDM_ALLOCATION pSurf, VBOXVHWA_SURFACEDESC RT_UNTRUSTED_VOLATILE_HOST *pInfo,
                                  uint32_t fFlags, bool bApplyHostHandle)
{
    int rc = VINF_SUCCESS;
    if (!(fFlags & VBOXVHWA_SD_PITCH))
    {
        /* should be set by host */
//        Assert(pInfo->flags & VBOXVHWA_SD_PITCH);
        pSurf->AllocData.SurfDesc.cbSize = pInfo->sizeX * pInfo->sizeY;
        Assert(pSurf->AllocData.SurfDesc.cbSize);
        pSurf->AllocData.SurfDesc.pitch = pInfo->pitch;
        Assert(pSurf->AllocData.SurfDesc.pitch);
        /** @todo make this properly */
        pSurf->AllocData.SurfDesc.bpp = pSurf->AllocData.SurfDesc.pitch * 8 / pSurf->AllocData.SurfDesc.width;
        Assert(pSurf->AllocData.SurfDesc.bpp);
    }
    else
    {
        Assert(pSurf->AllocData.SurfDesc.cbSize ==  pInfo->sizeX);
        Assert(pInfo->sizeY == 1);
        Assert(pInfo->pitch == pSurf->AllocData.SurfDesc.pitch);
        if (pSurf->AllocData.SurfDesc.cbSize !=  pInfo->sizeX
                || pInfo->sizeY != 1
                || pInfo->pitch != pSurf->AllocData.SurfDesc.pitch)
        {
            rc = VERR_INVALID_PARAMETER;
        }
    }

    if (bApplyHostHandle && RT_SUCCESS(rc))
    {
        pSurf->hHostHandle = pInfo->hSurf;
    }

    return rc;
}

int vboxVhwaHlpCreateSurface(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_ALLOCATION pSurf,
        uint32_t fFlags, uint32_t cBackBuffers, uint32_t fSCaps,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    /* the first thing we need is to post create primary */
    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd = vboxVhwaCommandCreate(pDevExt, VidPnSourceId, VBOXVHWACMD_TYPE_SURF_CREATE,
                                                                         sizeof(VBOXVHWACMD_SURF_CREATE));
    Assert(pCmd);
    if (pCmd)
    {
        VBOXVHWACMD_SURF_CREATE RT_UNTRUSTED_VOLATILE_HOST *pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_CREATE);
        int rc = VINF_SUCCESS;

        memset((void *)pBody, 0, sizeof(VBOXVHWACMD_SURF_CREATE));

        rc = vboxVhwaHlpPopulateSurInfo(&pBody->SurfInfo, pSurf, fFlags, cBackBuffers, fSCaps, VidPnSourceId);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            vboxVhwaCommandSubmit(pDevExt, pCmd);
            Assert(pCmd->rc == VINF_SUCCESS);
            if(pCmd->rc == VINF_SUCCESS)
                rc = vboxVhwaHlpCheckApplySurfInfo(pSurf, &pBody->SurfInfo, fFlags, true);
            else
                rc = pCmd->rc;
        }
        vboxVhwaCommandFree(pDevExt, pCmd);
        return rc;
    }

    return VERR_OUT_OF_RESOURCES;
}

int vboxVhwaHlpGetSurfInfoForSource(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_ALLOCATION pSurf, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    /* the first thing we need is to post create primary */
    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd = vboxVhwaCommandCreate(pDevExt, VidPnSourceId, VBOXVHWACMD_TYPE_SURF_GETINFO,
                                                                         sizeof(VBOXVHWACMD_SURF_GETINFO));
    Assert(pCmd);
    if (pCmd)
    {
        VBOXVHWACMD_SURF_GETINFO RT_UNTRUSTED_VOLATILE_HOST *pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_GETINFO);
        int rc = VINF_SUCCESS;

        memset((void *)pBody, 0, sizeof(VBOXVHWACMD_SURF_GETINFO));

        rc = vboxVhwaHlpPopulateSurInfo(&pBody->SurfInfo, pSurf, 0, 0,
                                          VBOXVHWA_SCAPS_OVERLAY | VBOXVHWA_SCAPS_VIDEOMEMORY
                                        | VBOXVHWA_SCAPS_LOCALVIDMEM | VBOXVHWA_SCAPS_COMPLEX,
                                        VidPnSourceId);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            vboxVhwaCommandSubmit(pDevExt, pCmd);
            Assert(pCmd->rc == VINF_SUCCESS);
            if(pCmd->rc == VINF_SUCCESS)
                rc = vboxVhwaHlpCheckApplySurfInfo(pSurf, &pBody->SurfInfo, 0, true);
            else
                rc = pCmd->rc;
        }
        vboxVhwaCommandFree(pDevExt, pCmd);
        return rc;
    }

    return VERR_OUT_OF_RESOURCES;
}

int vboxVhwaHlpGetSurfInfo(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_ALLOCATION pSurf)
{
    for (int i = 0; i < VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        PVBOXWDDM_SOURCE pSource = &pDevExt->aSources[i];
        if (pSource->Vhwa.Settings.fFlags & VBOXVHWA_F_ENABLED)
        {
            int rc = vboxVhwaHlpGetSurfInfoForSource(pDevExt, pSurf, i);
            AssertRC(rc);
            return rc;
        }
    }
    AssertBreakpoint();
    return VERR_NOT_SUPPORTED;
}

int vboxVhwaHlpDestroyPrimary(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_SOURCE pSource, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    PVBOXWDDM_ALLOCATION pFbSurf = VBOXVHWA_PRIMARY_ALLOCATION(pSource);

    int rc = vboxVhwaHlpDestroySurface(pDevExt, pFbSurf, VidPnSourceId);
    AssertRC(rc);
    return rc;
}

int vboxVhwaHlpCreatePrimary(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_SOURCE pSource, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    PVBOXWDDM_ALLOCATION pFbSurf = VBOXVHWA_PRIMARY_ALLOCATION(pSource);
    Assert(pSource->Vhwa.cOverlaysCreated == 1);
    Assert(pFbSurf->hHostHandle == VBOXVHWA_SURFHANDLE_INVALID);
    if (pFbSurf->hHostHandle != VBOXVHWA_SURFHANDLE_INVALID)
        return VERR_INVALID_STATE;

    int rc = vboxVhwaHlpCreateSurface(pDevExt, pFbSurf,
            VBOXVHWA_SD_PITCH, 0, VBOXVHWA_SCAPS_PRIMARYSURFACE | VBOXVHWA_SCAPS_VIDEOMEMORY | VBOXVHWA_SCAPS_LOCALVIDMEM,
            VidPnSourceId);
    AssertRC(rc);
    return rc;
}

int vboxVhwaHlpCheckInit(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    Assert(VidPnSourceId < (D3DDDI_VIDEO_PRESENT_SOURCE_ID)VBoxCommonFromDeviceExt(pDevExt)->cDisplays);
    if (VidPnSourceId >= (D3DDDI_VIDEO_PRESENT_SOURCE_ID)VBoxCommonFromDeviceExt(pDevExt)->cDisplays)
        return VERR_INVALID_PARAMETER;

    PVBOXWDDM_SOURCE pSource = &pDevExt->aSources[VidPnSourceId];

    Assert(!!(pSource->Vhwa.Settings.fFlags & VBOXVHWA_F_ENABLED));
    if (!(pSource->Vhwa.Settings.fFlags & VBOXVHWA_F_ENABLED))
        return VERR_NOT_SUPPORTED;

    int rc = VINF_SUCCESS;
    /** @todo need a better sync */
    uint32_t cNew = ASMAtomicIncU32(&pSource->Vhwa.cOverlaysCreated);
    if (cNew == 1)
    {
        rc = vboxVhwaEnable(pDevExt, VidPnSourceId);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            rc = vboxVhwaHlpCreatePrimary(pDevExt, pSource, VidPnSourceId);
            AssertRC(rc);
            if (RT_FAILURE(rc))
            {
                int tmpRc = vboxVhwaDisable(pDevExt, VidPnSourceId);
                AssertRC(tmpRc);
            }
        }
    }
    else
    {
        PVBOXWDDM_ALLOCATION pFbSurf = VBOXVHWA_PRIMARY_ALLOCATION(pSource);
        Assert(pFbSurf->hHostHandle);
        if (pFbSurf->hHostHandle)
            rc = VINF_ALREADY_INITIALIZED;
        else
            rc = VERR_INVALID_STATE;
    }

    if (RT_FAILURE(rc))
        ASMAtomicDecU32(&pSource->Vhwa.cOverlaysCreated);

    return rc;
}

int vboxVhwaHlpCheckTerm(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    Assert(VidPnSourceId < (D3DDDI_VIDEO_PRESENT_SOURCE_ID)VBoxCommonFromDeviceExt(pDevExt)->cDisplays);
    if (VidPnSourceId >= (D3DDDI_VIDEO_PRESENT_SOURCE_ID)VBoxCommonFromDeviceExt(pDevExt)->cDisplays)
        return VERR_INVALID_PARAMETER;

    PVBOXWDDM_SOURCE pSource = &pDevExt->aSources[VidPnSourceId];

    Assert(!!(pSource->Vhwa.Settings.fFlags & VBOXVHWA_F_ENABLED));

    /** @todo need a better sync */
    uint32_t cNew = ASMAtomicDecU32(&pSource->Vhwa.cOverlaysCreated);
    int rc = VINF_SUCCESS;
    if (!cNew)
    {
        rc = vboxVhwaHlpDestroyPrimary(pDevExt, pSource, VidPnSourceId);
        AssertRC(rc);
    }
    else
    {
        Assert(cNew < UINT32_MAX / 2);
    }

    return rc;
}

int vboxVhwaHlpOverlayFlip(PVBOXWDDM_OVERLAY pOverlay, const DXGKARG_FLIPOVERLAY *pFlipInfo)
{
    PVBOXWDDM_ALLOCATION pAlloc = (PVBOXWDDM_ALLOCATION)pFlipInfo->hSource;
    Assert(pAlloc->hHostHandle);
    Assert(pAlloc->pResource);
    Assert(pAlloc->pResource == pOverlay->pResource);
    Assert(pFlipInfo->PrivateDriverDataSize == sizeof (VBOXWDDM_OVERLAYFLIP_INFO));
    Assert(pFlipInfo->pPrivateDriverData);
    PVBOXWDDM_SOURCE pSource = &pOverlay->pDevExt->aSources[pOverlay->VidPnSourceId];
    Assert(!!(pSource->Vhwa.Settings.fFlags & VBOXVHWA_F_ENABLED));
    PVBOXWDDM_ALLOCATION pFbSurf = VBOXVHWA_PRIMARY_ALLOCATION(pSource);
    Assert(pFbSurf);
    Assert(pFbSurf->hHostHandle);
    Assert(pFbSurf->AllocData.Addr.offVram != VBOXVIDEOOFFSET_VOID);
    Assert(pOverlay->pCurentAlloc);
    Assert(pOverlay->pCurentAlloc->pResource == pOverlay->pResource);
    Assert(pOverlay->pCurentAlloc != pAlloc);
    int rc = VINF_SUCCESS;

    if (pFbSurf->AllocData.Addr.SegmentId != 1)
    {
        WARN(("invalid segment id on flip"));
        return VERR_INVALID_PARAMETER;
    }

    if (pFlipInfo->PrivateDriverDataSize == sizeof (VBOXWDDM_OVERLAYFLIP_INFO))
    {
        PVBOXWDDM_OVERLAYFLIP_INFO pOurInfo = (PVBOXWDDM_OVERLAYFLIP_INFO)pFlipInfo->pPrivateDriverData;

        VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd = vboxVhwaCommandCreate(pOverlay->pDevExt, pOverlay->VidPnSourceId,
                                                                             VBOXVHWACMD_TYPE_SURF_FLIP,
                                                                             sizeof(VBOXVHWACMD_SURF_FLIP));
        Assert(pCmd);
        if (pCmd)
        {
            VBOXVHWACMD_SURF_FLIP RT_UNTRUSTED_VOLATILE_HOST *pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_FLIP);

            memset((void *)pBody, 0, sizeof(VBOXVHWACMD_SURF_FLIP));

//            pBody->TargGuestSurfInfo;
//            pBody->CurrGuestSurfInfo;
            pBody->u.in.hTargSurf = pAlloc->hHostHandle;
            pBody->u.in.offTargSurface = pFlipInfo->SrcPhysicalAddress.QuadPart;
            pAlloc->AllocData.Addr.offVram = pFlipInfo->SrcPhysicalAddress.QuadPart;
            pBody->u.in.hCurrSurf = pOverlay->pCurentAlloc->hHostHandle;
            pBody->u.in.offCurrSurface = pOverlay->pCurentAlloc->AllocData.Addr.offVram;
            if (pOurInfo->DirtyRegion.fFlags & VBOXWDDM_DIRTYREGION_F_VALID)
            {
                pBody->u.in.xUpdatedTargMemValid = 1;
                if (pOurInfo->DirtyRegion.fFlags & VBOXWDDM_DIRTYREGION_F_RECT_VALID)
                    VBOXVHWA_COPY_RECT(&pBody->u.in.xUpdatedTargMemRect, &pOurInfo->DirtyRegion.Rect);
                else
                {
                    pBody->u.in.xUpdatedTargMemRect.right = pAlloc->AllocData.SurfDesc.width;
                    pBody->u.in.xUpdatedTargMemRect.bottom = pAlloc->AllocData.SurfDesc.height;
                    /* top & left are zero-inited with the above memset */
                }
            }

            /* we're not interested in completion, just send the command */
            vboxVhwaCommandSubmitAsynchAndComplete(pOverlay->pDevExt, pCmd);

            pOverlay->pCurentAlloc = pAlloc;

            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_OUT_OF_RESOURCES;
    }
    else
        rc = VERR_INVALID_PARAMETER;

    return rc;
}

AssertCompile(sizeof (RECT) == sizeof (VBOXVHWA_RECTL));
AssertCompile(RT_SIZEOFMEMB(RECT, left) == RT_SIZEOFMEMB(VBOXVHWA_RECTL, left));
AssertCompile(RT_SIZEOFMEMB(RECT, right) == RT_SIZEOFMEMB(VBOXVHWA_RECTL, right));
AssertCompile(RT_SIZEOFMEMB(RECT, top) == RT_SIZEOFMEMB(VBOXVHWA_RECTL, top));
AssertCompile(RT_SIZEOFMEMB(RECT, bottom) == RT_SIZEOFMEMB(VBOXVHWA_RECTL, bottom));
AssertCompile(RT_OFFSETOF(RECT, left) == RT_OFFSETOF(VBOXVHWA_RECTL, left));
AssertCompile(RT_OFFSETOF(RECT, right) == RT_OFFSETOF(VBOXVHWA_RECTL, right));
AssertCompile(RT_OFFSETOF(RECT, top) == RT_OFFSETOF(VBOXVHWA_RECTL, top));
AssertCompile(RT_OFFSETOF(RECT, bottom) == RT_OFFSETOF(VBOXVHWA_RECTL, bottom));

static void vboxVhwaHlpOverlayDstRectSet(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_OVERLAY pOverlay, const RECT *pRect)
{
    PVBOXWDDM_SOURCE pSource = &pDevExt->aSources[pOverlay->VidPnSourceId];
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSource->OverlayListLock, &OldIrql);
    pOverlay->DstRect = *pRect;
    KeReleaseSpinLock(&pSource->OverlayListLock, OldIrql);
}

static void vboxVhwaHlpOverlayListAdd(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_OVERLAY pOverlay)
{
    PVBOXWDDM_SOURCE pSource = &pDevExt->aSources[pOverlay->VidPnSourceId];
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSource->OverlayListLock, &OldIrql);
    ASMAtomicIncU32(&pSource->cOverlays);
    InsertHeadList(&pSource->OverlayList, &pOverlay->ListEntry);
    KeReleaseSpinLock(&pSource->OverlayListLock, OldIrql);
}

static void vboxVhwaHlpOverlayListRemove(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_OVERLAY pOverlay)
{
    PVBOXWDDM_SOURCE pSource = &pDevExt->aSources[pOverlay->VidPnSourceId];
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSource->OverlayListLock, &OldIrql);
    ASMAtomicDecU32(&pSource->cOverlays);
    RemoveEntryList(&pOverlay->ListEntry);
    KeReleaseSpinLock(&pSource->OverlayListLock, OldIrql);
}

AssertCompile(sizeof (RECT) == sizeof (VBOXVHWA_RECTL));
AssertCompile(RT_SIZEOFMEMB(RECT, left) == RT_SIZEOFMEMB(VBOXVHWA_RECTL, left));
AssertCompile(RT_SIZEOFMEMB(RECT, right) == RT_SIZEOFMEMB(VBOXVHWA_RECTL, right));
AssertCompile(RT_SIZEOFMEMB(RECT, top) == RT_SIZEOFMEMB(VBOXVHWA_RECTL, top));
AssertCompile(RT_SIZEOFMEMB(RECT, bottom) == RT_SIZEOFMEMB(VBOXVHWA_RECTL, bottom));
AssertCompile(RT_OFFSETOF(RECT, left) == RT_OFFSETOF(VBOXVHWA_RECTL, left));
AssertCompile(RT_OFFSETOF(RECT, right) == RT_OFFSETOF(VBOXVHWA_RECTL, right));
AssertCompile(RT_OFFSETOF(RECT, top) == RT_OFFSETOF(VBOXVHWA_RECTL, top));
AssertCompile(RT_OFFSETOF(RECT, bottom) == RT_OFFSETOF(VBOXVHWA_RECTL, bottom));

int vboxVhwaHlpOverlayUpdate(PVBOXWDDM_OVERLAY pOverlay, const DXGK_OVERLAYINFO *pOverlayInfo, RECT * pDstUpdateRect)
{
    PVBOXWDDM_ALLOCATION pAlloc = (PVBOXWDDM_ALLOCATION)pOverlayInfo->hAllocation;
    Assert(pAlloc->hHostHandle);
    Assert(pAlloc->pResource);
    Assert(pAlloc->pResource == pOverlay->pResource);
    Assert(pOverlayInfo->PrivateDriverDataSize == sizeof (VBOXWDDM_OVERLAY_INFO));
    Assert(pOverlayInfo->pPrivateDriverData);
    PVBOXWDDM_SOURCE pSource = &pOverlay->pDevExt->aSources[pOverlay->VidPnSourceId];
    Assert(!!(pSource->Vhwa.Settings.fFlags & VBOXVHWA_F_ENABLED));
    PVBOXWDDM_ALLOCATION pFbSurf = VBOXVHWA_PRIMARY_ALLOCATION(pSource);
    Assert(pFbSurf);
    Assert(pFbSurf->hHostHandle);
    Assert(pFbSurf->AllocData.Addr.offVram != VBOXVIDEOOFFSET_VOID);
    int rc = VINF_SUCCESS;

    if (pFbSurf->AllocData.Addr.SegmentId != 1)
    {
        WARN(("invalid segment id on overlay update"));
        return VERR_INVALID_PARAMETER;
    }

    if (pOverlayInfo->PrivateDriverDataSize == sizeof (VBOXWDDM_OVERLAY_INFO))
    {
        PVBOXWDDM_OVERLAY_INFO pOurInfo = (PVBOXWDDM_OVERLAY_INFO)pOverlayInfo->pPrivateDriverData;

        VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST * pCmd = vboxVhwaCommandCreate(pOverlay->pDevExt, pOverlay->VidPnSourceId,
                                                                              VBOXVHWACMD_TYPE_SURF_OVERLAY_UPDATE,
                                                                              sizeof(VBOXVHWACMD_SURF_OVERLAY_UPDATE));
        Assert(pCmd);
        if (pCmd)
        {
            VBOXVHWACMD_SURF_OVERLAY_UPDATE RT_UNTRUSTED_VOLATILE_HOST *pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_OVERLAY_UPDATE);

            memset((void *)pBody, 0, sizeof(VBOXVHWACMD_SURF_OVERLAY_UPDATE));

            pBody->u.in.hDstSurf = pFbSurf->hHostHandle;
            pBody->u.in.offDstSurface = pFbSurf->AllocData.Addr.offVram;
            VBOXVHWA_COPY_RECT(&pBody->u.in.dstRect, &pOverlayInfo->DstRect);

            pBody->u.in.hSrcSurf = pAlloc->hHostHandle;
            pBody->u.in.offSrcSurface = pOverlayInfo->PhysicalAddress.QuadPart;
            pAlloc->AllocData.Addr.offVram = pOverlayInfo->PhysicalAddress.QuadPart;
            VBOXVHWA_COPY_RECT(&pBody->u.in.srcRect, &pOverlayInfo->SrcRect);

            pBody->u.in.flags |= VBOXVHWA_OVER_SHOW;
            if (pOurInfo->OverlayDesc.fFlags & VBOXWDDM_OVERLAY_F_CKEY_DST)
            {
                pBody->u.in.flags |= VBOXVHWA_OVER_KEYDESTOVERRIDE /* ?? VBOXVHWA_OVER_KEYDEST */;
                pBody->u.in.desc.DstCK.high = pOurInfo->OverlayDesc.DstColorKeyHigh;
                pBody->u.in.desc.DstCK.low = pOurInfo->OverlayDesc.DstColorKeyLow;
            }

            if (pOurInfo->OverlayDesc.fFlags & VBOXWDDM_OVERLAY_F_CKEY_SRC)
            {
                pBody->u.in.flags |= VBOXVHWA_OVER_KEYSRCOVERRIDE /* ?? VBOXVHWA_OVER_KEYSRC */;
                pBody->u.in.desc.SrcCK.high = pOurInfo->OverlayDesc.SrcColorKeyHigh;
                pBody->u.in.desc.SrcCK.low = pOurInfo->OverlayDesc.SrcColorKeyLow;
            }

            if (pOurInfo->DirtyRegion.fFlags & VBOXWDDM_DIRTYREGION_F_VALID)
            {
                pBody->u.in.xFlags |= VBOXVHWACMD_SURF_OVERLAY_UPDATE_F_SRCMEMRECT;
                if (pOurInfo->DirtyRegion.fFlags & VBOXWDDM_DIRTYREGION_F_RECT_VALID)
                    VBOXVHWA_COPY_RECT(&pBody->u.in.xUpdatedSrcMemRect, &pOurInfo->DirtyRegion.Rect);
                else
                {
                    pBody->u.in.xUpdatedSrcMemRect.right = pAlloc->AllocData.SurfDesc.width;
                    pBody->u.in.xUpdatedSrcMemRect.bottom = pAlloc->AllocData.SurfDesc.height;
                    /* top & left are zero-inited with the above memset */
                }
            }

            if (pDstUpdateRect)
            {
                pBody->u.in.xFlags |= VBOXVHWACMD_SURF_OVERLAY_UPDATE_F_DSTMEMRECT;
                VBOXVHWA_COPY_RECT(&pBody->u.in.xUpdatedDstMemRect, pDstUpdateRect);
            }

            /* we're not interested in completion, just send the command */
            vboxVhwaCommandSubmitAsynchAndComplete(pOverlay->pDevExt, pCmd);

            pOverlay->pCurentAlloc = pAlloc;

            vboxVhwaHlpOverlayDstRectSet(pOverlay->pDevExt, pOverlay, &pOverlayInfo->DstRect);

            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_OUT_OF_RESOURCES;
    }
    else
        rc = VERR_INVALID_PARAMETER;

    return rc;
}

int vboxVhwaHlpOverlayUpdate(PVBOXWDDM_OVERLAY pOverlay, const DXGK_OVERLAYINFO *pOverlayInfo)
{
    return vboxVhwaHlpOverlayUpdate(pOverlay, pOverlayInfo, NULL);
}

int vboxVhwaHlpOverlayDestroy(PVBOXWDDM_OVERLAY pOverlay)
{
    int rc = VINF_SUCCESS;

    vboxVhwaHlpOverlayListRemove(pOverlay->pDevExt, pOverlay);

    for (uint32_t i = 0; i < pOverlay->pResource->cAllocations; ++i)
    {
        PVBOXWDDM_ALLOCATION pCurAlloc = &pOverlay->pResource->aAllocations[i];
        rc = vboxVhwaHlpDestroySurface(pOverlay->pDevExt, pCurAlloc, pOverlay->VidPnSourceId);
        AssertRC(rc);
    }

    if (RT_SUCCESS(rc))
    {
        int tmpRc = vboxVhwaHlpCheckTerm(pOverlay->pDevExt, pOverlay->VidPnSourceId);
        AssertRC(tmpRc);
    }

    return rc;
}


int vboxVhwaHlpOverlayCreate(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, DXGK_OVERLAYINFO *pOverlayInfo,
        /* OUT */ PVBOXWDDM_OVERLAY pOverlay)
{
    int rc = vboxVhwaHlpCheckInit(pDevExt, VidPnSourceId);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        PVBOXWDDM_ALLOCATION pAlloc = (PVBOXWDDM_ALLOCATION)pOverlayInfo->hAllocation;
        PVBOXWDDM_RESOURCE pRc = pAlloc->pResource;
        Assert(pRc);
        for (uint32_t i = 0; i < pRc->cAllocations; ++i)
        {
            PVBOXWDDM_ALLOCATION pCurAlloc = &pRc->aAllocations[i];
            rc = vboxVhwaHlpCreateSurface(pDevExt, pCurAlloc,
                        0, pRc->cAllocations - 1, VBOXVHWA_SCAPS_OVERLAY | VBOXVHWA_SCAPS_VIDEOMEMORY | VBOXVHWA_SCAPS_LOCALVIDMEM | VBOXVHWA_SCAPS_COMPLEX,
                        VidPnSourceId);
            AssertRC(rc);
            if (!RT_SUCCESS(rc))
            {
                int tmpRc;
                for (uint32_t j = 0; j < i; ++j)
                {
                    PVBOXWDDM_ALLOCATION pDestroyAlloc = &pRc->aAllocations[j];
                    tmpRc = vboxVhwaHlpDestroySurface(pDevExt, pDestroyAlloc, VidPnSourceId);
                    AssertRC(tmpRc);
                }
                break;
            }
        }

        if (RT_SUCCESS(rc))
        {
            pOverlay->pDevExt = pDevExt;
            pOverlay->pResource = pRc;
            pOverlay->VidPnSourceId = VidPnSourceId;

            vboxVhwaHlpOverlayListAdd(pDevExt, pOverlay);

            RECT DstRect;
            vboxVhwaHlpOverlayDstRectGet(pDevExt, pOverlay, &DstRect);

            rc = vboxVhwaHlpOverlayUpdate(pOverlay, pOverlayInfo, DstRect.right ? &DstRect : NULL);
            if (!RT_SUCCESS(rc))
            {
                int tmpRc = vboxVhwaHlpOverlayDestroy(pOverlay);
                AssertRC(tmpRc);
            }
        }

        if (RT_FAILURE(rc))
        {
            int tmpRc = vboxVhwaHlpCheckTerm(pDevExt, VidPnSourceId);
            AssertRC(tmpRc);
            AssertRC(rc);
        }
    }

    return rc;
}

BOOLEAN vboxVhwaHlpOverlayListIsEmpty(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    PVBOXWDDM_SOURCE pSource = &pDevExt->aSources[VidPnSourceId];
    return !ASMAtomicReadU32(&pSource->cOverlays);
}

#define VBOXWDDM_OVERLAY_FROM_ENTRY(_pEntry) ((PVBOXWDDM_OVERLAY)(((uint8_t*)(_pEntry)) - RT_UOFFSETOF(VBOXWDDM_OVERLAY, ListEntry)))

void vboxVhwaHlpOverlayDstRectUnion(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, RECT *pRect)
{
    if (vboxVhwaHlpOverlayListIsEmpty(pDevExt, VidPnSourceId))
    {
        memset(pRect, 0, sizeof (*pRect));
        return;
    }

    PVBOXWDDM_SOURCE pSource = &pDevExt->aSources[VidPnSourceId];
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSource->OverlayListLock, &OldIrql);
    if (pSource->cOverlays)
    {
        PVBOXWDDM_OVERLAY pOverlay = VBOXWDDM_OVERLAY_FROM_ENTRY(pSource->OverlayList.Flink);
        *pRect = pOverlay->DstRect;
        while (pOverlay->ListEntry.Flink != &pSource->OverlayList)
        {
            pOverlay = VBOXWDDM_OVERLAY_FROM_ENTRY(pOverlay->ListEntry.Flink);
            vboxWddmRectUnite(pRect, &pOverlay->DstRect);
        }
    }
    KeReleaseSpinLock(&pSource->OverlayListLock, OldIrql);
}

void vboxVhwaHlpOverlayDstRectGet(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_OVERLAY pOverlay, RECT *pRect)
{
    PVBOXWDDM_SOURCE pSource = &pDevExt->aSources[pOverlay->VidPnSourceId];
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSource->OverlayListLock, &OldIrql);
    *pRect = pOverlay->DstRect;
    KeReleaseSpinLock(&pSource->OverlayListLock, OldIrql);
}
