/* $Id: VBoxDispVHWA.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Display driver
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
#include <iprt/asm-amd64-x86.h>

static void VBoxDispVHWACommandFree(PVBOXDISPDEV pDev, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    VBoxHGSMIBufferFree(&pDev->hgsmi.ctx, pCmd);
}

static void VBoxDispVHWACommandRetain(VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    ASMAtomicIncU32(&pCmd->cRefs);
}

static void VBoxDispVHWACommandSubmitAsynchByEvent(PVBOXDISPDEV pDev, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd,
                                                   VBOXPEVENT pEvent)
{
    pCmd->GuestVBVAReserved1 = (uintptr_t)pEvent;
    pCmd->GuestVBVAReserved2 = 0;
    /* ensure the command is not removed until we're processing it */
    VBoxDispVHWACommandRetain(pCmd);

    /* complete it asynchronously by setting event */
    pCmd->Flags |= VBOXVHWACMD_FLAG_GH_ASYNCH_EVENT;
    VBoxHGSMIBufferSubmit(&pDev->hgsmi.ctx, pCmd);

    if(!(ASMAtomicReadU32((volatile uint32_t *)&pCmd->Flags)  & VBOXVHWACMD_FLAG_HG_ASYNCH))
    {
        /* the command is completed */
        pDev->vpAPI.VideoPortProcs.pfnSetEvent(pDev->vpAPI.pContext, pEvent);
    }

    VBoxDispVHWACommandRelease(pDev, pCmd);
}

static void VBoxDispVHWAHanldeVHWACmdCompletion(PVBOXDISPDEV pDev, VBVAHOSTCMD RT_UNTRUSTED_VOLATILE_HOST *pHostCmd)
{
    VBVAHOSTCMDVHWACMDCOMPLETE RT_UNTRUSTED_VOLATILE_HOST *pComplete = VBVAHOSTCMD_BODY(pHostCmd, VBVAHOSTCMDVHWACMDCOMPLETE);
    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST                *pComplCmd =
        (VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *)HGSMIOffsetToPointer(&pDev->hgsmi.ctx.heapCtx.area, pComplete->offCmd);

    PFNVBOXVHWACMDCOMPLETION pfnCompletion = (PFNVBOXVHWACMDCOMPLETION)(uintptr_t)pComplCmd->GuestVBVAReserved1;
    void                    *pContext      = (void *)(uintptr_t)pComplCmd->GuestVBVAReserved2;

    pfnCompletion(pDev, pComplCmd, pContext);

    VBoxDispVBVAHostCommandComplete(pDev, pHostCmd);
}

static void VBoxVHWAHostCommandHandler(PVBOXDISPDEV pDev, VBVAHOSTCMD RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    switch (pCmd->customOpCode)
    {
        case VBVAHG_DCUSTOM_VHWA_CMDCOMPLETE:
            VBoxDispVHWAHanldeVHWACmdCompletion(pDev, pCmd);
            break;

        default:
            VBoxDispVBVAHostCommandComplete(pDev, pCmd);
    }
}

void VBoxDispVHWAInit(PVBOXDISPDEV pDev)
{
    VHWAQUERYINFO info;
    int rc;

    rc = VBoxDispMPVHWAQueryInfo(pDev->hDriver, &info);
    VBOX_WARNRC(rc);

    if (RT_SUCCESS(rc))
    {
        pDev->vhwa.offVramBase = info.offVramBase;
    }
}

int VBoxDispVHWAEnable(PVBOXDISPDEV pDev)
{
    int rc = VERR_GENERAL_FAILURE;

    if (!pDev->hgsmi.bSupported)
    {
        return VERR_NOT_SUPPORTED;
    }

    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd = VBoxDispVHWACommandCreate(pDev, VBOXVHWACMD_TYPE_ENABLE, 0);
    if (!pCmd)
    {
        WARN(("VBoxDispVHWACommandCreate failed"));
        return rc;
    }

    if (VBoxDispVHWACommandSubmit(pDev, pCmd))
        if (RT_SUCCESS(pCmd->rc))
            rc = VINF_SUCCESS;

    VBoxDispVHWACommandRelease(pDev, pCmd);
    return rc;
}

VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *
VBoxDispVHWACommandCreate(PVBOXDISPDEV pDev, VBOXVHWACMD_TYPE enmCmd, VBOXVHWACMD_LENGTH cbCmd)
{
    uint32_t                                cbTotal = cbCmd + VBOXVHWACMD_HEADSIZE();
    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pHdr
        = (VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *)VBoxHGSMIBufferAlloc(&pDev->hgsmi.ctx, cbTotal, HGSMI_CH_VBVA, VBVA_VHWA_CMD);
    if (!pHdr)
    {
        WARN(("HGSMIHeapAlloc failed"));
    }
    else
    {
        memset((void *)pHdr, 0, cbTotal); /* always clear the whole body so caller doesn't need to */
        pHdr->iDisplay = pDev->iDevice;
        pHdr->rc = VERR_GENERAL_FAILURE;
        pHdr->enmCmd = enmCmd;
        pHdr->cRefs = 1;
    }

    /** @todo temporary hack */
    VBoxDispVHWACommandCheckHostCmds(pDev);

    return pHdr;
}

