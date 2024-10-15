/* $Id: VBoxMPInternal.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Miniport internal functions
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
#include <VBoxVideo.h>
#include <VBox/VBoxGuestLib.h>
#include <iprt/asm.h>

typedef struct _VBVAMINIPORT_CHANNELCONTEXT
{
    PFNHGSMICHANNELHANDLER pfnChannelHandler;
    void *pvChannelHandler;
} VBVAMINIPORT_CHANNELCONTEXT;

typedef struct _VBVADISP_CHANNELCONTEXT
{
    /** The generic command handler builds up a list of commands - in reverse
     * order! - here */
    VBVAHOSTCMD *pCmd;
    bool bValid;
} VBVADISP_CHANNELCONTEXT;

typedef struct _VBVA_CHANNELCONTEXTS
{
    PVBOXMP_COMMON pCommon;
    uint32_t cUsed;
    uint32_t cContexts;
    VBVAMINIPORT_CHANNELCONTEXT mpContext;
    VBVADISP_CHANNELCONTEXT aContexts[1];
} VBVA_CHANNELCONTEXTS;

/* Computes the size of a framebuffer. DualView has a few framebuffers of the computed size. */
static void VBoxComputeFrameBufferSizes(PVBOXMP_DEVEXT pPrimaryExt)
{
    PVBOXMP_COMMON pCommon = VBoxCommonFromDeviceExt(pPrimaryExt);

    ULONG ulAvailable = pCommon->cbVRAM - pCommon->cbMiniportHeap - VBVA_ADAPTER_INFORMATION_SIZE;
    /* Size of a framebuffer. */
    ULONG ulSize = ulAvailable / pCommon->cDisplays;
    /* Align down to 4096 bytes. */
    ulSize &= ~0xFFF;

    LOG(("cbVRAM = 0x%08X, cDisplays = %d, ulSize = 0x%08X, ulSize * cDisplays = 0x%08X, slack = 0x%08X",
         pCommon->cbVRAM, pCommon->cDisplays,
         ulSize, ulSize * pCommon->cDisplays,
         ulAvailable - ulSize * pCommon->cDisplays));

    /* Update the primary info. */
    pPrimaryExt->u.primary.ulMaxFrameBufferSize = ulSize;

    /* Update the per extension info. */
    PVBOXMP_DEVEXT pExt = pPrimaryExt;
    ULONG ulFrameBufferOffset = 0;
    while (pExt)
    {
        pExt->ulFrameBufferOffset = ulFrameBufferOffset;
        /* That is assigned when a video mode is set. */
        pExt->ulFrameBufferSize = 0;

        LOG(("[%d] ulFrameBufferOffset 0x%08X", pExt->iDevice, ulFrameBufferOffset));

        ulFrameBufferOffset += pPrimaryExt->u.primary.ulMaxFrameBufferSize;

        pExt = pExt->pNext;
    }
}

static DECLCALLBACK(int) VBoxVbvaInitInfoDisplayCB(void *pvData, struct VBVAINFOVIEW *p, uint32_t cViews)
{
    PVBOXMP_DEVEXT pExt, pPrimaryExt = (PVBOXMP_DEVEXT) pvData;
    unsigned i;

    for (i = 0, pExt=pPrimaryExt; i < cViews && pExt; i++, pExt = pExt->pNext)
    {
        p[i].u32ViewIndex     = pExt->iDevice;
        p[i].u32ViewOffset    = pExt->ulFrameBufferOffset;
        p[i].u32ViewSize      = pPrimaryExt->u.primary.ulMaxFrameBufferSize;

        /* How much VRAM should be reserved for the guest drivers to use VBVA. */
        const uint32_t cbReservedVRAM = VBVA_DISPLAY_INFORMATION_SIZE + VBVA_MIN_BUFFER_SIZE;

        p[i].u32MaxScreenSize = p[i].u32ViewSize > cbReservedVRAM?
                                    p[i].u32ViewSize - cbReservedVRAM:
                                    0;
    }

    if (i == (unsigned)VBoxCommonFromDeviceExt(pPrimaryExt)->cDisplays && pExt == NULL)
    {
        return VINF_SUCCESS;
    }

    AssertFailed ();
    return VERR_INTERNAL_ERROR;
}

