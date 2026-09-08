/* $Id: vrdpvideoin.cpp 115184 2026-09-08 09:27:13Z vitali.pelenjow@oracle.com $ */
/** @file
 * VBox Remote Desktop Protocol.
 */

/*
 * Copyright (C) 2012-2026 Oracle and/or its affiliates.
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

#include "vrdpserv.h"
#include "vrdpvideoin.h"


#ifdef VRDP_DEBUG_VIDEOIN
#  define VIDEOINLOG(a) do { VRDPLOG(("VIDEOIN::%s: ", __FUNCTION__)); VRDPLOG(a); } while (0)
#else
#  define VIDEOINLOG(a)
#endif /* VRDP_DEBUG_VIDEOIN */

#ifdef DEBUG_sunlover
#  undef VIDEOINLOG
#  define VIDEOINLOG(a) do { LogRel(("VIDEOIN::%s: ", __FUNCTION__)); LogRel(a); } while (0)
#endif

#ifdef DEBUG_sunlover
#define VIDEOINIGNORE(s) do { AssertMsgFailed((s)); } while(0)
#else
#define VIDEOINIGNORE(s) do {} while(0)
#endif


#define VRDP_PKT_TYPE_VIDEOIN_BASE   100
#define VRDP_PKT_TYPE_VIDEOINCHANNEL (VRDP_PKT_TYPE_VIDEOIN_BASE + 1)
#define VRDP_PKT_TYPE_VIDEOINDEVICE  (VRDP_PKT_TYPE_VIDEOIN_BASE + 2)
#define VRDP_PKT_TYPE_VIDEOINIO      (VRDP_PKT_TYPE_VIDEOIN_BASE + 3)


/* Information about a VIDEOIN channel instance. One instance per client. */
typedef enum VIDEOINCHANNELSTATUS
{
    VIDEO_IN_CHANNEL_VOID,
    VIDEO_IN_CHANNEL_NEGOTIATING,
    VIDEO_IN_CHANNEL_ESTABLISHED,
    VIDEO_IN_CHANNEL_CLOSING
} VIDEOINCHANNELSTATUS;

typedef struct VIDEOINCHANNEL
{
    VRDPPKT pkt;

    RTLISTNODE nodeChannels;

    VideoInClient *pClientChannel;
    uint32_t u32ClientId;
    uint32_t u32Version;       /* VRDE_VIDEOIN_NEGOTIATE_VERSION */
    uint32_t fu32Capabilities; /* VRDE_VIDEOIN_NEGOTIATE_CAP_* */

    VIDEOINCHANNELSTATUS enmStatus;
} VIDEOINCHANNEL;

/* The server context for a videoin device. */
typedef struct VRDPVIDEOINDEVICE
{
    VRDPPKT pkt;

    RTLISTNODE nodeDevice;

    uint32_t u32ClientId;
    uint32_t u32DeviceId;

    VRDEVIDEOINDEVICEHANDLE handle;
    bool fAttached;
    void *pvDeviceCtx;

    VIDEOINCHANNEL *pChannel; /* The channel where the device was attached. */
} VRDPVIDEOINDEVICE;

#pragma pack(1)
typedef struct VIDEOINIOCTX
{
    VRDPPKT pkt;

    VRDPVideoIn *pThis;
    VIDEOINCHANNEL *pChannel; /* The channel where the IO was submitted. */
    void *pvUser;
    uint32_t u32ClientId;
    uint32_t u32DeviceId;
    uint32_t u32CompletionId;

    VRDEVIDEOINMSGHDR hdr; /* &hdr is ready to be sent to the client. */
    uint8_t au8MsgData[1]; /* Placeholder for the message specific data. May contain no actual data. */
} VIDEOINIOCTX;
#pragma pack()

typedef struct VIDEOINIO
{
    RTLISTNODE nodeIO;

    uint32_t u32CompletionId;

    void *pvCtx;
} VIDEOINIO;


/*
 * Wrappers for the VRDPServer VideoIn API.
 */

static DECLCALLBACK(int) VRDEVideoInDeviceAttach(HVRDESERVER hServer,
                                                 const VRDEVIDEOINDEVICEHANDLE *pDeviceHandle,
                                                 void *pvDeviceCtx)
{
    int rc = VINF_SUCCESS;

    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        rc = pServer->VideoIn()->VideoInDeviceAttach(pDeviceHandle, pvDeviceCtx);
    }

    return rc;
}

static DECLCALLBACK(int) VRDEVideoInDeviceDetach(HVRDESERVER hServer,
                                                 const VRDEVIDEOINDEVICEHANDLE *pDeviceHandle)
{
    int rc = VINF_SUCCESS;

    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        rc = pServer->VideoIn()->VideoInDeviceDetach(pDeviceHandle);
    }

    return rc;
}

static DECLCALLBACK(int) VRDEVideoInGetDeviceDesc(HVRDESERVER hServer,
                                                  void *pvUser,
                                                  const VRDEVIDEOINDEVICEHANDLE *pDeviceHandle)
{
    int rc = VINF_SUCCESS;

    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        rc = pServer->VideoIn()->VideoInGetDeviceDesc(pvUser, pDeviceHandle);
    }

    return rc;
}

static DECLCALLBACK(int) VRDEVideoInControl(HVRDESERVER hServer,
                                            void *pvUser,
                                            const VRDEVIDEOINDEVICEHANDLE *pDeviceHandle,
                                            const VRDEVIDEOINCTRLHDR *pReq,
                                            uint32_t cbReq)
{
    int rc = VINF_SUCCESS;

    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        rc = pServer->VideoIn()->VideoInControl(pvUser, pDeviceHandle, pReq, cbReq);
    }

    return rc;
}

static VRDEVIDEOININTERFACE gVideoInInterface =
{
    { 1, sizeof (VRDEVIDEOININTERFACE) },

    VRDEVideoInDeviceAttach,
    VRDEVideoInDeviceDetach,
    VRDEVideoInGetDeviceDesc,
    VRDEVideoInControl
};


/*
 * The VRDP server video input implementation.
 */

VRDPVideoIn::VRDPVideoIn(VRDPServer *pServer)
    :
    m_pServer (pServer),
    m_fShutdown (false)
{
    RT_ZERO(m_interface);

    RTListInit(&m_listChannels);
    RTListInit(&m_listDevices);

    m_IOCompletion.u32IdSrc = 0;
    m_IOCompletion.cIds = 0;
    RTListInit(&m_IOCompletion.ListFree);
    RTListInit(&m_IOCompletion.ListUsed);

    VRDPLock::Create("VideoIn", &m_pLock);
}

/* virtual */ VRDPVideoIn::~VRDPVideoIn()
{
    VIDEOINIO *pIterIO;
    VIDEOINIO *pNextIO;

    RTListForEachSafe(&m_IOCompletion.ListUsed, pIterIO, pNextIO, VIDEOINIO, nodeIO)
    {
        AssertFailed(); /* They all should be completed by now. */
        RTListNodeRemove(&pIterIO->nodeIO);
        VRDPMemFree(pIterIO);
    }

    RTListForEachSafe(&m_IOCompletion.ListFree, pIterIO, pNextIO, VIDEOINIO, nodeIO)
    {
        RTListNodeRemove(&pIterIO->nodeIO);
        VRDPMemFree(pIterIO);
    }

    VRDPLock::Delete(&m_pLock);
}