void VBoxDispVHWACommandRelease(PVBOXDISPDEV pDev, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    uint32_t cRefs = ASMAtomicDecU32(&pCmd->cRefs);
    Assert(cRefs < UINT32_MAX / 2);
    if (!cRefs)
        VBoxDispVHWACommandFree(pDev, pCmd);
}

BOOL VBoxDispVHWACommandSubmit(PVBOXDISPDEV pDev, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    VBOXPEVENT pEvent;
    VBOXVP_STATUS rc = pDev->vpAPI.VideoPortProcs.pfnCreateEvent(pDev->vpAPI.pContext, VBOXNOTIFICATION_EVENT, NULL, &pEvent);
    /* don't assert here, otherwise NT4 will be unhappy */
    if(rc == VBOXNO_ERROR)
    {
        pCmd->Flags |= VBOXVHWACMD_FLAG_GH_ASYNCH_IRQ;
        VBoxDispVHWACommandSubmitAsynchByEvent(pDev, pCmd, pEvent);

        rc = pDev->vpAPI.VideoPortProcs.pfnWaitForSingleObject(pDev->vpAPI.pContext, pEvent,
                NULL /*IN PLARGE_INTEGER  pTimeOut*/
                );
        Assert(rc == VBOXNO_ERROR);
        if(rc == VBOXNO_ERROR)
        {
            pDev->vpAPI.VideoPortProcs.pfnDeleteEvent(pDev->vpAPI.pContext, pEvent);
        }
    }
    return rc == VBOXNO_ERROR;
}

void VBoxDispVHWACommandCheckHostCmds(PVBOXDISPDEV pDev)
{
    VBVAHOSTCMD RT_UNTRUSTED_VOLATILE_HOST *pCmd;
    int rc = pDev->hgsmi.mp.pfnRequestCommandsHandler(pDev->hgsmi.mp.hContext, HGSMI_CH_VBVA, pDev->iDevice, &pCmd);
    /* don't assert here, otherwise NT4 will be unhappy */
    if (RT_SUCCESS(rc))
    {
        while (pCmd)
        {
            VBVAHOSTCMD RT_UNTRUSTED_VOLATILE_HOST *pNextCmd = pCmd->u.pNext;
            VBoxVHWAHostCommandHandler(pDev, pCmd);
            pCmd = pNextCmd;
        }
    }
}

static DECLCALLBACK(void) VBoxDispVHWACommandCompletionCallbackEvent(PVBOXDISPDEV pDev, VBOXVHWACMD * pCmd, void * pContext)
{
    RT_NOREF(pCmd);
    VBOXPEVENT pEvent = (VBOXPEVENT)pContext;
    LONG oldState = pDev->vpAPI.VideoPortProcs.pfnSetEvent(pDev->vpAPI.pContext, pEvent);
    Assert(!oldState); NOREF(oldState);
}

/* do not wait for completion */
void VBoxDispVHWACommandSubmitAsynch(PVBOXDISPDEV pDev, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd,
                                     PFNVBOXVHWACMDCOMPLETION pfnCompletion, void * pContext)
{
    pCmd->GuestVBVAReserved1 = (uintptr_t)pfnCompletion;
    pCmd->GuestVBVAReserved2 = (uintptr_t)pContext;
    VBoxDispVHWACommandRetain(pCmd);

    VBoxHGSMIBufferSubmit(&pDev->hgsmi.ctx, pCmd);

    if(!(pCmd->Flags & VBOXVHWACMD_FLAG_HG_ASYNCH))
    {
        /* the command is completed */
        pfnCompletion(pDev, pCmd, pContext);
    }

    VBoxDispVHWACommandRelease(pDev, pCmd);
}

static DECLCALLBACK(void) VBoxDispVHWAFreeCmdCompletion(PVBOXDISPDEV pDev, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd,
                                                        void *pvContext)
{
    RT_NOREF(pvContext);
    VBoxDispVHWACommandRelease(pDev, pCmd);
}

void VBoxDispVHWACommandSubmitAsynchAndComplete (PVBOXDISPDEV pDev, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    pCmd->GuestVBVAReserved1 = (uintptr_t)VBoxDispVHWAFreeCmdCompletion;

    VBoxDispVHWACommandRetain(pCmd);

    pCmd->Flags |= VBOXVHWACMD_FLAG_GH_ASYNCH_NOCOMPLETION;

    VBoxHGSMIBufferSubmit(&pDev->hgsmi.ctx, pCmd);

    uint32_t const fCmdFlags = pCmd->Flags;
    if (   !(fCmdFlags & VBOXVHWACMD_FLAG_HG_ASYNCH)
        || (fCmdFlags & VBOXVHWACMD_FLAG_HG_ASYNCH_RETURNED))
    {
        /* the command is completed */
        VBoxDispVHWAFreeCmdCompletion(pDev, pCmd, NULL);
    }

    VBoxDispVHWACommandRelease(pDev, pCmd);
}