void VBoxCreateDisplays(PVBOXMP_DEVEXT pExt, PVIDEO_PORT_CONFIG_INFO pConfigInfo)
{
    RT_NOREF(pConfigInfo);
    LOGF_ENTER();

    PVBOXMP_COMMON pCommon = VBoxCommonFromDeviceExt(pExt);
    VBOXVIDEOPORTPROCS *pAPI = &pExt->u.primary.VideoPortProcs;

    if (pCommon->bHGSMI)
    {
        if (pAPI->fSupportedTypes & VBOXVIDEOPORTPROCS_CSD)
        {
            PVBOXMP_DEVEXT pPrev = pExt;
            ULONG iDisplay, cDisplays;

            cDisplays = pCommon->cDisplays;
            pCommon->cDisplays = 1;

            for (iDisplay=1; iDisplay<cDisplays; ++iDisplay)
            {
                PVBOXMP_DEVEXT pSExt = NULL;
                VP_STATUS rc;

                /* If VIDEO_DUALVIEW_REMOVABLE is passed as the 3rd parameter, then
                 * the guest does not allow to choose the primary screen.
                 */
                rc = pAPI->pfnCreateSecondaryDisplay(pExt, (PVOID*)&pSExt, 0);
                VBOXMP_WARN_VPS(rc);

                if (rc != NO_ERROR)
                {
                    break;
                }
                LOG(("created secondary device %p", pSExt));

                pSExt->pNext = NULL;
                pSExt->pPrimary = pExt;
                pSExt->iDevice = iDisplay;
                pSExt->ulFrameBufferOffset  = 0;
                pSExt->ulFrameBufferSize    = 0;
                pSExt->u.secondary.bEnabled = FALSE;

                /* Update the list pointers */
                pPrev->pNext = pSExt;
                pPrev = pSExt;

                /* Take the successfully created display into account. */
                pCommon->cDisplays++;
            }
        }
        else
        {
            /* Even though VM could be configured to have multiply monitors,
             * we can't support it on this windows version.
             */
            pCommon->cDisplays = 1;
        }
    }

    /* Now when the number of monitors is known and extensions are created,
     * calculate the layout of framebuffers.
     */
    VBoxComputeFrameBufferSizes(pExt);

    /*Report our screen configuration to host*/
    if (pCommon->bHGSMI)
    {
        int rc;
        rc = VBoxHGSMISendViewInfo(&pCommon->guestCtx, pCommon->cDisplays, VBoxVbvaInitInfoDisplayCB, (void *) pExt);

        if (RT_FAILURE (rc))
        {
            WARN(("VBoxHGSMISendViewInfo failed with rc=%#x, HGSMI disabled", rc));
            pCommon->bHGSMI = FALSE;
        }
    }

    LOGF_LEAVE();
}

static DECLCALLBACK(void) VBoxVbvaFlush(void *pvFlush)
{
    LOGF_ENTER();

    PVBOXMP_DEVEXT pExt = (PVBOXMP_DEVEXT)pvFlush;
    PVBOXMP_DEVEXT pPrimary = pExt? pExt->pPrimary: NULL;

    if (pPrimary)
    {
        VMMDevVideoAccelFlush *req = (VMMDevVideoAccelFlush *)pPrimary->u.primary.pvReqFlush;

        if (req)
        {
            int rc = VbglR0GRPerform (&req->header);

            if (RT_FAILURE(rc))
            {
                WARN(("rc = %#xrc!", rc));
            }
        }
    }
    LOGF_LEAVE();
}