/* Helper to get the interface method table, called from VRDPServer::GetInterface */
int VRDPVideoIn::GetInterfaceVideoIn(VRDEINTERFACEHDR *pInterface, const VRDEINTERFACEHDR *pCallbacks, void *pvContext)
{
    int rc = VINF_SUCCESS;

    if (   pInterface->u64Version == 1
        && pInterface->u64Size == sizeof(VRDEVIDEOININTERFACE)
        && pCallbacks->u64Version == 1
        && pCallbacks->u64Size == sizeof(VRDEVIDEOINCALLBACKS))
    {
        /* Interface version 1 is requested. */
        VRDEVIDEOININTERFACE *p = (VRDEVIDEOININTERFACE *)pInterface;

        m_interface.callbacks = *(VRDEVIDEOINCALLBACKS *)pCallbacks;
        m_interface.pvContext = pvContext;

        *p = gVideoInInterface;
    }
    else
    {
        rc = VERR_VERSION_MISMATCH;
    }

    return rc;
}

int VRDPVideoIn::VideoInInitialize(void)
{
    VIDEOINLOG(("\n"));

    int rc = viInitialize();

    if (RT_FAILURE(rc))
    {
        viShutdown();
    }

    return rc;
}

void VRDPVideoIn::VideoInShutdown(void)
{
    VIDEOINLOG(("\n"));

    viShutdown();
}

void VRDPVideoIn::viShutdown(void)
{
    /* This is called when the server did not close RDP connection yet.
     * So try to close contexts of every device.
     */
    VIDEOINLOG(("\n"));

    m_fShutdown = true;

    /** @todo Currently the VRDPClient closes the channels. */
}

int VRDPVideoIn::viInitialize(void)
{
    VIDEOINLOG(("\n"));

    m_fShutdown = false;
    RTListInit(&m_listChannels);
    RTListInit(&m_listDevices);

    RT_ZERO(m_IOCompletion);
    RTListInit(&m_IOCompletion.ListFree);
    RTListInit(&m_IOCompletion.ListUsed);

    /** @todo */
    return VINF_SUCCESS;
}