void VBoxDispVHWAFreeHostInfo1(PVBOXDISPDEV pDev, VBOXVHWACMD_QUERYINFO1 RT_UNTRUSTED_VOLATILE_HOST *pInfo)
{
    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd = VBOXVHWACMD_HEAD(pInfo);
    VBoxDispVHWACommandRelease(pDev, pCmd);
}

void VBoxDispVHWAFreeHostInfo2(PVBOXDISPDEV pDev, VBOXVHWACMD_QUERYINFO2 RT_UNTRUSTED_VOLATILE_HOST *pInfo)
{
    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd = VBOXVHWACMD_HEAD(pInfo);
    VBoxDispVHWACommandRelease(pDev, pCmd);
}

static VBOXVHWACMD_QUERYINFO1 RT_UNTRUSTED_VOLATILE_HOST *VBoxDispVHWAQueryHostInfo1(PVBOXDISPDEV pDev)
{
    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd = VBoxDispVHWACommandCreate(pDev, VBOXVHWACMD_TYPE_QUERY_INFO1,
                                                                             sizeof(VBOXVHWACMD_QUERYINFO1));
    if (!pCmd)
    {
        WARN(("VBoxDispVHWACommandCreate failed"));
        return NULL;
    }

    VBOXVHWACMD_QUERYINFO1 RT_UNTRUSTED_VOLATILE_HOST *pInfo1= VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_QUERYINFO1);
    pInfo1->u.in.guestVersion.maj = VBOXVHWA_VERSION_MAJ;
    pInfo1->u.in.guestVersion.min = VBOXVHWA_VERSION_MIN;
    pInfo1->u.in.guestVersion.bld = VBOXVHWA_VERSION_BLD;
    pInfo1->u.in.guestVersion.reserved = VBOXVHWA_VERSION_RSV;

    if(VBoxDispVHWACommandSubmit (pDev, pCmd))
    {
        if(RT_SUCCESS(pCmd->rc))
        {
            return VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_QUERYINFO1);
        }
    }

    VBoxDispVHWACommandRelease(pDev, pCmd);
    return NULL;
}

static VBOXVHWACMD_QUERYINFO2 RT_UNTRUSTED_VOLATILE_HOST *VBoxDispVHWAQueryHostInfo2(PVBOXDISPDEV pDev, uint32_t numFourCC)
{
    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd = VBoxDispVHWACommandCreate(pDev, VBOXVHWACMD_TYPE_QUERY_INFO2,
                                                                             VBOXVHWAINFO2_SIZE(numFourCC));
    if (!pCmd)
    {
        WARN(("VBoxDispVHWACommandCreate failed"));
        return NULL;
    }

    VBOXVHWACMD_QUERYINFO2 RT_UNTRUSTED_VOLATILE_HOST *pInfo2 = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_QUERYINFO2);
    pInfo2->numFourCC = numFourCC;
    if (VBoxDispVHWACommandSubmit(pDev, pCmd))
        if (RT_SUCCESS(pCmd->rc))
            if (pInfo2->numFourCC == numFourCC)
                return pInfo2;

    VBoxDispVHWACommandRelease(pDev, pCmd);
    return NULL;
}

int VBoxDispVHWAInitHostInfo1(PVBOXDISPDEV pDev)
{

    if (!pDev->hgsmi.bSupported)
        return VERR_NOT_SUPPORTED;

    VBOXVHWACMD_QUERYINFO1 RT_UNTRUSTED_VOLATILE_HOST *pInfo = VBoxDispVHWAQueryHostInfo1(pDev);
    if(!pInfo)
    {
        pDev->vhwa.bEnabled = false;
        return VERR_OUT_OF_RESOURCES;
    }

    pDev->vhwa.caps = pInfo->u.out.caps;
    pDev->vhwa.caps2 = pInfo->u.out.caps2;
    pDev->vhwa.colorKeyCaps = pInfo->u.out.colorKeyCaps;
    pDev->vhwa.stretchCaps = pInfo->u.out.stretchCaps;
    pDev->vhwa.surfaceCaps = pInfo->u.out.surfaceCaps;
    pDev->vhwa.numOverlays = pInfo->u.out.numOverlays;
    pDev->vhwa.numFourCC = pInfo->u.out.numFourCC;
    pDev->vhwa.bEnabled = (pInfo->u.out.cfgFlags & VBOXVHWA_CFG_ENABLED);

    VBoxDispVHWAFreeHostInfo1(pDev, pInfo);
    return VINF_SUCCESS;
}