int VBoxVbvaEnable(PVBOXMP_DEVEXT pExt, BOOLEAN bEnable, VBVAENABLERESULT *pResult)
{
    int rc = VINF_SUCCESS;
    LOGF_ENTER();

    VMMDevMemory *pVMMDevMemory = NULL;

    rc = VbglR0QueryVMMDevMemory(&pVMMDevMemory);
    if (RT_FAILURE(rc))
    {
        WARN(("VbglR0QueryVMMDevMemory rc = %#xrc", rc));
        LOGF_LEAVE();
        return rc;
    }

    if (pExt->iDevice>0)
    {
        PVBOXMP_DEVEXT pPrimary = pExt->pPrimary;
        LOGF(("skipping non-primary display %d", pExt->iDevice));

        if (bEnable && pPrimary->u.primary.ulVbvaEnabled && pVMMDevMemory)
        {
            pResult->pVbvaMemory = &pVMMDevMemory->vbvaMemory;
            pResult->pfnFlush    = VBoxVbvaFlush;
            pResult->pvFlush     = pExt;
        }
        else
        {
            VideoPortZeroMemory(&pResult, sizeof(VBVAENABLERESULT));
        }

        LOGF_LEAVE();
        return rc;
    }

    /* Allocate the memory block for VMMDevReq_VideoAccelFlush request. */
    if (pExt->u.primary.pvReqFlush == NULL)
    {
        VMMDevVideoAccelFlush *req = NULL;

        rc = VbglR0GRAlloc((VMMDevRequestHeader **)&req, sizeof(VMMDevVideoAccelFlush), VMMDevReq_VideoAccelFlush);

        if (RT_SUCCESS(rc))
        {
            pExt->u.primary.pvReqFlush = req;
        }
        else
        {
            WARN(("VbglR0GRAlloc(VMMDevVideoAccelFlush) rc = %#xrc", rc));
            LOGF_LEAVE();
            return rc;
        }
    }

    ULONG ulEnabled = 0;

    VMMDevVideoAccelEnable *req = NULL;
    rc = VbglR0GRAlloc((VMMDevRequestHeader **)&req, sizeof(VMMDevVideoAccelEnable), VMMDevReq_VideoAccelEnable);

    if (RT_SUCCESS(rc))
    {
        req->u32Enable    = bEnable;
        req->cbRingBuffer = VBVA_RING_BUFFER_SIZE;
        req->fu32Status   = 0;

        rc = VbglR0GRPerform(&req->header);
        if (RT_SUCCESS(rc))
        {
            if (req->fu32Status & VBVA_F_STATUS_ACCEPTED)
            {
                LOG(("accepted"));

                /* Initialize the result information and VBVA memory. */
                if (req->fu32Status & VBVA_F_STATUS_ENABLED)
                {
                    pResult->pVbvaMemory = &pVMMDevMemory->vbvaMemory;
                    pResult->pfnFlush    = VBoxVbvaFlush;
                    pResult->pvFlush     = pExt;
                    ulEnabled = 1;
                }
                else
                {
                    VideoPortZeroMemory(&pResult, sizeof(VBVAENABLERESULT));
                }
            }
            else
            {
                LOG(("rejected"));

                /* Disable VBVA for old hosts. */
                req->u32Enable = 0;
                req->cbRingBuffer = VBVA_RING_BUFFER_SIZE;
                req->fu32Status = 0;

                VbglR0GRPerform(&req->header);

                rc = VERR_NOT_SUPPORTED;
            }
        }
        else
        {
            WARN(("rc = %#xrc", rc));
        }

        VbglR0GRFree(&req->header);
    }
    else
    {
        WARN(("VbglR0GRAlloc(VMMDevVideoAccelEnable) rc = %#xrc", rc));
    }

    pExt->u.primary.ulVbvaEnabled = ulEnabled;

    LOGF_LEAVE();
    return rc;
}

static VBVADISP_CHANNELCONTEXT* VBoxVbvaFindHandlerInfo(VBVA_CHANNELCONTEXTS *pCallbacks, int iId)
{
    if (iId < 0)
    {
        return NULL;
    }
    else if(pCallbacks->cContexts > (uint32_t)iId)
    {
        return &pCallbacks->aContexts[iId];
    }
    return NULL;
}