int VRDPVideoIn::VideoInDeviceAttach(const VRDEVIDEOINDEVICEHANDLE *pDeviceHandle, void *pvDeviceCtx)
{
    int rc = VINF_SUCCESS;

    VIDEOINLOG(("VideoInDeviceAttach [%d,%d] %p\n",
               pDeviceHandle->u32ClientId, pDeviceHandle->u32DeviceId, pvDeviceCtx));

    VRDPVIDEOINDEVICE *pDev = viDeviceFind(pDeviceHandle->u32ClientId, pDeviceHandle->u32DeviceId);

    if (pDev)
    {
        pDev->pvDeviceCtx = pvDeviceCtx;
        pDev->fAttached = true;

        VRDPPktRelease(&pDev->pkt);
    }
    else
    {
        rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}

int VRDPVideoIn::VideoInDeviceDetach(const VRDEVIDEOINDEVICEHANDLE *pDeviceHandle)
{
    int rc = VINF_SUCCESS;

    VIDEOINLOG(("VideoInDeviceDetach [%d,%d]\n",
               pDeviceHandle->u32ClientId, pDeviceHandle->u32DeviceId));

    VRDPVIDEOINDEVICE *pDev = viDeviceFind(pDeviceHandle->u32ClientId, pDeviceHandle->u32DeviceId);

    if (pDev)
    {
        pDev->pvDeviceCtx = NULL;
        pDev->fAttached = false;

        VRDPPktRelease(&pDev->pkt);
    }
    else
    {
        rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}

int VRDPVideoIn::VideoInGetDeviceDesc(void *pvUser, const VRDEVIDEOINDEVICEHANDLE *pDeviceHandle)
{
    int rc = VINF_SUCCESS;

    VIDEOINLOG(("VideoInGetDeviceDesc %p [%d,%d]\n",
               pvUser, pDeviceHandle->u32ClientId, pDeviceHandle->u32DeviceId));

    VRDPVIDEOINDEVICE *pDev = viDeviceFind(pDeviceHandle->u32ClientId, pDeviceHandle->u32DeviceId);

    if (pDev)
    {
        rc = viSubmitMsg(VRDE_VIDEOIN_FN_DEVICEDESC, pDev, NULL, 0, pvUser);

        VRDPPktRelease(&pDev->pkt);
    }
    else
    {
        rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}

int VRDPVideoIn::VideoInControl(void *pvUser,
                                const VRDEVIDEOINDEVICEHANDLE *pDeviceHandle,
                                const VRDEVIDEOINCTRLHDR *pReq,
                                uint32_t cbReq)
{
    int rc = VINF_SUCCESS;

    VIDEOINLOG(("VideoInControl %p [%d,%d] %p %d\n",
               pvUser, pDeviceHandle->u32ClientId, pDeviceHandle->u32DeviceId,
               pReq, cbReq));

    if (pvUser == NULL)
    {
        /* The interface specification requires this. */
        return VERR_INVALID_PARAMETER;
    }

    VRDPVIDEOINDEVICE *pDev = viDeviceFind(pDeviceHandle->u32ClientId, pDeviceHandle->u32DeviceId);

    if (pDev)
    {
        rc = viSubmitMsg(VRDE_VIDEOIN_FN_CONTROL, pDev, pReq, cbReq, pvUser);

        VRDPPktRelease(&pDev->pkt);
    }
    else
    {
        rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}

static int viChannelCreate(VIDEOINCHANNEL **ppChannel)
{
    int rc = VINF_SUCCESS;

    VIDEOINCHANNEL *pChannel = (VIDEOINCHANNEL *)VRDPMemAllocZ(sizeof(VIDEOINCHANNEL));

    if (pChannel)
    {
        VRDPPktInit(&pChannel->pkt, VRDP_PKT_TYPE_VIDEOINCHANNEL, sizeof(VIDEOINCHANNEL), NULL, NULL);
        VRDPPktAddRef(&pChannel->pkt);
        *ppChannel = pChannel;
    }
    else
    {
        rc = VERR_NO_MEMORY;
    }

    return rc;
}

int VRDPVideoIn::viChannelAdd(VIDEOINCHANNEL *pChannel)
{
    int rc = VINF_SUCCESS;

    if (RT_SUCCESS(VRDPLock::Lock(m_pLock)))
    {
        RTListAppend(&m_listChannels, &pChannel->nodeChannels);

        VRDPLock::Unlock(m_pLock);
    }
    else
    {
        VRDPPktRelease(&pChannel->pkt);

        rc = VERR_NOT_SUPPORTED;
    }

    return rc;
}

void VRDPVideoIn::viChannelRemove(VIDEOINCHANNEL *pChannel)
{
    if (RT_SUCCESS(VRDPLock::Lock(m_pLock)))
    {
        RTListNodeRemove(&pChannel->nodeChannels);
        VRDPPktRelease(&pChannel->pkt);

        VRDPLock::Unlock(m_pLock);
    }
}

VIDEOINCHANNEL *VRDPVideoIn::viChannelFind(uint32_t u32ClientId)
{
    VIDEOINCHANNEL *pChannel = NULL;

    if (RT_SUCCESS(VRDPLock::Lock(m_pLock)))
    {
        VIDEOINCHANNEL *pIter;
        RTListForEach(&m_listChannels, pIter, VIDEOINCHANNEL, nodeChannels)
        {
            if (pIter->u32ClientId == u32ClientId)
            {
                pChannel = pIter;

                VRDPPktAddRef(&pChannel->pkt);

                break;
            }
        }

        VRDPLock::Unlock(m_pLock);
    }

    return pChannel;
}

VRDPVIDEOINDEVICE *VRDPVideoIn::viDeviceFind(uint32_t u32ClientId, uint32_t u32DeviceId)
{
    VIDEOINLOG(("viDeviceFind: [%d,%d]\n",
               u32ClientId, u32DeviceId));

    VRDPVIDEOINDEVICE *pDev = NULL;

    if (RT_SUCCESS(VRDPLock::Lock(m_pLock)))
    {
        VRDPVIDEOINDEVICE *pIter;
        RTListForEach(&m_listDevices, pIter, VRDPVIDEOINDEVICE, nodeDevice)
        {
            if (   pIter->u32DeviceId == u32DeviceId
                && pIter->u32ClientId == u32ClientId)
            {
                pDev = pIter;
                VRDPPktAddRef(&pDev->pkt);
                break;
            }
        }

        VRDPLock::Unlock(m_pLock);
    }

    return pDev;
}

VRDPVIDEOINDEVICE *VRDPVideoIn::viDeviceAdd(VIDEOINCHANNEL *pChannel, uint32_t u32DeviceId)
{
    /* The caller already checked the device is not in the list. */
    uint32_t u32ClientId = pChannel->pClientChannel->Client()->Id();

    VIDEOINLOG(("viDeviceAdd: [%d,%d]\n",
               u32ClientId, u32DeviceId));

    VRDPVIDEOINDEVICE *pDev = (VRDPVIDEOINDEVICE *)VRDPMemAllocZ(sizeof(VRDPVIDEOINDEVICE));

    if (pDev)
    {
        VRDPPktInit(&pDev->pkt, VRDP_PKT_TYPE_VIDEOINDEVICE, sizeof(VRDPVIDEOINDEVICE), NULL, NULL);

        pDev->u32ClientId    = u32ClientId;
        pDev->u32DeviceId    = u32DeviceId;
        pDev->handle.u32ClientId = u32ClientId;
        pDev->handle.u32DeviceId = u32DeviceId;
        pDev->pvDeviceCtx    = NULL;
        pDev->fAttached      = false;
        pDev->pChannel       = pChannel;

        VRDPPktAddRef(&pDev->pkt);

        VIDEOINLOG(("viDeviceAdd: [%d,%d] -> created %p\n",
               u32ClientId, u32DeviceId, pDev));

        if (RT_SUCCESS(VRDPLock::Lock(m_pLock)))
        {
            RTListAppend(&m_listDevices, &pDev->nodeDevice);

            VRDPLock::Unlock(m_pLock);
        }
        else
        {
            VRDPPktRelease(&pDev->pkt);
            pDev = NULL;
        }
    }

    if (!pDev)
    {
        VRDPPktRelease(&pChannel->pkt);
    }

    return pDev;
}

void VRDPVideoIn::viDeviceRemove(VRDPVIDEOINDEVICE *pDev)
{
    VIDEOINLOG(("viDeviceRemove: %p, [%d,%d]\n",
              pDev, pDev? pDev->u32ClientId: 0, pDev? pDev->u32DeviceId: 0));

    if (pDev)
    {
        if (RT_SUCCESS(VRDPLock::Lock(m_pLock)))
        {
            RTListNodeRemove(&pDev->nodeDevice);

            VRDPLock::Unlock(m_pLock);

            VRDPPktRelease(&pDev->pkt);
        }
    }
}

VRDPVIDEOINDEVICE *VRDPVideoIn::viDeviceFirst(void)
{
    VIDEOINLOG(("viDeviceFirst\n"));

    VRDPVIDEOINDEVICE *pDev = NULL;

    if (RT_SUCCESS(VRDPLock::Lock(m_pLock)))
    {
        pDev = RTListGetFirst(&m_listDevices, VRDPVIDEOINDEVICE, nodeDevice);

        if (pDev)
        {
            VRDPPktAddRef(&pDev->pkt);
        }

        VRDPLock::Unlock(m_pLock);
    }

    return pDev;
}

VRDPVIDEOINDEVICE *VRDPVideoIn::viDeviceNext(VRDPVIDEOINDEVICE *pDev)
{
    VIDEOINLOG(("viDeviceNext\n"));

    VRDPVIDEOINDEVICE *pDevNext = NULL;

    if (RT_SUCCESS(VRDPLock::Lock(m_pLock)))
    {
        pDevNext = RTListGetNext(&m_listDevices, pDev, VRDPVIDEOINDEVICE, nodeDevice);

        if (pDevNext)
        {
            VRDPPktAddRef(&pDevNext->pkt);
        }

        VRDPLock::Unlock(m_pLock);

        VRDPPktRelease(&pDev->pkt);
    }

    return pDevNext;
}

int VRDPVideoIn::viCallbackNotify(uint32_t u32NotifyEvent, const void *pvData, uint32_t cbData)
{
    VIDEOINLOG(("viCallbackNotify: %d %p %d\n",
                u32NotifyEvent, pvData, cbData));

    if (m_interface.callbacks.VRDECallbackVideoInNotify)
    {
        m_interface.callbacks.VRDECallbackVideoInNotify(m_interface.pvContext,
                                                        u32NotifyEvent, pvData, cbData);
        return VINF_SUCCESS;
    }

    return VERR_NOT_SUPPORTED;
}

int VRDPVideoIn::viCallbackDeviceDesc(int rcRequest, void *pvDeviceCtx, void *pvUser, const VRDEVIDEOINDEVICEDESC *pDevice, uint32_t cbDevice)
{
    VIDEOINLOG(("viCallbackDeviceDesc: %Rrc %p %p %p %d\n",
                rcRequest, pvDeviceCtx, pvUser, pDevice, cbDevice));

    if (m_interface.callbacks.VRDECallbackVideoInDeviceDesc)
    {
        m_interface.callbacks.VRDECallbackVideoInDeviceDesc(m_interface.pvContext,
                                                            rcRequest, pvDeviceCtx, pvUser, pDevice, cbDevice);
        return VINF_SUCCESS;
    }

    return VERR_NOT_SUPPORTED;
}

int VRDPVideoIn::viCallbackControl(int rcRequest, void *pvDeviceCtx, void *pvUser, const VRDEVIDEOINCTRLHDR *pControl, uint32_t cbControl)
{
    VIDEOINLOG(("viCallbackControl: %Rrc %p %p %p %d\n",
                rcRequest, pvDeviceCtx, pvUser, pControl, cbControl));

    if (m_interface.callbacks.VRDECallbackVideoInControl)
    {
        m_interface.callbacks.VRDECallbackVideoInControl(m_interface.pvContext,
                                                         rcRequest, pvDeviceCtx, pvUser, pControl, cbControl);
        return VINF_SUCCESS;
    }

    return VERR_NOT_SUPPORTED;
}

int VRDPVideoIn::viCallbackFrame(int rcRequest, void *pvDeviceCtx, const VRDEVIDEOINPAYLOADHDR *pFrame, uint32_t cbFrame)
{
    VIDEOINLOG(("viCallbackFrame: %Rrc %p %p %d\n",
                rcRequest, pvDeviceCtx, pFrame, cbFrame));

    if (m_interface.callbacks.VRDECallbackVideoInFrame)
    {
        m_interface.callbacks.VRDECallbackVideoInFrame(m_interface.pvContext,
                                                       rcRequest, pvDeviceCtx, pFrame, cbFrame);
        return VINF_SUCCESS;
    }

    return VERR_NOT_SUPPORTED;
}

/** @thread INPUT */
int VRDPVideoIn::viSendNegotiate(VideoInClient *pClientChannel)
{
    VRDEVIDEOINMSG_NEGOTIATE msg;
    msg.hdr.u32Length     = sizeof(msg);
    msg.hdr.u32DeviceId   = 0;
    msg.hdr.u32MessageId  = 0;
    msg.hdr.u16FunctionId = VRDE_VIDEOIN_FN_NEGOTIATE;
    msg.hdr.u16Status     = 0;
    msg.u32Version        = VRDE_VIDEOIN_NEGOTIATE_VERSION;
    msg.fu32Capabilities  = VRDE_VIDEOIN_NEGOTIATE_CAP_VOID;

    return pClientChannel->SendData(&msg, sizeof(msg));
}

void VRDPVideoIn::viChannelCleanup(VIDEOINCHANNEL *pChannel)
{
    /* The dynamic channel was closed or requested a negotiation. */
    VideoInClient *pClientChannel = pChannel->pClientChannel;
    uint32_t u32ClientId = pChannel->u32ClientId;

    /** @todo Complete all pending IO for this channel.
     * Search the list of pending IOs and move IOs of this channel to another list,
     * then complete the found IOs.
     */
    VIDEOINIO *pIterIO;
    VIDEOINIO *pNextIO;

    RTLISTANCHOR listProcess;
    RTListInit(&listProcess);

    if (RT_SUCCESS(VRDPLock::Lock(m_pLock)))
    {
        RTListForEachSafe(&m_IOCompletion.ListUsed, pIterIO, pNextIO, VIDEOINIO, nodeIO)
        {
            VIDEOINIOCTX *pCtx = (VIDEOINIOCTX *)pIterIO->pvCtx;

            if (pCtx->pChannel == pChannel)
            {
                /* The IO must be completed. */
                RTListNodeRemove(&pIterIO->nodeIO);
                RTListAppend(&listProcess, &pIterIO->nodeIO);
            }
        }

        VRDPLock::Unlock(m_pLock);
    }
    else
    {
        /* Impossible to complete pending IOs.  */
    }

    RTListForEachSafe(&listProcess, pIterIO, pNextIO, VIDEOINIO, nodeIO)
    {
        VIDEOINIOCTX *pCtx = (VIDEOINIOCTX *)pIterIO->pvCtx;

        VRDPVIDEOINDEVICE *pDev = viDeviceFind(u32ClientId, pCtx->u32DeviceId);

        if (pDev && pDev->fAttached)
        {
            /* Complete the IO. */
            switch (pCtx->hdr.u16FunctionId)
            {
                case VRDE_VIDEOIN_FN_DEVICEDESC:
                    VIDEOINLOG(("Completing cancelled FN_DEVICEDESC [%d,%d]\n",
                                u32ClientId, pCtx->u32DeviceId));
                    viCallbackDeviceDesc(VERR_INTERRUPTED, pDev->pvDeviceCtx, pCtx->pvUser, NULL, 0);
                    break;

                case VRDE_VIDEOIN_FN_CONTROL:
                    VIDEOINLOG(("Completing cancelled FN_CONTROL [%d,%d]\n",
                                u32ClientId, pCtx->u32DeviceId));
                    viCallbackControl(VERR_INTERRUPTED, pDev->pvDeviceCtx, pCtx->pvUser, NULL, 0);
                    break;

                case VRDE_VIDEOIN_FN_NEGOTIATE:
                case VRDE_VIDEOIN_FN_NOTIFY:
                case VRDE_VIDEOIN_FN_CONTROL_NOTIFY:
                case VRDE_VIDEOIN_FN_FRAME:
                default:
                    /* The server does not submit such IOs. */
                    AssertFailed();
                    break;
            }
        }
        else
        {
            VIDEOINLOG(("no device %p %d!!!\n",
                        pDev, pDev? pDev->fAttached: 0));
        }

        if (pDev)
        {
            VRDPPktRelease(&pDev->pkt);
        }

        /* CancelIO. */
        RTListNodeRemove(&pIterIO->nodeIO);
        VRDPPktRelease(&pCtx->pkt);
    }

    /* Report that all video devices in the channel were disconnected. */
    VRDPVIDEOINDEVICE *pIterDev;
    VRDPVIDEOINDEVICE *pNextDev;

    RTListInit(&listProcess);

    if (RT_SUCCESS(VRDPLock::Lock(m_pLock)))
    {
        RTListForEachSafe(&m_listDevices, pIterDev, pNextDev, VRDPVIDEOINDEVICE, nodeDevice)
        {
            /* One channel per client, so the client id identifies devices for this channel. */
            if (pIterDev->u32ClientId == u32ClientId)
            {
                /* The device must be detached. */
                RTListNodeRemove(&pIterDev->nodeDevice);
                RTListAppend(&listProcess, &pIterDev->nodeDevice);
            }
        }

        VRDPLock::Unlock(m_pLock);
    }

    RTListForEachSafe(&listProcess, pIterDev, pNextDev, VRDPVIDEOINDEVICE, nodeDevice)
    {
        viDeviceDetach(pClientChannel, pIterDev);
    }

    return;
}

/** @thread INPUT */
int VRDPVideoIn::VideoInOnCreate(VideoInClient *pClientChannel)
{
    /* A new VideoIn channel.
     * There can be only one VIDEOIN channel per client.
     * Check that the client does not have the channel yet,
     * then create a context and start negotiation.
     */
    uint32_t u32ClientId = pClientChannel->Client()->Id();

    VIDEOINCHANNEL *pChannel = viChannelFind(u32ClientId);
    if (pChannel)
    {
        VRDPPktRelease(&pChannel->pkt);
        VIDEOINLOG(("OnCreate: channel exists for client %u!!!\n", u32ClientId));
        VIDEOINIGNORE("channel exists\n");
        return VERR_NOT_SUPPORTED;
    }

    /* Create a context. */
    int rc = viChannelCreate(&pChannel);

    if (RT_SUCCESS(rc))
    {
        pChannel->pClientChannel = pClientChannel;
        pChannel->u32ClientId = u32ClientId;
        pChannel->enmStatus = VIDEO_IN_CHANNEL_VOID;

        VRDPPktAddRef(&pChannel->pkt);
        rc = viChannelAdd(pChannel);

        if (RT_SUCCESS(rc))
        {
            /* Start negotiations. */
            rc = viSendNegotiate(pClientChannel);

            if (RT_SUCCESS(rc))
            {
                pChannel->enmStatus = VIDEO_IN_CHANNEL_NEGOTIATING;
                VRDPLOGREL(("VIDEOIN created for %d\n", u32ClientId));
            }

            if (RT_FAILURE(rc))
            {
                viChannelRemove(pChannel);
            }
        }

        VRDPPktRelease(&pChannel->pkt);
    }

    return rc;
}

int VRDPVideoIn::VideoInOnData(VideoInClient *pClientChannel,
                               const void *pvData, uint32_t cbData)
{
    /* Process a data packet from the client. Wrong packets are ignored. */
    int rc = VINF_SUCCESS;

    /** @todo VRDPLOGRELIO */ VIDEOINLOG(("VIDEOIN recv %d:\n%.*Rhxd\n", cbData, RT_MIN(64, cbData), pvData));

    uint32_t u32ClientId = pClientChannel->Client()->Id();

    VIDEOINCHANNEL *pChannel = viChannelFind(u32ClientId);

    if (pChannel)
    {
        if (cbData >= sizeof(VRDEVIDEOINMSGHDR))
        {
            const VRDEVIDEOINMSGHDR *pHdr = (const VRDEVIDEOINMSGHDR *)pvData;

            VIDEOINLOG(("IN: u32Length %d, u32DeviceId 0x%08X, u32MessageId 0x%08X, u16FunctionId %d, u16Status %d\n",
                        pHdr->u32Length, pHdr->u32DeviceId, pHdr->u32MessageId, pHdr->u16FunctionId, pHdr->u16Status));

            if (pChannel->enmStatus == VIDEO_IN_CHANNEL_NEGOTIATING)
            {
                /* Only a negotiation response is expected. */
                if (pHdr->u16FunctionId == VRDE_VIDEOIN_FN_NEGOTIATE)
                {
                    if (cbData >= sizeof(VRDEVIDEOINMSG_NEGOTIATE))
                    {
                        const VRDEVIDEOINMSG_NEGOTIATE *pMsg = (const VRDEVIDEOINMSG_NEGOTIATE *)pHdr;

                        VRDPLOGREL(("VIDEOIN for %d: status %d, ver %d, caps 0x%08X\n",
                                    u32ClientId, pHdr->u16Status, pMsg->u32Version, pMsg->fu32Capabilities));

                        if (   pHdr->u16Status == 0
                            && pMsg->u32Version <= VRDE_VIDEOIN_NEGOTIATE_VERSION)
                        {
                            /* A good client, the channel is ready. */
                            pChannel->enmStatus = VIDEO_IN_CHANNEL_ESTABLISHED;
                            VIDEOINLOG(("Client accepted\n"));

                            pChannel->u32Version = pMsg->u32Version;
                            pChannel->fu32Capabilities = pMsg->fu32Capabilities;
                        }
                        else
                        {
                            VRDPLOGREL(("VIDEOIN closing for %d\n", u32ClientId));
                            pClientChannel->CloseChannel();
                            viChannelRemove(pChannel);
                        }
                    }
                    else
                    {
                        VIDEOINIGNORE("Wrong NEGOTIATE length\n");
                        rc = VERR_NOT_SUPPORTED;
                    }
                }
                else
                {
                    VIDEOINIGNORE("Unexpected function\n");
                    rc = VERR_NOT_SUPPORTED;
                }
            }
            else if (pChannel->enmStatus == VIDEO_IN_CHANNEL_ESTABLISHED)
            {
                switch(pHdr->u16FunctionId)
                {
                    case VRDE_VIDEOIN_FN_NOTIFY:
                    {
                        rc = viOnNotify(pClientChannel, pHdr, cbData);
                    } break;

                    case VRDE_VIDEOIN_FN_DEVICEDESC:
                    {
                        rc = viOnDeviceDesc(pClientChannel, pHdr, cbData);
                    } break;

                    case VRDE_VIDEOIN_FN_CONTROL:
                    {
                        rc = viOnControl(pClientChannel, pHdr, cbData);
                    } break;

                    case VRDE_VIDEOIN_FN_CONTROL_NOTIFY:
                    {
                        rc = viOnControlNotify(pClientChannel, pHdr, cbData);
                    } break;

                    case VRDE_VIDEOIN_FN_FRAME:
                    {
                        rc = viOnFrame(pClientChannel, pHdr, cbData);
                    } break;

                    case VRDE_VIDEOIN_FN_NEGOTIATE:
                    default:
                    {
                        VIDEOINIGNORE("Invalid function\n");
                        rc = VERR_NOT_SUPPORTED;
                    } break;
                }
            }
            else
            {
                VIDEOINIGNORE("Invalid channel status\n");
                rc = VERR_NOT_SUPPORTED;
            }
        }
        else
        {
            VIDEOINIGNORE("Invalid message length\n");
            rc = VERR_NOT_SUPPORTED;
        }

        VRDPPktRelease(&pChannel->pkt);
    }
    else
    {
        VIDEOINIGNORE("no channel\n");
        rc = VERR_NOT_SUPPORTED;
    }

    if (RT_FAILURE(rc))
    {
        VIDEOINLOG(("VideoInOnData: %Rrc\n%.*Rhxd\n",
                    rc, cbData, pvData));
    }

    return rc;
}

/** @thread INPUT */
int VRDPVideoIn::VideoInOnClose(VideoInClient *pClientChannel)
{
    /** @todo The dynamic channel has been closed.
     *
     * Make sure that all pending IOs are cancelled
     * and the application is informed about device detach.
     */
    int rc = VINF_SUCCESS;

    VIDEOINLOG(("\n"));

    uint32_t u32ClientId = pClientChannel->Client()->Id();

    VIDEOINCHANNEL *pChannel = viChannelFind(u32ClientId);

    if (pChannel)
    {
        viChannelCleanup(pChannel);

        pChannel->enmStatus = VIDEO_IN_CHANNEL_VOID;

        /* Remove this channel from channels list. */
        viChannelRemove(pChannel);

        VRDPPktRelease(&pChannel->pkt);
    }
    else
    {
        VIDEOINLOG(("no channel\n"));
        rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}

/** @thread INPUT */
int VRDPVideoIn::viOnDeviceAttach(VideoInClient *pClientChannel, uint32_t u32DeviceId)
{
    uint32_t u32ClientId = pClientChannel->Client()->Id();

    VIDEOINLOG(("viOnDeviceAttach: [%d,%d]\n",
               u32ClientId, u32DeviceId));

    /* The client informs that there is a VideoIn device on this client.
     *
     * Remember this device.
     */

    /* Check if there is currently an attached device already. */
    VRDPVIDEOINDEVICE *pDev = viDeviceFind(u32ClientId, u32DeviceId);

    if (pDev)
    {
        /* Should not happen. Ignore the notification. */
        VIDEOINIGNORE("Device already attached\n");
        return VERR_INVALID_PARAMETER;
    }

    VIDEOINCHANNEL *pChannel = viChannelFind(u32ClientId);
    if (!pChannel)
    {
        VIDEOINIGNORE("No channel\n");
        return VERR_INVALID_PARAMETER;
    }

    VRDPPktAddRef(&pChannel->pkt);
    pDev = viDeviceAdd(pChannel, u32DeviceId);

    if (!pDev)
    {
        return VERR_NOT_SUPPORTED;
    }

    VRDEVIDEOINNOTIFYATTACH notify;
    notify.deviceHandle = pDev->handle;
    notify.u32Version = pChannel->u32Version;
    notify.fu32Capabilities = pChannel->fu32Capabilities;

    VRDPPktRelease(&pChannel->pkt);

    return viCallbackNotify(VRDE_VIDEOIN_NOTIFY_ID_ATTACH, &notify, sizeof(notify));
}

/** @thread any */
int VRDPVideoIn::viDeviceDetach(VideoInClient *pClientChannel, VRDPVIDEOINDEVICE *pDev)
{
    RT_NOREF1(pClientChannel);
    VIDEOINLOG(("viDeviceDetach: [%d,%d]\n",
               pDev->u32ClientId, pDev->u32DeviceId));

    VRDEVIDEOINNOTIFYDETACH notify;
    notify.deviceHandle = pDev->handle;

    if (pDev->pChannel)
    {
        VRDPPktRelease(&pDev->pChannel->pkt);
        pDev->pChannel = NULL;
    }

    /* This will release the device. */
    viDeviceRemove(pDev);

    return viCallbackNotify(VRDE_VIDEOIN_NOTIFY_ID_DETACH, &notify, sizeof(notify));
}

/** @thread INPUT */
int VRDPVideoIn::viOnDeviceDetach(VideoInClient *pClientChannel, uint32_t u32DeviceId)
{
    uint32_t u32ClientId = pClientChannel->Client()->Id();

    VIDEOINLOG(("viOnDeviceDetach: [%d,%d]\n",
               u32ClientId, u32DeviceId));

    /* The client informs that the VideoIn device was detached.
     * Find the device, release it and notify the interface user.
     */
    if (u32DeviceId == 0)
    {
        /* Special case, all devices are detached from this client. */
        VIDEOINCHANNEL *pChannel = viChannelFind(u32ClientId);

        if (!pChannel)
        {
            /* This should never happen. */
            VRDPLOGREL(("VIDEOIN no channel to detach for %d\n", u32ClientId));
            AssertFailed();
            return VERR_NOT_SUPPORTED;
        }

        viChannelCleanup(pChannel);
        return VINF_SUCCESS;
    }

    /* Check if there is currently an attached device. */
    VRDPVIDEOINDEVICE *pDev = viDeviceFind(u32ClientId, u32DeviceId);

    if (!pDev)
    {
        /* Should not happen. Ignore the notification. */
        VIDEOINIGNORE("device does not exist\n");
        return VERR_INVALID_PARAMETER;
    }

    return viDeviceDetach(pClientChannel, pDev);
}

/** @thread INPUT */
int VRDPVideoIn::viOnNegotiate(VideoInClient *pClientChannel)
{
    uint32_t u32ClientId = pClientChannel->Client()->Id();

    VIDEOINLOG(("viOnNegotiate: [%d]\n",
               u32ClientId));

    /* Client capabilities may be changed.
     *
     * Detach devices and send the negotiate request.
     */

    VIDEOINCHANNEL *pChannel = viChannelFind(u32ClientId);

    if (!pChannel)
    {
        /* This should never happen. */
        VRDPLOGREL(("VIDEOIN no channel to negotiate for %d\n", u32ClientId));
        AssertFailed();
        return VERR_NOT_SUPPORTED;
    }

    viChannelCleanup(pChannel);

    int rc = viSendNegotiate(pClientChannel);

    if (RT_SUCCESS(rc))
    {
        pChannel->enmStatus = VIDEO_IN_CHANNEL_NEGOTIATING;
        VRDPLOGREL(("VIDEOIN negotiate for %d\n", u32ClientId));
    }

    return rc;
}

int VRDPVideoIn::viOnNotify(VideoInClient *pClientChannel, const VRDEVIDEOINMSGHDR *pHdr, uint32_t cbMsg)
{
    int rc = VINF_SUCCESS;

    if (cbMsg >= sizeof(VRDEVIDEOINMSG_NOTIFY))
    {
        const VRDEVIDEOINMSG_NOTIFY *pMsg = (const VRDEVIDEOINMSG_NOTIFY *)pHdr;

        switch (pMsg->u32NotifyEvent)
        {
            case VRDE_VIDEOIN_NOTIFY_EVENT_ATTACH:
            {
                rc = viOnDeviceAttach(pClientChannel, pMsg->hdr.u32DeviceId);
            } break;
            case VRDE_VIDEOIN_NOTIFY_EVENT_DETACH:
            {
                rc = viOnDeviceDetach(pClientChannel, pMsg->hdr.u32DeviceId);
            } break;
            case VRDE_VIDEOIN_NOTIFY_EVENT_NEGOTIATE:
            {
                rc = viOnNegotiate(pClientChannel);
            } break;
            default:
            {
                VIDEOINIGNORE("Invalid notification event\n");
                rc = VERR_NOT_SUPPORTED;
            } break;
        }
    }
    else
    {
        VIDEOINIGNORE("invalid message length\n");
        rc = VERR_NOT_SUPPORTED;
    }

    return rc;
}

int VRDPVideoIn::viStatusToRC(uint16_t u16Status)
{
    switch (u16Status)
    {
        case VRDE_VIDEOIN_STATUS_SUCCESS: return VINF_SUCCESS;
        case VRDE_VIDEOIN_STATUS_FAILED:
        default:
            break;
    }
    return VERR_NOT_SUPPORTED;
}

int VRDPVideoIn::viOnDeviceDesc(VideoInClient *pClientChannel, const VRDEVIDEOINMSGHDR *pHdr, uint32_t cbMsg)
{
    /* This must be a response to a VRDEVIDEOINMSG_DEVICEDESC_REQ, so find the pending IO. */
    VIDEOINIOCTX *pCtx = NULL;

    int rc = fetchIO(pHdr->u32MessageId, (void **)&pCtx);

    if (RT_SUCCESS(rc))
    {
        const VRDEVIDEOINDEVICEDESC *pDevice;
        uint32_t cbDevice;

        int rcRequest = viStatusToRC(pHdr->u16Status);

        if (RT_SUCCESS(rcRequest))
        {
            const VRDEVIDEOINMSG_DEVICEDESC_RSP *pMsg = (const VRDEVIDEOINMSG_DEVICEDESC_RSP *)pHdr;
            pDevice = &pMsg->Device;
            cbDevice = cbMsg - sizeof(VRDEVIDEOINMSGHDR);
        }
        else
        {
            /* The client was not able to deliver a description. */
            pDevice = NULL;
            cbDevice = 0;
        }

        /* Check if the device does exist actually. */
        VRDPVIDEOINDEVICE *pDev = viDeviceFind(pClientChannel->Client()->Id(), pHdr->u32DeviceId);

        if (pDev && pDev->fAttached)
        {
            rc = viCallbackDeviceDesc(rcRequest, pDev->pvDeviceCtx, pCtx->pvUser, pDevice, cbDevice);
        }
        else
        {
            VIDEOINLOG(("viOnDeviceDesc: no device %p %d!!!\n",
                        pDev, pDev? pDev->fAttached: 0));
            VIDEOINIGNORE("Device is not attached\n");
            rc = VERR_NOT_SUPPORTED;
        }

        if (pDev)
        {
            VRDPPktRelease(&pDev->pkt);
        }

        VRDPPktRelease(&pCtx->pkt);
    }
    else
    {
        VIDEOINLOG(("viOnDeviceDesc: no pending IO!!!\n"));
        VIDEOINIGNORE("no pending IO\n");
    }

    return rc;
}

int VRDPVideoIn::viOnControlCommon(VideoInClient *pClientChannel, const VRDEVIDEOINMSGHDR *pHdr, uint32_t cbMsg, void *pvUser)
{
    int rc = VINF_SUCCESS;

    const VRDEVIDEOINCTRLHDR *pControl;
    uint32_t cbControl;

    int rcRequest = viStatusToRC(pHdr->u16Status);

    if (RT_SUCCESS(rcRequest))
    {
        const VRDEVIDEOINMSG_CONTROL *pMsg = (const VRDEVIDEOINMSG_CONTROL *)pHdr;
        pControl = &pMsg->Control;
        cbControl = cbMsg - sizeof(VRDEVIDEOINMSGHDR);
    }
    else
    {
        pControl = NULL;
        cbControl = 0;
    }

    /* Check if the device does exist actually. */
    VRDPVIDEOINDEVICE *pDev = viDeviceFind(pClientChannel->Client()->Id(), pHdr->u32DeviceId);

    if (pDev && pDev->fAttached)
    {
        rc = viCallbackControl(rcRequest, pDev->pvDeviceCtx, pvUser, pControl, cbControl);
    }
    else
    {
        VIDEOINLOG(("viOnControlCommon: no device %p %d!!!\n",
                    pDev, pDev? pDev->fAttached: 0));
        VIDEOINIGNORE("Device is not attached\n");
        rc = VERR_NOT_SUPPORTED;
    }

    if (pDev)
    {
        VRDPPktRelease(&pDev->pkt);
    }

    return rc;
}

int VRDPVideoIn::viOnControl(VideoInClient *pClientChannel, const VRDEVIDEOINMSGHDR *pHdr, uint32_t cbMsg)
{
    /* This is a response to a VRDEVIDEOINMSG_CONTROL. */
    VIDEOINIOCTX *pCtx = NULL;

    int rc = fetchIO(pHdr->u32MessageId, (void **)&pCtx);

    if (RT_SUCCESS(rc))
    {
        rc = viOnControlCommon(pClientChannel, pHdr, cbMsg, pCtx->pvUser);
        VRDPPktRelease(&pCtx->pkt);
    }
    else
    {
        VIDEOINLOG(("viOnControl: no pending IO!!!\n"));
        VIDEOINIGNORE("No pending IO\n");
    }

    return rc;
}

int VRDPVideoIn::viOnControlNotify(VideoInClient *pClientChannel, const VRDEVIDEOINMSGHDR *pHdr, uint32_t cbMsg)
{
    /* This is a notification from the client. */
    return viOnControlCommon(pClientChannel, pHdr, cbMsg, NULL);
}

int VRDPVideoIn::viOnFrame(VideoInClient *pClientChannel, const VRDEVIDEOINMSGHDR *pHdr, uint32_t cbMsg)
{
    /* The client delivers frames. There is no corresponding pending IO. */
    int rc = VINF_SUCCESS;

    const VRDEVIDEOINPAYLOADHDR *pFrame;
    uint32_t cbFrame;

    int rcRequest = viStatusToRC(pHdr->u16Status);

    if (RT_SUCCESS(rcRequest))
    {
        const VRDEVIDEOINMSG_FRAME *pMsg = (const VRDEVIDEOINMSG_FRAME *)pHdr;
        pFrame = &pMsg->Payload;
        cbFrame = cbMsg - sizeof(VRDEVIDEOINMSGHDR);
    }
    else
    {
        pFrame = NULL;
        cbFrame = 0;
    }

    /* Check if the device does exist actually. */
    VRDPVIDEOINDEVICE *pDev = viDeviceFind(pClientChannel->Client()->Id(), pHdr->u32DeviceId);

    if (pDev && pDev->fAttached)
    {
        rc = viCallbackFrame(rcRequest, pDev->pvDeviceCtx, pFrame, cbFrame);
    }
    else
    {
        VIDEOINLOG(("viOnFrame: no device %p %d!!!\n",
                    pDev, pDev? pDev->fAttached: 0));
        VIDEOINIGNORE("Device is not attached\n");
        rc = VERR_NOT_SUPPORTED;
    }

    if (pDev)
    {
        VRDPPktRelease(&pDev->pkt);
    }

    return rc;
}


/** @thread any */
int VRDPVideoIn::createIO(uint32_t *pu32CompletionId,
                          void *pvCtx)
{
    int rc = VINF_SUCCESS;

    if (RT_SUCCESS(VRDPLock::Lock(m_pLock)))
    {
        /* Allocate or reuse a completionId for IO request and associate pHdr, etc with it. */
        VIDEOINIO *pIO = RTListGetFirst(&m_IOCompletion.ListFree, VIDEOINIO, nodeIO);

        if (pIO)
        {
            /* There is a free id. */
            RTListNodeRemove(&pIO->nodeIO);
        }
        else
        {
            /* Allocate a new IO. */
            pIO = (VIDEOINIO *)VRDPMemAllocZ(sizeof(VIDEOINIO));
            if (pIO)
            {
                pIO->u32CompletionId = ASMAtomicIncU32(&m_IOCompletion.u32IdSrc);

                /* Since ids are reused, verify that there are not too many ids. */
                m_IOCompletion.cIds++;
                AssertLogRelMsg(m_IOCompletion.cIds < _64K,
                                ("%d\n", m_IOCompletion.cIds));
            }
            else
            {
                rc = VERR_NO_MEMORY;
            }
        }

        if (RT_SUCCESS(rc))
        {
            pIO->pvCtx = pvCtx;

            *pu32CompletionId = pIO->u32CompletionId;

            RTListAppend(&m_IOCompletion.ListUsed, &pIO->nodeIO);
        }

        VRDPLock::Unlock(m_pLock);
    }
    else
    {
        rc = VERR_NOT_SUPPORTED;
    }

    if (RT_SUCCESS(rc))
    {
        VIDEOINLOG(("Created Id %d\n",
                    *pu32CompletionId));
    }

    return rc;
}

/** @thread any */
int VRDPVideoIn::fetchIO(uint32_t u32CompletionId,
                         void **ppvCtx)
{
    /* Find the completionId context. */

    int rc = VINF_SUCCESS;

    if (RT_SUCCESS(VRDPLock::Lock(m_pLock)))
    {
        VIDEOINIO *pIO = NULL;

        VIDEOINIO *pIter;
        RTListForEach(&m_IOCompletion.ListUsed, pIter, VIDEOINIO, nodeIO)
        {
            if (pIter->u32CompletionId == u32CompletionId)
            {
                pIO = pIter;
                break;
            }
        }

        if (pIO)
        {
            /* Found the IO operation. Fetch the info and move the completionId to the Free list. */
            RTListNodeRemove(&pIO->nodeIO);

            /* Pass the context info to the caller. */
            if (ppvCtx != NULL)
            {
                *ppvCtx = pIO->pvCtx;
            }

            pIO->pvCtx = NULL;
            RTListAppend(&m_IOCompletion.ListFree, &pIO->nodeIO);
        }
        else
        {
            rc = VERR_NOT_FOUND;
        }

        VRDPLock::Unlock(m_pLock);
    }
    else
    {
        rc = VERR_NOT_SUPPORTED;
    }

    return rc;
}

/** @thread OUTPUT */
/* static */ DECLCALLBACK(void) VRDPVideoIn::processSubmitMsg(VRDPClient *pClient,
                                                              const VRDPOUTPUTGENERICHDR *pHdr)
{
    RT_NOREF1(pClient);
    VRDPVideoIn *pThis = (VRDPVideoIn *)pHdr->pvUser;
    AssertReturnVoid(pThis);
    uint32_t const u32CompletionId = pHdr->u32Parm;

    VIDEOINIOCTX *pCtx = NULL;
    int rc = pThis->fetchIO(u32CompletionId, (void **)&pCtx);
    AssertRCReturnVoid(rc);
    AssertReturnVoid(pCtx);
    AssertReturnVoidStmt(u32CompletionId == pCtx->u32CompletionId, VRDPPktRelease(&pCtx->pkt));

    pCtx->pChannel->pClientChannel->SendData(&pCtx->hdr, pCtx->hdr.u32Length);

    VRDPPktRelease(&pCtx->pkt);
}

int VRDPVideoIn::viSubmitMsg(uint16_t u16FunctionId,
                             VRDPVIDEOINDEVICE *pDev,
                             const void *pvParms,
                             uint32_t cbParms,
                             void *pvUser)
{
    VIDEOINLOG(("viSubmitMsg: %d %p %p %d %p\n",
                u16FunctionId, pDev, pvParms, cbParms, pvUser));

    int rc = VINF_SUCCESS;

    /*
     * Create a context structure, which will be kept until the client returns a response.
     */
    uint32_t cbAlloc = RT_OFFSETOF(VIDEOINIOCTX, au8MsgData) + cbParms;
    VIDEOINIOCTX *pCtx = (VIDEOINIOCTX *)VRDPMemAlloc(cbAlloc);

    if (pCtx)
    {
        VRDPPktInit(&pCtx->pkt, VRDP_PKT_TYPE_VIDEOINIO, cbAlloc, NULL, NULL);

        pCtx->pThis             = this;
        pCtx->pChannel          = pDev->pChannel;
        pCtx->pvUser            = pvUser;
        pCtx->u32CompletionId   = 0; /* Will be set later. */
        pCtx->u32ClientId       = pDev->u32ClientId;
        pCtx->u32DeviceId       = pDev->u32DeviceId;
        pCtx->hdr.u32Length     = sizeof(VRDEVIDEOINMSGHDR) + cbParms;
        pCtx->hdr.u32DeviceId   = pDev->u32DeviceId;
        pCtx->hdr.u32MessageId  = 0; /* Will be set later. */
        pCtx->hdr.u16FunctionId = u16FunctionId;
        pCtx->hdr.u16Status     = 0;
        if (cbParms > 0)
        {
            memcpy(&pCtx->au8MsgData[0], pvParms, cbParms);
        }

        VRDPPktAddRef(&pCtx->pkt);

        uint32_t u32CompletionId = 0;
        rc = createIO(&u32CompletionId, pCtx);

        if (RT_SUCCESS(rc))
        {
            pCtx->u32CompletionId  = u32CompletionId;
            pCtx->hdr.u32MessageId = u32CompletionId;

            /* Send the message. */
            VRDPOUTPUTGENERICHDR pkt;

            pkt.pfnCallback = processSubmitMsg;
            pkt.pvUser      = this;
            pkt.u32Size     = sizeof(pkt);
            pkt.u32Parm     = u32CompletionId;

            rc = m_pServer->PostOutput(VRDP_OUTPUT_GENERIC, pDev->u32ClientId, &pkt, sizeof(pkt));

            if (RT_FAILURE(rc))
            {
                /* Cancel. */
                fetchIO(u32CompletionId, NULL);
                VRDPPktRelease(&pCtx->pkt);
            }
        }
        else
            VRDPPktRelease(&pCtx->pkt);
    }
    else
    {
        rc = VERR_NO_MEMORY;
    }

    return rc;
}


/*
 * DVC channel proxy, which forwards the data to the VideoIn.
 * There can be only one VIDEOIN channel per client.
 * So it is ok to use only the client id to distinguish
 * channel instances.
 *
 * @todo make this generic, it should be used by server objects to talk to a channel instance (TSMF, VideoIn, etc).
 */

VideoInClient::VideoInClient(VRDPClient *pClient)
    :
    m_pClient (pClient),
    m_u8DVCChannelId (0)
{
}

VideoInClient::~VideoInClient()
{
    if (m_u8DVCChannelId)
    {
        m_pClient->Server()->VideoIn()->VideoInOnClose(this);
    }
}

int VideoInClient::RegisterChannel(void)
{
    int rc = m_pClient->TP()->DVC ()->RegisterChannel("RVIDEOIN",
                                                      dvcInput,
                                                      this,
                                                      &m_u8DVCChannelId);

    return rc;
}

/** @thread OUTPUT */
/* static */ DECLCALLBACK(void) VideoInClient::processCloseChannel(VRDPClient *pClient,
                                                                   const VRDPOUTPUTGENERICHDR *pHdr)
{
    VideoInClient *pThis = (VideoInClient *)pHdr->pvUser;

    pClient->TP()->DVC()->SendCloseChannel(pThis->m_u8DVCChannelId);
}

void VideoInClient::CloseChannel(void)
{
    VRDPOUTPUTGENERICHDR pkt;

    pkt.pfnCallback = processCloseChannel;
    pkt.pvUser      = this;
    pkt.u32Size     = sizeof(pkt);
    pkt.u32Parm     = 0;

    m_pClient->Server()->PostOutput(VRDP_OUTPUT_GENERIC, m_pClient->Id(), &pkt, sizeof(pkt));
    m_pClient->Server()->RaiseOutputEvent();
}

typedef struct VRDPOUTPUTSENDDATA
{
    VRDPOUTPUTGENERICHDR hdr;
    void *pvData;
    uint32_t cbData;
} VRDPOUTPUTSENDDATA;

/** @thread OUTPUT */
/* static */ DECLCALLBACK(void) VideoInClient::processOutput(VRDPClient *pClient,
                                                             const VRDPOUTPUTGENERICHDR *pHdr)
{
    VideoInClient *pThis = (VideoInClient *)pHdr->pvUser;

    VRDPOUTPUTSENDDATA *pPkt = (VRDPOUTPUTSENDDATA *)pHdr;

    pClient->TP()->DVC()->SendData(pThis->m_u8DVCChannelId, pPkt->pvData, pPkt->cbData);

    RTMemFree(pPkt->pvData);
}

int VideoInClient::SendData(const void *pvData, uint32_t cbData)
{
    /** @todo VRDPLOGRELIO */ VIDEOINLOG(("VIDEOIN send %d:\n%.*Rhxd\n", cbData, RT_MIN(64, cbData), pvData));

    if (m_pClient->Server()->IsOutputThread())
    {
        return m_pClient->TP()->DVC()->SendData(m_u8DVCChannelId, (void *)pvData, cbData);
    }

    /* Forward to the output thread. */
    VRDPOUTPUTSENDDATA pkt;

    pkt.hdr.pfnCallback = processOutput;
    pkt.hdr.pvUser      = this;
    pkt.hdr.u32Size     = sizeof(pkt);
    pkt.hdr.u32Parm     = 0;
    pkt.pvData = RTMemDup(pvData, cbData);
    if (!pkt.pvData)
    {
        return VERR_NO_MEMORY;
    }
    pkt.cbData = cbData;

    return m_pClient->Server()->PostOutput(VRDP_OUTPUT_GENERIC, m_pClient->Id(), &pkt, sizeof(pkt));
}


/** @thread INPUT */
/* static */ void VideoInClient::dvcInput(void *pvCtx, uint32_t u32Event, const void *pvData, uint32_t cbData)
{
    VideoInClient *pThis = (VideoInClient *)pvCtx;

    pThis->processInput(u32Event, pvData, cbData);
}

/** @thread INPUT */
void VideoInClient::processInput(uint32_t u32Event, const void *pvData, uint32_t cbData)
{
    VIDEOINLOG(("this %p, u32Event %d, pvData %p, cbData %d\n",
                 this, u32Event, pvData, cbData));

    int rc = VINF_SUCCESS;

    switch (u32Event)
    {
        case DVC_EVENT_CREATE:
        {
            VRDPLOGREL(("VideoIn[%d] enabled: %d\n",
                        m_pClient->Id(), m_u8DVCChannelId));

            /* Client accepted the channel. */
            m_pClient->Server()->VideoIn()->VideoInOnCreate(this);
        } break;

        case DVC_EVENT_DATA:
        {
            m_pClient->Server()->VideoIn()->VideoInOnData(this, pvData, cbData);
        } break;

        case DVC_EVENT_CLOSE:
        {
            VRDPLOGREL(("VideoIn[%d] closed: %d\n", m_pClient->Id(), m_u8DVCChannelId));

            m_u8DVCChannelId = 0;

            m_pClient->Server()->VideoIn()->VideoInOnClose(this);
        } break;

        default:
        {
            AssertFailed();
        }
    }

    if (RT_FAILURE(rc))
    {
        VRDPLOGRELLIMIT(32, ("VRDP: VIDEOIN: input %Rrc!!!\n", rc));
    }
}