int VBoxDispVHWAInitHostInfo2(PVBOXDISPDEV pDev, DWORD *pFourCC)
{
    int rc = VINF_SUCCESS;

    if (!pDev->hgsmi.bSupported)
        return VERR_NOT_SUPPORTED;

    VBOXVHWACMD_QUERYINFO2 RT_UNTRUSTED_VOLATILE_HOST *pInfo = VBoxDispVHWAQueryHostInfo2(pDev, pDev->vhwa.numFourCC);
    Assert(pInfo);
    if(!pInfo)
        return VERR_OUT_OF_RESOURCES;

    if (pDev->vhwa.numFourCC)
        memcpy(pFourCC, (void const *)pInfo->FourCC, pDev->vhwa.numFourCC * sizeof(pFourCC[0]));
    else
    {
        Assert(0);
        rc = VERR_GENERAL_FAILURE;
    }

    VBoxDispVHWAFreeHostInfo2(pDev, pInfo);

    return rc;
}

int VBoxDispVHWADisable(PVBOXDISPDEV pDev)
{
    int rc = VERR_GENERAL_FAILURE;

    if (!pDev->hgsmi.bSupported)
        return VERR_NOT_SUPPORTED;

    VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd = VBoxDispVHWACommandCreate(pDev, VBOXVHWACMD_TYPE_DISABLE, 0);
    if (!pCmd)
    {
        WARN(("VBoxDispVHWACommandCreate failed"));
        return rc;
    }

    if (VBoxDispVHWACommandSubmit(pDev, pCmd))
        if(RT_SUCCESS(pCmd->rc))
            rc = VINF_SUCCESS;

    VBoxDispVHWACommandRelease(pDev, pCmd);

    VBoxDispVHWACommandCheckHostCmds(pDev);

    return rc;
}

#define MEMTAG 'AWHV'
PVBOXVHWASURFDESC VBoxDispVHWASurfDescAlloc()
{
    return (PVBOXVHWASURFDESC) EngAllocMem(FL_NONPAGED_MEMORY | FL_ZERO_MEMORY, sizeof(VBOXVHWASURFDESC), MEMTAG);
}

void VBoxDispVHWASurfDescFree(PVBOXVHWASURFDESC pDesc)
{
    EngFreeMem(pDesc);
}

uint64_t VBoxDispVHWAVramOffsetFromPDEV(PVBOXDISPDEV pDev, ULONG_PTR offPdev)
{
    return (uint64_t)(pDev->vhwa.offVramBase + offPdev);
}

#define VBOX_DD(_f) DD##_f
#define VBOX_VHWA(_f) VBOXVHWA_##_f
#define VBOX_DD2VHWA(_out, _in, _f) do {if((_in) & VBOX_DD(_f)) _out |= VBOX_VHWA(_f); }while(0)
#define VBOX_DD_VHWA_PAIR(_v) {VBOX_DD(_v), VBOX_VHWA(_v)}
#define VBOX_DD_DUMMY_PAIR(_v) {VBOX_DD(_v), 0}

#define VBOXVHWA_SUPPORTED_CAPS ( \
        VBOXVHWA_CAPS_BLT \
        | VBOXVHWA_CAPS_BLTCOLORFILL \
        | VBOXVHWA_CAPS_BLTFOURCC \
        | VBOXVHWA_CAPS_BLTSTRETCH \
        | VBOXVHWA_CAPS_BLTQUEUE \
        | VBOXVHWA_CAPS_OVERLAY \
        | VBOXVHWA_CAPS_OVERLAYFOURCC \
        | VBOXVHWA_CAPS_OVERLAYSTRETCH \
        | VBOXVHWA_CAPS_OVERLAYCANTCLIP \
        | VBOXVHWA_CAPS_COLORKEY \
        | VBOXVHWA_CAPS_COLORKEYHWASSIST \
        )

#define VBOXVHWA_SUPPORTED_SCAPS ( \
        VBOXVHWA_SCAPS_BACKBUFFER \
        | VBOXVHWA_SCAPS_COMPLEX \
        | VBOXVHWA_SCAPS_FLIP \
        | VBOXVHWA_SCAPS_FRONTBUFFER \
        | VBOXVHWA_SCAPS_OFFSCREENPLAIN \
        | VBOXVHWA_SCAPS_OVERLAY \
        | VBOXVHWA_SCAPS_PRIMARYSURFACE \
        | VBOXVHWA_SCAPS_SYSTEMMEMORY \
        | VBOXVHWA_SCAPS_VIDEOMEMORY \
        | VBOXVHWA_SCAPS_VISIBLE \
        | VBOXVHWA_SCAPS_LOCALVIDMEM \
        )