/* Reverses a NULL-terminated linked list of VBVAHOSTCMD structures. */
static VBVAHOSTCMD *VBoxVbvaReverseList(VBVAHOSTCMD *pList)
{
    VBVAHOSTCMD *pFirst = NULL;
    while (pList)
    {
        VBVAHOSTCMD *pNext = pList;
        pList = pList->u.pNext;
        pNext->u.pNext = pFirst;
        pFirst = pNext;
    }
    return pFirst;
}

/** @callback_method_impl{FNVBOXVIDEOHGSMICOMPLETION} */
DECLCALLBACK(void) VBoxMPHGSMIHostCmdCompleteCB(HVBOXVIDEOHGSMI hHGSMI, struct VBVAHOSTCMD RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    PHGSMIHOSTCOMMANDCONTEXT pCtx = &((PVBOXMP_COMMON)hHGSMI)->hostCtx;
    VBoxHGSMIHostCmdComplete(pCtx, pCmd);
}

/** @callback_method_impl{FNVBOXVIDEOHGSMICOMMANDS} */
DECLCALLBACK(int) VBoxMPHGSMIHostCmdRequestCB(HVBOXVIDEOHGSMI hHGSMI, uint8_t u8Channel,
                                              uint32_t iDisplay, struct VBVAHOSTCMD RT_UNTRUSTED_VOLATILE_HOST **ppCmd)
{
    LOGF_ENTER();

    if (!ppCmd)
    {
        LOGF_LEAVE();
        return VERR_INVALID_PARAMETER;
    }

    PHGSMIHOSTCOMMANDCONTEXT pCtx = &((PVBOXMP_COMMON)hHGSMI)->hostCtx;

    /* pick up the host commands */
    VBoxHGSMIProcessHostQueue(pCtx);

    HGSMICHANNEL *pChannel = HGSMIChannelFindById(&pCtx->channels, u8Channel);
    if (pChannel)
    {
        VBVA_CHANNELCONTEXTS * pContexts = (VBVA_CHANNELCONTEXTS *)pChannel->handler.pvHandler;
        VBVADISP_CHANNELCONTEXT *pDispContext = VBoxVbvaFindHandlerInfo(pContexts, iDisplay);

        if (pDispContext)
        {
            VBVAHOSTCMD *pCmd;
            do
            {
                pCmd = ASMAtomicReadPtrT(&pDispContext->pCmd, VBVAHOSTCMD *);
            } while (!ASMAtomicCmpXchgPtr(&pDispContext->pCmd, NULL, pCmd));

            *ppCmd = VBoxVbvaReverseList(pCmd);

            LOGF_LEAVE();
            return VINF_SUCCESS;
        }
        WARN(("!pDispContext for display %d", iDisplay));
    }

    *ppCmd = NULL;
    LOGF_LEAVE();
    return VERR_INVALID_PARAMETER;
}

#define MEM_TAG 'HVBV'
static void* VBoxMPMemAllocDriver(PVBOXMP_COMMON pCommon, const size_t size)
{
    ULONG Tag = MEM_TAG;
    PVBOXMP_DEVEXT pExt = VBoxCommonToPrimaryExt(pCommon);
    return pExt->u.primary.VideoPortProcs.pfnAllocatePool(pExt, (VBOXVP_POOL_TYPE)VpNonPagedPool, size, Tag);
}

static void VBoxMPMemFreeDriver(PVBOXMP_COMMON pCommon, void *pv)
{
    PVBOXMP_DEVEXT pExt = VBoxCommonToPrimaryExt(pCommon);
    pExt->u.primary.VideoPortProcs.pfnFreePool(pExt, pv);
}