#define VBOXVHWA_SUPPORTED_SCAPS2 ( \
        VBOXVHWA_CAPS2_CANRENDERWINDOWED \
        | VBOXVHWA_CAPS2_WIDESURFACES \
        | VBOXVHWA_CAPS2_COPYFOURCC \
        )

#define VBOXVHWA_SUPPORTED_PF ( \
        VBOXVHWA_PF_PALETTEINDEXED8 \
        | VBOXVHWA_PF_RGB \
        | VBOXVHWA_PF_RGBTOYUV \
        | VBOXVHWA_PF_YUV \
        | VBOXVHWA_PF_FOURCC \
        )

#define VBOXVHWA_SUPPORTED_SD ( \
        VBOXVHWA_SD_BACKBUFFERCOUNT \
        | VBOXVHWA_SD_CAPS \
        | VBOXVHWA_SD_CKDESTBLT \
        | VBOXVHWA_SD_CKDESTOVERLAY \
        | VBOXVHWA_SD_CKSRCBLT \
        | VBOXVHWA_SD_CKSRCOVERLAY \
        | VBOXVHWA_SD_HEIGHT \
        | VBOXVHWA_SD_PITCH \
        | VBOXVHWA_SD_PIXELFORMAT \
        | VBOXVHWA_SD_WIDTH \
        )

#define VBOXVHWA_SUPPORTED_CKEYCAPS ( \
        VBOXVHWA_CKEYCAPS_DESTBLT \
        | VBOXVHWA_CKEYCAPS_DESTBLTCLRSPACE \
        | VBOXVHWA_CKEYCAPS_DESTBLTCLRSPACEYUV \
        | VBOXVHWA_CKEYCAPS_DESTBLTYUV \
        | VBOXVHWA_CKEYCAPS_DESTOVERLAY \
        | VBOXVHWA_CKEYCAPS_DESTOVERLAYCLRSPACE \
        | VBOXVHWA_CKEYCAPS_DESTOVERLAYCLRSPACEYUV \
        | VBOXVHWA_CKEYCAPS_DESTOVERLAYONEACTIVE \
        | VBOXVHWA_CKEYCAPS_DESTOVERLAYYUV \
        | VBOXVHWA_CKEYCAPS_SRCBLT \
        | VBOXVHWA_CKEYCAPS_SRCBLTCLRSPACE \
        | VBOXVHWA_CKEYCAPS_SRCBLTCLRSPACEYUV \
        | VBOXVHWA_CKEYCAPS_SRCBLTYUV \
        | VBOXVHWA_CKEYCAPS_SRCOVERLAY \
        | VBOXVHWA_CKEYCAPS_SRCOVERLAYCLRSPACE \
        | VBOXVHWA_CKEYCAPS_SRCOVERLAYCLRSPACEYUV \
        | VBOXVHWA_CKEYCAPS_SRCOVERLAYONEACTIVE \
        | VBOXVHWA_CKEYCAPS_SRCOVERLAYYUV \
        | VBOXVHWA_CKEYCAPS_NOCOSTOVERLAY \
        )

#define VBOXVHWA_SUPPORTED_CKEY ( \
        VBOXVHWA_CKEY_COLORSPACE \
        | VBOXVHWA_CKEY_DESTBLT \
        | VBOXVHWA_CKEY_DESTOVERLAY \
        | VBOXVHWA_CKEY_SRCBLT \
        | VBOXVHWA_CKEY_SRCOVERLAY \
        )

#define VBOXVHWA_SUPPORTED_OVER ( \
        VBOXVHWA_OVER_DDFX \
        | VBOXVHWA_OVER_HIDE \
        | VBOXVHWA_OVER_KEYDEST \
        | VBOXVHWA_OVER_KEYDESTOVERRIDE \
        | VBOXVHWA_OVER_KEYSRC \
        | VBOXVHWA_OVER_KEYSRCOVERRIDE \
        | VBOXVHWA_OVER_SHOW \
        )

uint32_t VBoxDispVHWAUnsupportedDDCAPS(uint32_t caps)
{
    return caps & (~VBOXVHWA_SUPPORTED_CAPS);
}

uint32_t VBoxDispVHWAUnsupportedDDSCAPS(uint32_t caps)
{
    return caps & (~VBOXVHWA_SUPPORTED_SCAPS);
}

uint32_t VBoxDispVHWAUnsupportedDDPFS(uint32_t caps)
{
    return caps & (~VBOXVHWA_SUPPORTED_PF);
}

uint32_t VBoxDispVHWAUnsupportedDSS(uint32_t caps)
{
    return caps & (~VBOXVHWA_SUPPORTED_SD);
}

uint32_t VBoxDispVHWAUnsupportedDDCEYCAPS(uint32_t caps)
{
    return caps & (~VBOXVHWA_SUPPORTED_CKEYCAPS);
}

uint32_t VBoxDispVHWASupportedDDCEYCAPS(uint32_t caps)
{
    return caps & (VBOXVHWA_SUPPORTED_CKEYCAPS);
}


uint32_t VBoxDispVHWASupportedDDCAPS(uint32_t caps)
{
    return caps & (VBOXVHWA_SUPPORTED_CAPS);
}

uint32_t VBoxDispVHWASupportedDDSCAPS(uint32_t caps)
{
    return caps & (VBOXVHWA_SUPPORTED_SCAPS);
}

uint32_t VBoxDispVHWASupportedDDPFS(uint32_t caps)
{
    return caps & (VBOXVHWA_SUPPORTED_PF);
}

uint32_t VBoxDispVHWASupportedDSS(uint32_t caps)
{
    return caps & (VBOXVHWA_SUPPORTED_SD);
}

uint32_t VBoxDispVHWASupportedOVERs(uint32_t caps)
{
    return caps & (VBOXVHWA_SUPPORTED_OVER);
}

uint32_t VBoxDispVHWAUnsupportedOVERs(uint32_t caps)
{
    return caps & (~VBOXVHWA_SUPPORTED_OVER);
}

uint32_t VBoxDispVHWASupportedCKEYs(uint32_t caps)
{
    return caps & (VBOXVHWA_SUPPORTED_CKEY);
}

uint32_t VBoxDispVHWAUnsupportedCKEYs(uint32_t caps)
{
    return caps & (~VBOXVHWA_SUPPORTED_CKEY);
}

uint32_t VBoxDispVHWAFromDDOVERs(uint32_t caps) { return caps; }
uint32_t VBoxDispVHWAToDDOVERs(uint32_t caps)   { return caps; }
uint32_t VBoxDispVHWAFromDDCKEYs(uint32_t caps) { return caps; }
uint32_t VBoxDispVHWAToDDCKEYs(uint32_t caps)   { return caps; }

uint32_t VBoxDispVHWAFromDDCAPS(uint32_t caps)
{
    return caps;
}

uint32_t VBoxDispVHWAToDDCAPS(uint32_t caps)
{
    return caps;
}

uint32_t VBoxDispVHWAFromDDCAPS2(uint32_t caps)
{
    return caps;
}

uint32_t VBoxDispVHWAToDDCAPS2(uint32_t caps)
{
    return caps;
}

uint32_t VBoxDispVHWAFromDDSCAPS(uint32_t caps)
{
    return caps;
}

uint32_t VBoxDispVHWAToDDSCAPS(uint32_t caps)
{
    return caps;
}

uint32_t VBoxDispVHWAFromDDPFS(uint32_t caps)
{
    return caps;
}

uint32_t VBoxDispVHWAToDDPFS(uint32_t caps)
{
    return caps;
}

uint32_t VBoxDispVHWAFromDDCKEYCAPS(uint32_t caps)
{
    return caps;
}

uint32_t VBoxDispVHWAToDDCKEYCAPS(uint32_t caps)
{
    return caps;
}

uint32_t VBoxDispVHWAToDDBLTs(uint32_t caps)
{
    return caps;
}

uint32_t VBoxDispVHWAFromDDBLTs(uint32_t caps)
{
    return caps;
}

void VBoxDispVHWAFromDDCOLORKEY(VBOXVHWA_COLORKEY RT_UNTRUSTED_VOLATILE_HOST *pVHWACKey, DDCOLORKEY  *pDdCKey)
{
    pVHWACKey->low = pDdCKey->dwColorSpaceLowValue;
    pVHWACKey->high = pDdCKey->dwColorSpaceHighValue;
}

void VBoxDispVHWAFromDDOVERLAYFX(VBOXVHWA_OVERLAYFX RT_UNTRUSTED_VOLATILE_HOST *pVHWAOverlay, DDOVERLAYFX *pDdOverlay)
{
    /// @todo fxFlags
    VBoxDispVHWAFromDDCOLORKEY(&pVHWAOverlay->DstCK, &pDdOverlay->dckDestColorkey);
    VBoxDispVHWAFromDDCOLORKEY(&pVHWAOverlay->SrcCK, &pDdOverlay->dckSrcColorkey);
}

void VBoxDispVHWAFromDDBLTFX(VBOXVHWA_BLTFX RT_UNTRUSTED_VOLATILE_HOST *pVHWABlt, DDBLTFX *pDdBlt)
{
    pVHWABlt->fillColor = pDdBlt->dwFillColor;

    VBoxDispVHWAFromDDCOLORKEY(&pVHWABlt->DstCK, &pDdBlt->ddckDestColorkey);
    VBoxDispVHWAFromDDCOLORKEY(&pVHWABlt->SrcCK, &pDdBlt->ddckSrcColorkey);
}