static int VBoxVbvaCreateChannelContexts(PVBOXMP_COMMON pCommon, VBVA_CHANNELCONTEXTS **ppContext)
{
    uint32_t cDisplays = (uint32_t)pCommon->cDisplays;
    const size_t size = RT_UOFFSETOF_DYN(VBVA_CHANNELCONTEXTS, aContexts[cDisplays]);
    VBVA_CHANNELCONTEXTS *pContext = (VBVA_CHANNELCONTEXTS*) VBoxMPMemAllocDriver(pCommon, size);
    if (pContext)
    {
        VideoPortZeroMemory(pContext, (ULONG)size);
        pContext->cContexts = cDisplays;
        pContext->pCommon = pCommon;
        *ppContext = pContext;
        return VINF_SUCCESS;
    }

    WARN(("Failed to allocate %d bytes", size));
    return VERR_GENERAL_FAILURE;
}

static int VBoxVbvaDeleteChannelContexts(PVBOXMP_COMMON pCommon, VBVA_CHANNELCONTEXTS * pContext)
{
    VBoxMPMemFreeDriver(pCommon, pContext);
    return VINF_SUCCESS;
}

static void VBoxMPSignalEvent(PVBOXMP_COMMON pCommon, uint64_t pvEvent)
{
    PVBOXMP_DEVEXT pExt = VBoxCommonToPrimaryExt(pCommon);
    PEVENT pEvent = (PEVENT)pvEvent;
    pExt->u.primary.VideoPortProcs.pfnSetEvent(pExt, pEvent);
}

static DECLCALLBACK(int)
VBoxVbvaChannelGenericHandlerCB(void *pvHandler, uint16_t u16ChannelInfo,
                                void RT_UNTRUSTED_VOLATILE_HOST *pvBuffer, HGSMISIZE cbBuffer)
{
    VBVA_CHANNELCONTEXTS *pCallbacks = (VBVA_CHANNELCONTEXTS*)pvHandler;
    LOGF_ENTER();

    Assert(cbBuffer > VBVAHOSTCMD_HDRSIZE);

    if (cbBuffer > VBVAHOSTCMD_HDRSIZE)
    {
        VBVAHOSTCMD *pHdr = (VBVAHOSTCMD*)pvBuffer;
        Assert(pHdr->iDstID >= 0);

        if(pHdr->iDstID >= 0)
        {
            VBVADISP_CHANNELCONTEXT* pHandler = VBoxVbvaFindHandlerInfo(pCallbacks, pHdr->iDstID);
            Assert(pHandler && pHandler->bValid);

            if(pHandler && pHandler->bValid)
            {
                VBVAHOSTCMD *pFirst=NULL, *pLast=NULL, *pCur=pHdr;

                while (pCur)
                {
                    /** @todo */
                    Assert(!pCur->u.Data);
                    Assert(!pFirst);
                    Assert(!pLast);

                    switch (u16ChannelInfo)
                    {
                        case VBVAHG_DISPLAY_CUSTOM:
                        {
#if 0  /* Never taken */
                            if(pLast)
                            {
                                pLast->u.pNext = pCur;
                                pLast = pCur;
                            }
                            else
#endif
                            {
                                pFirst = pCur;
                                pLast = pCur;
                            }
                            Assert(!pCur->u.Data);
#if 0  /* Who is supposed to set pNext? */
                            /// @todo use offset here
                            pCur = pCur->u.pNext;
                            Assert(!pCur);
#else
                            Assert(!pCur->u.pNext);
                            pCur = NULL;
#endif
                            Assert(pFirst);
                            Assert(pFirst == pLast);
                            break;
                        }

                        case VBVAHG_EVENT:
                        {
                            VBVAHOSTCMDEVENT RT_UNTRUSTED_VOLATILE_HOST *pEventCmd = VBVAHOSTCMD_BODY(pCur, VBVAHOSTCMDEVENT);
                            VBoxMPSignalEvent(pCallbacks->pCommon, pEventCmd->pEvent);
                        }

                        default:
                        {
                            Assert(u16ChannelInfo==VBVAHG_EVENT);
                            Assert(!pCur->u.Data);
#if 0  /* pLast has been asserted to be NULL, and who should set pNext? */
                            /// @todo use offset here
                            if(pLast)
                                pLast->u.pNext = pCur->u.pNext;
                            VBVAHOSTCMD * pNext = pCur->u.pNext;
                            pCur->u.pNext = NULL;
#else
                            Assert(!pCur->u.pNext);
#endif
                            VBoxHGSMIHostCmdComplete(&pCallbacks->pCommon->hostCtx, pCur);
#if 0  /* pNext is NULL, and the other things have already been asserted */
                            pCur = pNext;
                            Assert(!pCur);
                            Assert(!pFirst);
                            Assert(pFirst == pLast);
#else
                            pCur = NULL;
#endif
                        }
                    }
                }

                /* we do not support lists currently */
                Assert(pFirst == pLast);
                if(pLast)
                {
                    Assert(pLast->u.pNext == NULL);
                }
                if(pFirst)
                {
                    Assert(pLast);
                    VBVAHOSTCMD *pCmd;
                    do
                    {
                        pCmd = ASMAtomicReadPtrT(&pHandler->pCmd, VBVAHOSTCMD *);
                        pFirst->u.pNext = pCmd;
                    }
                    while (!ASMAtomicCmpXchgPtr(&pHandler->pCmd, pFirst, pCmd));
                }
                else
                {
                    Assert(!pLast);
                }
                LOGF_LEAVE();
                return VINF_SUCCESS;
            }
        }
        else
        {
            /** @todo */
        }
    }

    LOGF_LEAVE();

    /* no handlers were found, need to complete the command here */
    VBoxHGSMIHostCmdComplete(&pCallbacks->pCommon->hostCtx, pvBuffer);
    return VINF_SUCCESS;
}