int VBoxDispVHWAFromDDPIXELFORMAT(VBOXVHWA_PIXELFORMAT RT_UNTRUSTED_VOLATILE_HOST *pVHWAFormat, DDPIXELFORMAT *pDdFormat)
{
    uint32_t unsup = VBoxDispVHWAUnsupportedDDPFS(pDdFormat->dwFlags);
    Assert(!unsup);
    if(unsup)
        return VERR_GENERAL_FAILURE;

    pVHWAFormat->flags = VBoxDispVHWAFromDDPFS(pDdFormat->dwFlags);
    pVHWAFormat->fourCC = pDdFormat->dwFourCC;
    pVHWAFormat->c.rgbBitCount = pDdFormat->dwRGBBitCount;
    pVHWAFormat->m1.rgbRBitMask = pDdFormat->dwRBitMask;
    pVHWAFormat->m2.rgbGBitMask = pDdFormat->dwGBitMask;
    pVHWAFormat->m3.rgbBBitMask = pDdFormat->dwBBitMask;
    return VINF_SUCCESS;
}

int VBoxDispVHWAFromDDSURFACEDESC(VBOXVHWA_SURFACEDESC RT_UNTRUSTED_VOLATILE_HOST *pVHWADesc, DDSURFACEDESC *pDdDesc)
{
    uint32_t unsupds = VBoxDispVHWAUnsupportedDSS(pDdDesc->dwFlags);
    Assert(!unsupds);
    if(unsupds)
        return VERR_GENERAL_FAILURE;

    pVHWADesc->flags = 0;

    if(pDdDesc->dwFlags & DDSD_BACKBUFFERCOUNT)
    {
        pVHWADesc->flags |= VBOXVHWA_SD_BACKBUFFERCOUNT;
        pVHWADesc->cBackBuffers = pDdDesc->dwBackBufferCount;
    }
    if(pDdDesc->dwFlags & DDSD_CAPS)
    {
        uint32_t unsup = VBoxDispVHWAUnsupportedDDSCAPS(pDdDesc->ddsCaps.dwCaps);
        Assert(!unsup);
        if(unsup)
            return VERR_GENERAL_FAILURE;
        pVHWADesc->flags |= VBOXVHWA_SD_CAPS;
        pVHWADesc->surfCaps = VBoxDispVHWAFromDDSCAPS(pDdDesc->ddsCaps.dwCaps);
    }
    if(pDdDesc->dwFlags & DDSD_CKDESTBLT)
    {
        pVHWADesc->flags |= VBOXVHWA_SD_CKDESTBLT;
        VBoxDispVHWAFromDDCOLORKEY(&pVHWADesc->DstBltCK, &pDdDesc->ddckCKDestBlt);
    }
    if(pDdDesc->dwFlags & DDSD_CKDESTOVERLAY)
    {
        pVHWADesc->flags |= VBOXVHWA_SD_CKDESTOVERLAY;
        VBoxDispVHWAFromDDCOLORKEY(&pVHWADesc->DstOverlayCK, &pDdDesc->ddckCKDestOverlay);
    }
    if(pDdDesc->dwFlags & DDSD_CKSRCBLT)
    {
        pVHWADesc->flags |= VBOXVHWA_SD_CKSRCBLT;
        VBoxDispVHWAFromDDCOLORKEY(&pVHWADesc->SrcBltCK, &pDdDesc->ddckCKSrcBlt);
    }
    if(pDdDesc->dwFlags & DDSD_CKSRCOVERLAY)
    {
        pVHWADesc->flags |= VBOXVHWA_SD_CKSRCOVERLAY;
        VBoxDispVHWAFromDDCOLORKEY(&pVHWADesc->SrcOverlayCK, &pDdDesc->ddckCKSrcOverlay);
    }
    if(pDdDesc->dwFlags & DDSD_HEIGHT)
    {
        pVHWADesc->flags |= VBOXVHWA_SD_HEIGHT;
        pVHWADesc->height = pDdDesc->dwHeight;
    }
    if(pDdDesc->dwFlags & DDSD_WIDTH)
    {
        pVHWADesc->flags |= VBOXVHWA_SD_WIDTH;
        pVHWADesc->width = pDdDesc->dwWidth;
    }
    if(pDdDesc->dwFlags & DDSD_PITCH)
    {
        pVHWADesc->flags |= VBOXVHWA_SD_PITCH;
        pVHWADesc->pitch = pDdDesc->lPitch;
    }
    if(pDdDesc->dwFlags & DDSD_PIXELFORMAT)
    {
        int rc = VBoxDispVHWAFromDDPIXELFORMAT(&pVHWADesc->PixelFormat, &pDdDesc->ddpfPixelFormat);
        if(RT_FAILURE(rc))
            return rc;
        pVHWADesc->flags |= VBOXVHWA_SD_PIXELFORMAT;
    }
    return VINF_SUCCESS;
}