/* Note: negative iDisplay would mean this is a miniport handler */
int VBoxVbvaChannelDisplayEnable(PVBOXMP_COMMON pCommon, int iDisplay, uint8_t u8Channel)
{
    LOGF_ENTER();

    VBVA_CHANNELCONTEXTS * pContexts;
    HGSMICHANNEL * pChannel = HGSMIChannelFindById(&pCommon->hostCtx.channels, u8Channel);

    if (!pChannel)
    {
        int rc = VBoxVbvaCreateChannelContexts(pCommon, &pContexts);
        if (RT_FAILURE(rc))
        {
            WARN(("VBoxVbvaCreateChannelContexts failed with rc=%#x", rc));
            LOGF_LEAVE();
            return rc;
        }
    }
    else
    {
        pContexts = (VBVA_CHANNELCONTEXTS *)pChannel->handler.pvHandler;
    }

    VBVADISP_CHANNELCONTEXT *pDispContext = VBoxVbvaFindHandlerInfo(pContexts, iDisplay);
    if (!pDispContext)
    {
        WARN(("!pDispContext"));
        LOGF_LEAVE();
        return VERR_GENERAL_FAILURE;
    }

#ifdef DEBUGVHWASTRICT
    Assert(!pDispContext->bValid);
#endif
    Assert(!pDispContext->pCmd);

    if (!pDispContext->bValid)
    {
        pDispContext->bValid = true;
        pDispContext->pCmd = NULL;

        int rc = VINF_SUCCESS;
        if (!pChannel)
        {
            rc = HGSMIChannelRegister(&pCommon->hostCtx.channels, u8Channel,
                                       "VGA Miniport HGSMI channel", VBoxVbvaChannelGenericHandlerCB,
                                       pContexts);
        }

        if (RT_SUCCESS(rc))
        {
            pContexts->cUsed++;
            LOGF_LEAVE();
            return VINF_SUCCESS;
        }
        else
        {
            WARN(("HGSMIChannelRegister failed with rc=%#x", rc));
        }
    }

    if(!pChannel)
    {
        VBoxVbvaDeleteChannelContexts(pCommon, pContexts);
    }

    LOGF_LEAVE();
    return VERR_GENERAL_FAILURE;
}