void VBoxDispVHWAFromRECTL(VBOXVHWA_RECTL *pDst, RECTL const *pSrc)
{
    pDst->left = pSrc->left;
    pDst->top = pSrc->top;
    pDst->right = pSrc->right;
    pDst->bottom = pSrc->bottom;
}

void VBoxDispVHWAFromRECTL(VBOXVHWA_RECTL RT_UNTRUSTED_VOLATILE_HOST *pDst, RECTL const *pSrc)
{
    pDst->left = pSrc->left;
    pDst->top = pSrc->top;
    pDst->right = pSrc->right;
    pDst->bottom = pSrc->bottom;
}

#define MIN(_a, _b) (_a) < (_b) ? (_a) : (_b)
#define MAX(_a, _b) (_a) > (_b) ? (_a) : (_b)

void VBoxDispVHWARectUnited(RECTL * pDst, RECTL * pRect1, RECTL * pRect2)
{
    pDst->left = MIN(pRect1->left, pRect2->left);
    pDst->top = MIN(pRect1->top, pRect2->top);
    pDst->right = MAX(pRect1->right, pRect2->right);
    pDst->bottom = MAX(pRect1->bottom, pRect2->bottom);
}

bool VBoxDispVHWARectIsEmpty(RECTL * pRect)
{
    return pRect->left == pRect->right-1 && pRect->top == pRect->bottom-1;
}

bool VBoxDispVHWARectIntersect(RECTL * pRect1, RECTL * pRect2)
{
    return !((pRect1->left < pRect2->left && pRect1->right < pRect2->left)
            || (pRect2->left < pRect1->left && pRect2->right < pRect1->left)
            || (pRect1->top < pRect2->top && pRect1->bottom < pRect2->top)
            || (pRect2->top < pRect1->top && pRect2->bottom < pRect1->top));
}

bool VBoxDispVHWARectInclude(RECTL * pRect1, RECTL * pRect2)
{
    return ((pRect1->left <= pRect2->left && pRect1->right >= pRect2->right)
            && (pRect1->top <= pRect2->top && pRect1->bottom >= pRect2->bottom));
}


bool VBoxDispVHWARegionIntersects(PVBOXVHWAREGION pReg, RECTL * pRect)
{
    if(!pReg->bValid)
        return false;
    return VBoxDispVHWARectIntersect(&pReg->Rect, pRect);
}

bool VBoxDispVHWARegionIncludes(PVBOXVHWAREGION pReg, RECTL * pRect)
{
    if(!pReg->bValid)
        return false;
    return VBoxDispVHWARectInclude(&pReg->Rect, pRect);
}

bool VBoxDispVHWARegionIncluded(PVBOXVHWAREGION pReg, RECTL * pRect)
{
    if(!pReg->bValid)
        return true;
    return VBoxDispVHWARectInclude(pRect, &pReg->Rect);
}

void VBoxDispVHWARegionSet(PVBOXVHWAREGION pReg, RECTL * pRect)
{
    if(VBoxDispVHWARectIsEmpty(pRect))
    {
        pReg->bValid = false;
    }
    else
    {
        pReg->Rect = *pRect;
        pReg->bValid = true;
    }
}

void VBoxDispVHWARegionAdd(PVBOXVHWAREGION pReg, RECTL * pRect)
{
    if(VBoxDispVHWARectIsEmpty(pRect))
    {
        return;
    }
    else if(!pReg->bValid)
    {
        VBoxDispVHWARegionSet(pReg, pRect);
    }
    else
    {
        VBoxDispVHWARectUnited(&pReg->Rect, &pReg->Rect, pRect);
    }
}

void VBoxDispVHWARegionInit(PVBOXVHWAREGION pReg)
{
    pReg->bValid = false;
}

void VBoxDispVHWARegionClear(PVBOXVHWAREGION pReg)
{
    pReg->bValid = false;
}

bool VBoxDispVHWARegionValid(PVBOXVHWAREGION pReg)
{
    return pReg->bValid;
}

void VBoxDispVHWARegionTrySubstitute(PVBOXVHWAREGION pReg, const RECTL *pRect)
{
    if(!pReg->bValid)
        return;

    if(pReg->Rect.left >= pRect->left && pReg->Rect.right <= pRect->right)
    {
        LONG t = MAX(pReg->Rect.top, pRect->top);
        LONG b = MIN(pReg->Rect.bottom, pRect->bottom);
        if(t < b)
        {
            pReg->Rect.top = t;
            pReg->Rect.bottom = b;
        }
        else
        {
            pReg->bValid = false;
        }
    }
    else if(pReg->Rect.top >= pRect->top && pReg->Rect.bottom <= pRect->bottom)
    {
        LONG l = MAX(pReg->Rect.left, pRect->left);
        LONG r = MIN(pReg->Rect.right, pRect->right);
        if(l < r)
        {
            pReg->Rect.left = l;
            pReg->Rect.right = r;
        }
        else
        {
            pReg->bValid = false;
        }
    }
}
